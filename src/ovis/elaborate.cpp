#include <algorithm>
#include <cctype>
#include <map>
#include <set>

#include <ovis/core/elaborate.hpp>

namespace ovis::core {

namespace {

auto loose(string_view_type p_name) -> string_type {
  string_type result;
  for (const auto character : p_name)
    if (character != '_' && character != '-')
      result.push_back(
          static_cast<char_type>(std::tolower(static_cast<unsigned char>(character))));
  return result;
}

auto edit_distance(string_view_type p_left, string_view_type p_right)
    -> std::size_t {
  std::vector<std::size_t> row(p_right.size() + 1);
  for (std::size_t column = 0; column < row.size(); ++column)
    row[column] = column;
  for (std::size_t line = 1; line <= p_left.size(); ++line) {
    auto diagonal = row[0];
    row[0] = line;
    for (std::size_t column = 1; column <= p_right.size(); ++column) {
      const auto above = row[column];
      row[column] = std::min(
          {row[column] + 1, row[column - 1] + 1,
           diagonal + (p_left[line - 1] == p_right[column - 1] ? 0 : 1)});
      diagonal = above;
    }
  }
  return row.back();
}

// Type inference.
//
// Every variable, parameter, return value and expression gets a slot. Slots
// that must hold the same type are merged (union-find); values flowing into a
// slot widen it (int -> float). Pinned slots have a stated type and never
// change; the checker reports anything that doesn't fit them.
class inference {
public:
  enum class kind {
    none,
    integer,
    real,
    boolean,
    string,
    pointer,
    memory,
    nothing,
    list,
    record
  };

  using slot_id = std::size_t;

private:
  struct slot {
    slot_id parent;
    kind value = kind::none;
    slot_id element = 0; // for lists
    string_type record;  // for records
    bool pinned = false;
  };
  // `(field X NAME)` and `(set-field X NAME ...)`: the field's slot is only
  // known once X's record is.
  struct field_access {
    slot_id object;
    string_type field;
    slot_id result;
    bool is_done = false;
  };

  std::vector<slot> slots;
  std::vector<std::pair<slot_id, slot_id>> flows;
  // Applied only if the first slot ends up unconstrained.
  std::vector<std::pair<slot_id, slot_id>> fallbacks;
  std::vector<field_access> accesses;
  // record → field → slot.
  std::map<string_type, std::map<string_type, slot_id>> record_fields;

  static auto kind_of(base_type p_base) -> kind {
    switch (p_base) {
    case base_type::nothing:
      return kind::nothing;
    case base_type::integer:
      return kind::integer;
    case base_type::real:
      return kind::real;
    case base_type::boolean:
      return kind::boolean;
    case base_type::string:
      return kind::string;
    case base_type::pointer:
      return kind::pointer;
    case base_type::memory:
      return kind::memory;
    case base_type::unknown:
      return kind::none;
    case base_type::record:
      return kind::record;
    }
    return kind::none;
  }

  // Returns whether anything changed.
  auto widen(slot_id p_target, const slot &p_source) -> bool {
    auto &target = slots[find(p_target)];
    if (p_source.value == kind::none)
      return false;
    if (p_source.value == kind::list) {
      if (target.value == kind::none && !target.pinned) {
        target.value = kind::list;
        target.element = p_source.element;
        return true;
      }
      if (target.value == kind::list)
        return unify(target.element, p_source.element);
      return false;
    }
    if (target.pinned || target.value == p_source.value)
      return false;
    if (p_source.value == kind::record) {
      if (target.value != kind::none)
        return false; // A different type: the checker explains.
      target.value = kind::record;
      target.record = p_source.record;
      return true;
    }
    if (target.value == kind::none ||
        (target.value == kind::integer && p_source.value == kind::real) ||
        // `+` with text joins text.
        (p_source.value == kind::string &&
         (target.value == kind::integer || target.value == kind::real))) {
      target.value = p_source.value;
      return true;
    }
    return false;
  }

public:
  struct trait_hint {
    slot_id slot;
    string_type trait;
    source_location location;
    std::size_t unit_index;
  };
  // Slots used as the value of a trait's phrase: if nothing else says what
  // they are, they hold that trait.
  std::vector<trait_hint> hints;

  auto find(slot_id p_slot) -> slot_id {
    while (slots[p_slot].parent != p_slot) {
      slots[p_slot].parent = slots[slots[p_slot].parent].parent;
      p_slot = slots[p_slot].parent;
    }
    return p_slot;
  }

  auto fresh() -> slot_id {
    slots.push_back({slots.size()});
    return slots.size() - 1;
  }

  auto of(const value_type &p_type, bool p_pinned) -> slot_id {
    const auto result = fresh();
    if (p_type.is_list) {
      const auto element = of(p_type.element(), p_pinned);
      slots[result].value = kind::list;
      slots[result].element = element;
      slots[result].pinned = p_pinned;
      return result;
    }
    slots[result].value = kind_of(p_type.base);
    slots[result].record = p_type.record;
    slots[result].pinned = p_pinned && p_type.is_known();
    return result;
  }

  auto add_record_field(const string_type &p_record, const string_type &p_field,
                        slot_id p_slot) -> void {
    record_fields[p_record][p_field] = p_slot;
  }

  // The slot of `p_object`'s field `p_field`, settled while solving.
  auto access(slot_id p_object, const string_type &p_field) -> slot_id {
    const auto result = fresh();
    accesses.push_back({p_object, p_field, result});
    return result;
  }

  auto value(slot_id p_slot) -> kind { return slots[find(p_slot)].value; }

  auto hint(slot_id p_slot, string_type p_trait, source_location p_location,
            std::size_t p_unit_index) -> void {
    hints.push_back({p_slot, std::move(p_trait), p_location, p_unit_index});
  }

  // Gives each still-open slot the trait whose phrases were used on it.
  // Slots used with phrases of several traits are returned (with where) so
  // the author can be asked for the type. Returns whether any slot changed.
  auto settle_hints(std::vector<std::pair<trait_hint, string_type>> &p_unclear)
      -> bool {
    std::map<slot_id, std::set<string_type>> by_slot;
    std::map<slot_id, const trait_hint *> first;
    for (const auto &each : hints) {
      const auto root = find(each.slot);
      if (slots[root].value != kind::none || slots[root].pinned)
        continue;
      by_slot[root].insert(each.trait);
      first.try_emplace(root, &each);
    }
    bool changed = false;
    for (const auto &[root, names] : by_slot) {
      if (names.size() == 1) {
        slots[root].value = kind::record;
        slots[root].record = *names.begin();
        changed = true;
        continue;
      }
      string_type joined;
      for (const auto &name : names)
        joined += (joined.empty() ? "" : "' and '") + name;
      p_unclear.emplace_back(*first.at(root), joined);
    }
    return changed;
  }

  // The element slot of a list, making the slot a list if it is still open.
  auto element_of(slot_id p_slot) -> slot_id {
    const auto root = find(p_slot);
    if (slots[root].value == kind::none && !slots[root].pinned) {
      const auto element = fresh();
      slots[find(p_slot)].value = kind::list;
      slots[find(p_slot)].element = element;
    }
    const auto &result = slots[find(p_slot)];
    return result.value == kind::list ? result.element : fresh();
  }

  auto unify(slot_id p_left, slot_id p_right) -> bool {
    const auto left = find(p_left);
    const auto right = find(p_right);
    if (left == right)
      return false;
    const auto left_state = slots[left];
    const auto right_state = slots[right];
    slots[right].parent = left;
    if (right_state.pinned && !left_state.pinned) {
      slots[left].value = right_state.value;
      slots[left].element = right_state.element;
      slots[left].record = right_state.record;
      slots[left].pinned = true;
      if (left_state.value == kind::list && right_state.value == kind::list)
        unify(left_state.element, right_state.element);
    } else if (!left_state.pinned) {
      widen(left, right_state);
    } else if (left_state.value == kind::list &&
               right_state.value == kind::list) {
      unify(left_state.element, right_state.element);
    }
    return true;
  }

  auto flow(slot_id p_from, slot_id p_to) -> void {
    flows.emplace_back(p_from, p_to);
  }
  auto fallback(slot_id p_slot, slot_id p_default) -> void {
    fallbacks.emplace_back(p_slot, p_default);
  }

  auto solve() -> void {
    bool changed = true;
    while (changed) {
      changed = false;
      for (const auto &[from, to] : flows)
        changed = widen(to, slots[find(from)]) || changed;
      changed = settle_accesses() || changed;
      if (changed)
        continue;
      for (const auto &[target, preferred] : fallbacks) {
        if (value(target) == kind::none && value(preferred) != kind::none) {
          unify(target, preferred);
          changed = true;
        }
      }
      if (!changed)
        changed = guess_records();
    }
  }

  // Joins each access whose record is known to that record's field.
  auto settle_accesses() -> bool {
    bool changed = false;
    for (auto &access : accesses) {
      if (access.is_done)
        continue;
      const auto &object = slots[find(access.object)];
      if (object.value != kind::record)
        continue;
      access.is_done = true;
      const auto record = record_fields.find(object.record);
      if (record == record_fields.end())
        continue;
      const auto field = record->second.find(access.field);
      if (field == record->second.end())
        continue; // No such field: the checker explains.
      unify(field->second, access.result);
      changed = true;
    }
    return changed;
  }

  // `(field b title)` where nothing else says what `b` is: the record, if
  // only one has a field of that name.
  auto guess_records() -> bool {
    for (auto &access : accesses) {
      if (access.is_done || value(access.object) != kind::none ||
          slots[find(access.object)].pinned)
        continue;
      std::vector<string_type> owners;
      for (const auto &[record, fields] : record_fields)
        if (fields.contains(access.field))
          owners.push_back(record);
      if (owners.size() != 1)
        continue;
      auto &object = slots[find(access.object)];
      object.value = kind::record;
      object.record = owners.front();
      return true;
    }
    return false;
  }

  // `unknown` where nothing decided the type.
  auto resolve(slot_id p_slot) -> value_type {
    const auto &found = slots[find(p_slot)];
    switch (found.value) {
    case kind::none:
      return value_type::unknown();
    case kind::integer:
      return value_type::integer();
    case kind::real:
      return value_type::real();
    case kind::boolean:
      return value_type::boolean();
    case kind::string:
      return value_type::string();
    case kind::pointer:
      return value_type::pointer();
    case kind::memory:
      return value_type::memory();
    case kind::nothing:
      return value_type::nothing();
    case kind::list: {
      const auto element = resolve(found.element);
      return value_type::list_of(element);
    }
    case kind::record:
      return value_type::record_of(found.record);
    }
    return value_type::unknown();
  }
};

using slot_id = inference::slot_id;

class elaborator {
  struct function_info {
    function *declaration;
    std::size_t unit_index;
    std::vector<slot_id> parameters;
    slot_id result;
    bool returns_value = false;
  };
  struct variable_target {
    statement *declaration;
    slot_id variable;
    slot_id initial_value;
    bool has_initial_value;
    std::size_t unit_index;
  };
  struct list_target {
    expression *list;
    slot_id element;
    std::vector<slot_id> items;
    std::size_t unit_index;
  };
  struct condition_target {
    expression *condition;
    slot_id slot;
    std::size_t unit_index;
  };
  struct field_target {
    record_field *declaration;
    string_type record;
    slot_id slot;
    source_location location;
    std::size_t unit_index;
  };

  program &target;
  inference types;
  std::vector<diagnostic> notes;
  std::map<string_type, function_info> functions;
  std::map<string_type, const c_function *> c_functions;
  std::map<string_type, slot_id> globals;
  std::vector<std::map<string_type, slot_id>> scopes;
  // Trait methods by name, and which records implement which traits.
  std::map<string_type, trait *> method_owner;
  std::set<std::pair<string_type, string_type>> implemented;
  std::vector<diagnostic> problems;
  function_info *current_function = nullptr;
  std::size_t unit_index = 0;

  std::vector<variable_target> variables;
  std::vector<list_target> lists;
  std::vector<condition_target> conditions;
  std::vector<field_target> fields;

  auto note(source_location p_location, string_type p_message) -> void {
    for (const auto &existing : notes)
      if (existing.unit_index == unit_index && existing.message == p_message)
        return;
    notes.push_back({unit_index, p_location, std::move(p_message)});
  }

  auto at_top_level() const -> bool {
    return current_function == nullptr && scopes.size() == 1;
  }

  auto lookup(const string_type &p_name) -> std::optional<slot_id> {
    for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope)
      if (const auto found = scope->find(p_name); found != scope->end())
        return found->second;
    if (const auto found = globals.find(p_name); found != globals.end())
      return found->second;
    return std::nullopt;
  }

  auto declare(const string_type &p_name, slot_id p_slot) -> void {
    if (at_top_level())
      globals[p_name] = p_slot;
    else
      scopes.back()[p_name] = p_slot;
  }

  auto visible_variables() const -> std::vector<string_type> {
    std::set<string_type> names;
    for (const auto &scope : scopes)
      for (const auto &[name, slot] : scope)
        names.insert(name);
    for (const auto &[name, slot] : globals)
      names.insert(name);
    return {names.begin(), names.end()};
  }

  auto callable_names() const -> std::vector<string_type> {
    std::vector<string_type> names;
    for (const auto &[name, info] : functions)
      names.push_back(name);
    for (const auto &[name, function] : c_functions)
      names.push_back(name);
    for (const auto &[name, owner] : method_owner)
      names.push_back(name);
    for (const auto &name : builtin_names())
      if (std::isalpha(static_cast<unsigned char>(name.front())))
        names.emplace_back(name);
    return names;
  }

  auto is_callable(const string_type &p_name) const -> bool {
    return functions.contains(p_name) || c_functions.contains(p_name) ||
           method_owner.contains(p_name) || is_builtin(p_name);
  }

  // Expressions.

  auto walk_arguments(expression &p_call) -> std::vector<slot_id> {
    std::vector<slot_id> result;
    for (auto &argument : p_call.arguments)
      result.push_back(walk(argument));
    return result;
  }

  auto known(const value_type &p_type) -> slot_id {
    return types.of(p_type, false);
  }

  auto walk_call(expression &p_call) -> slot_id {
    auto &name = p_call.text;
    if (!is_callable(name) && !lookup(name)) {
      if (const auto guess = closest_name(name, callable_names())) {
        note(p_call.location, "Read `" + name + "` as `" + *guess + "`.");
        name = *guess;
      }
    }
    const auto arguments = walk_arguments(p_call);

    if (name == "+" || name == "-" || name == "*" || name == "%" ||
        name == "min" || name == "max" || name == "abs") {
      const auto result = types.fresh();
      for (const auto argument : arguments)
        types.flow(argument, result);
      return result;
    }
    if (name == "/" || name == "float" || name == "sqrt" || name == "pow" ||
        name == "floor" || name == "read_float")
      return known(value_type::real());
    if (name == "div" || name == "int" || name == "read_int" ||
        name == "random" || name == "char_at")
      return known(value_type::integer());
    if (name == "=" || name == "!=" || name == "<" || name == "<=" ||
        name == ">" || name == ">=") {
      if (arguments.size() == 2) {
        types.fallback(arguments[0], arguments[1]);
        types.fallback(arguments[1], arguments[0]);
      }
      return known(value_type::boolean());
    }
    if (name == "and" || name == "or" || name == "not") {
      for (const auto argument : arguments)
        types.fallback(argument, known(value_type::boolean()));
      return known(value_type::boolean());
    }
    if (name == "ref")
      return known(value_type::pointer());
    if (name == "new" && p_call.type_argument) {
      const auto result = known(*p_call.type_argument);
      for (std::size_t index = 0;
           index < std::min(arguments.size(), p_call.fields.size()); ++index)
        types.flow(arguments[index], types.access(result, p_call.fields[index]));
      return result;
    }
    if (name == "field") {
      if (arguments.empty() || p_call.fields.empty())
        return types.fresh();
      return types.access(arguments[0], p_call.fields[0]);
    }
    if (name == "concat" || name == "substring" || name == "to_str" ||
        name == "chr" || name == "buffer" || name == "read_line")
      return known(value_type::string());
    if (name == "len" || name == "sort" || name == "reverse") {
      if (!arguments.empty())
        types.element_of(arguments[0]);
      return known(name == "len" ? value_type::integer()
                                 : value_type::nothing());
    }
    if (name == "get") {
      if (arguments.empty())
        return types.fresh();
      return types.element_of(arguments[0]);
    }
    if (name == "new-memory")
      return known(value_type::memory());
    if (name == "clear-memory" || name == "free")
      return known(value_type::nothing());
    // `keep` gives back what it is given.
    if (name == "keep" || name == "copy")
      return arguments.empty() ? types.fresh() : arguments[0];
    if (name == "remove-at") {
      if (arguments.size() == 2) {
        types.element_of(arguments[0]);
        types.flow(arguments[1], types.of(value_type::integer(), true));
      }
      return known(value_type::nothing());
    }
    if (name == "append") {
      if (arguments.size() == 2)
        types.flow(arguments[1], types.element_of(arguments[0]));
      return known(value_type::nothing());
    }
    if (name == "list" || name == "array") {
      const auto element =
          p_call.type_argument ? types.of(*p_call.type_argument, true)
                               : types.fresh();
      if (name == "list")
        for (const auto argument : arguments)
          types.flow(argument, element);
      if (name == "list" ||
          (p_call.type_argument && !p_call.type_argument->is_known()))
        lists.push_back({&p_call, element,
                         name == "list" ? arguments
                                        : std::vector<slot_id>(),
                         unit_index});
      auto result = types.fresh();
      types.unify(result, types.of(value_type::list_of(base_type::unknown),
                                   false));
      types.unify(types.element_of(result), element);
      return result;
    }

    if (const auto method = method_owner.find(name);
        method != method_owner.end() && !arguments.empty()) {
      auto &owner = *method->second;
      const auto &signature = *owner.find(name);
      types.hint(arguments[0], owner.name, p_call.location, unit_index);
      // A default method is also a function, with the types it works out.
      if (!signature.has_default) {
        for (std::size_t index = 1;
             index < std::min(arguments.size(), signature.parameters.size());
             ++index)
          types.flow(arguments[index], known(signature.parameters[index].type));
        return known(signature.return_type);
      }
    }
    if (const auto found = functions.find(name); found != functions.end()) {
      auto &callee = found->second;
      for (std::size_t index = 0;
           index < std::min(arguments.size(), callee.parameters.size());
           ++index)
        types.flow(arguments[index], callee.parameters[index]);
      return callee.result;
    }
    if (const auto found = c_functions.find(name); found != c_functions.end()) {
      const auto &callee = *found->second;
      if (!callee.is_variadic &&
          arguments.size() + 1 == callee.parameters.size() &&
          callee.parameters.back() == c_type::pointer_type) {
        note(p_call.location, "`" + name +
                                  "` gets null for its last argument, which "
                                  "was left out.");
        p_call.arguments.push_back({.kind = expression::kind_type::variable,
                                    .text = "null",
                                    .location = p_call.location});
      }
      for (std::size_t index = 0;
           index < std::min(arguments.size(), callee.parameters.size());
           ++index) {
        types.fallback(arguments[index],
                       known(ovis_type_of(callee.parameters[index])));
        // (ref x) into `int *` or `double *`: x holds that kind of value.
        const auto &argument = p_call.arguments[index];
        if (argument.kind == expression::kind_type::call &&
            argument.text == "ref" && argument.arguments.size() == 1 &&
            index < callee.pointees.size() && callee.pointees[index])
          if (const auto variable = lookup(argument.arguments[0].text))
            types.fallback(*variable,
                           known(ovis_type_of(*callee.pointees[index])));
      }
      return types.of(ovis_type_of(callee.result), true);
    }
    return types.fresh();
  }

  auto walk(expression &p_expression) -> slot_id {
    using kind = expression::kind_type;
    switch (p_expression.kind) {
    case kind::integer:
      return known(value_type::integer());
    case kind::real:
      return known(value_type::real());
    case kind::boolean:
      return known(value_type::boolean());
    case kind::string:
      return known(value_type::string());
    case kind::variable: {
      if (const auto found = lookup(p_expression.text))
        return *found;
      if (const auto *const constant = find_constant(target, p_expression.text)) {
        switch (constant->kind) {
        case c_constant::kind_type::integer:
          return known(value_type::integer());
        case c_constant::kind_type::real:
          return known(value_type::real());
        case c_constant::kind_type::null:
          return known(value_type::pointer());
        case c_constant::kind_type::runtime:
          return known(p_expression.text.starts_with("std")
                           ? value_type::pointer()
                           : value_type::integer());
        }
      }
      if (const auto guess =
              closest_name(p_expression.text, visible_variables())) {
        note(p_expression.location,
             "Read `" + p_expression.text + "` as `" + *guess + "`.");
        p_expression.text = *guess;
        return *lookup(*guess);
      }
      return types.fresh();
    }
    case kind::call:
      return walk_call(p_expression);
    }
    return types.fresh();
  }

  // Statements.

  auto walk_block(std::vector<statement> &p_body) -> void {
    scopes.emplace_back();
    for (auto &statement : p_body)
      walk(statement);
    scopes.pop_back();
  }

  auto walk_condition(expression &p_condition) -> void {
    const auto slot = walk(p_condition);
    types.fallback(slot, known(value_type::boolean()));
    conditions.push_back({&p_condition, slot, unit_index});
  }

  auto existing_in_scope(const string_type &p_name) -> std::optional<slot_id> {
    if (at_top_level()) {
      if (const auto found = globals.find(p_name); found != globals.end())
        return found->second;
      return std::nullopt;
    }
    if (const auto found = scopes.back().find(p_name);
        found != scopes.back().end())
      return found->second;
    return std::nullopt;
  }

  auto walk_let(statement &p_statement) -> void {
    const bool has_value = !p_statement.values.empty();
    const auto value =
        has_value ? walk(p_statement.values[0]) : types.fresh();

    if (const auto existing = existing_in_scope(p_statement.name);
        existing && has_value) {
      types.flow(value, *existing);
      return;
    }

    const bool is_stated =
        p_statement.declared_type && p_statement.declared_type->is_known() &&
        !(p_statement.declared_type->is_list &&
          p_statement.declared_type->base == base_type::unknown);
    const auto variable =
        p_statement.declared_type ? types.of(*p_statement.declared_type, true)
                                  : types.fresh();
    types.flow(value, variable);
    if (!is_stated)
      variables.push_back({&p_statement, variable, value, has_value,
                           unit_index});
    declare(p_statement.name, variable);
  }

  auto walk(statement &p_statement) -> void {
    using kind = statement::kind_type;
    auto &values = p_statement.values;
    switch (p_statement.kind) {
    case kind::let:
      walk_let(p_statement);
      break;
    case kind::set:
      if (const auto found = lookup(p_statement.name)) {
        types.flow(walk(values[0]), *found);
      } else {
        // Assigning to a new name declares it.
        p_statement.kind = kind::let;
        walk_let(p_statement);
      }
      break;
    case kind::put: {
      const auto list = lookup(p_statement.name);
      const auto index = walk(values[0]);
      const auto value = walk(values[1]);
      types.fallback(index, known(value_type::integer()));
      if (list)
        types.flow(value, types.element_of(*list));
      break;
    }
    case kind::print:
    case kind::evaluate:
      for (auto &value : values)
        walk(value);
      break;
    case kind::if_:
      walk_condition(values[0]);
      walk_block(p_statement.body);
      walk_block(p_statement.else_body);
      break;
    case kind::while_:
      walk_condition(values[0]);
      walk_block(p_statement.body);
      break;
    case kind::for_: {
      types.fallback(walk(values[0]), known(value_type::integer()));
      types.fallback(walk(values[1]), known(value_type::integer()));
      scopes.push_back(
          {{p_statement.name, types.of(value_type::integer(), true)}});
      walk_block(p_statement.body);
      scopes.pop_back();
      break;
    }
    case kind::each: {
      const auto list = walk(values[0]);
      scopes.push_back({{p_statement.name, types.element_of(list)}});
      walk_block(p_statement.body);
      scopes.pop_back();
      break;
    }
    case kind::return_:
      if (current_function != nullptr && !values.empty()) {
        current_function->returns_value = true;
        types.flow(walk(values[0]), current_function->result);
      } else {
        for (auto &value : values)
          walk(value);
      }
      break;
    case kind::set_field: {
      const auto object = walk(values[0]);
      types.flow(walk(values[1]), types.access(object, p_statement.name));
      break;
    }
    case kind::with_memory:
      if (!values.empty())
        types.fallback(walk(values[0]), known(value_type::memory()));
      walk_block(p_statement.body);
      break;
    case kind::defer:
      walk_block(p_statement.body);
      break;
    case kind::break_:
    case kind::continue_:
    case kind::pass:
      break;
    }
  }

  // Implicit return.

  static auto has_value_return(const std::vector<statement> &p_body) -> bool {
    for (const auto &statement : p_body) {
      if (statement.kind == statement::kind_type::return_ &&
          !statement.values.empty())
        return true;
      if (has_value_return(statement.body) ||
          has_value_return(statement.else_body))
        return true;
    }
    return false;
  }

  auto produces_value(const function &p_function,
                      const expression &p_expression) const -> bool {
    if (p_expression.kind != expression::kind_type::call)
      return true;
    const auto &name = p_expression.text;
    if (name == "append" || name == "sort" || name == "reverse" ||
        name == "remove-at" || name == "free" || name == "clear-memory")
      return false;
    if (is_builtin(name))
      return true;
    // Calls made for their effect (`printf`, a procedure) shouldn't turn
    // into the result unless the function says it returns something.
    if (!p_function.return_type.is_known())
      return false;
    return c_functions.contains(name) || functions.contains(name) ||
           method_owner.contains(name);
  }

  // `(fn square (x) (* x x))` returns `(* x x)`.
  auto add_implicit_return(function &p_function) -> void {
    if (p_function.return_type == value_type::nothing() ||
        p_function.body.empty() || has_value_return(p_function.body))
      return;
    // A trailing `defer` is cleanup, not the function's value.
    auto last_index = p_function.body.size();
    while (last_index > 0 &&
           p_function.body[last_index - 1].kind == statement::kind_type::defer)
      --last_index;
    if (last_index == 0)
      return;
    auto &last = p_function.body[last_index - 1];
    if (last.kind == statement::kind_type::evaluate &&
        produces_value(p_function, last.values[0])) {
      last.kind = statement::kind_type::return_;
      // The return goes after the defers, or it would leave before they are
      // registered and they would never run.
      std::rotate(p_function.body.begin() + (last_index - 1),
                  p_function.body.begin() + last_index, p_function.body.end());
      note(last.location, "`" + p_function.name +
                              "` gives back the value it works out last.");
      return;
    }
    if ((last.kind == statement::kind_type::let ||
         last.kind == statement::kind_type::set) &&
        p_function.return_type.is_known()) {
      statement result{.kind = statement::kind_type::return_,
                       .location = last.location};
      result.values.push_back({.kind = expression::kind_type::variable,
                               .text = last.name,
                               .location = last.location});
      note(last.location, "`" + p_function.name + "` gives back `" + last.name +
                              "`, the last value it works out.");
      p_function.body.push_back(std::move(result));
    }
  }

  // Writing the results back.

  // The inferred type, or the default when nothing decided it.
  auto settle(slot_id p_slot, bool &p_was_defaulted) -> value_type {
    const auto type = types.resolve(p_slot);
    p_was_defaulted = !type.is_known();
    if (!p_was_defaulted)
      return type;
    return type.is_list ? value_type::list_of(base_type::integer)
                        : value_type::integer();
  }

  auto write_back() -> void {
    for (auto &field : fields) {
      if (field.declaration->type.is_known() &&
          !(field.declaration->type.is_list &&
            field.declaration->type.base == base_type::unknown))
        continue;
      unit_index = field.unit_index;
      bool was_defaulted = false;
      field.declaration->type = settle(field.slot, was_defaulted);
      auto owner = describe_in_english(value_type::record_of(field.record));
      owner.front() = 'A';
      note(field.location,
           owner + "'s `" + field.declaration->name + "` is " +
               describe_in_english(field.declaration->type) +
               (was_defaulted ? ", since nothing says otherwise."
                              : ", from the values it is given."));
    }

    for (auto &[name, info] : functions) {
      unit_index = info.unit_index;
      auto &declaration = *info.declaration;
      for (std::size_t index = 0; index < info.parameters.size(); ++index) {
        auto &parameter = declaration.parameters[index];
        if (parameter.type.is_known() &&
            !(parameter.type.is_list &&
              parameter.type.base == base_type::unknown))
          continue;
        bool was_defaulted = false;
        parameter.type = settle(info.parameters[index], was_defaulted);
        note(declaration.location,
             "`" + name + "` takes `" + parameter.name + "` as " +
                 describe_in_english(parameter.type) +
                 (was_defaulted ? ", since nothing says otherwise."
                                : ", from how it is used."));
      }
      if (declaration.return_type.is_known())
        continue;
      if (!info.returns_value) {
        declaration.return_type = value_type::nothing();
        continue;
      }
      bool was_defaulted = false;
      declaration.return_type = settle(info.result, was_defaulted);
      note(declaration.location,
           "`" + name + "` gives back " +
               describe_in_english(declaration.return_type) +
               (was_defaulted ? ", since nothing says otherwise."
                              : ", from what it returns."));
    }

    for (auto &variable : variables) {
      unit_index = variable.unit_index;
      auto &declaration = *variable.declaration;
      const auto natural = types.resolve(variable.initial_value);
      bool was_defaulted = false;
      const auto type = settle(variable.variable, was_defaulted);
      if (was_defaulted)
        note(declaration.location,
             "Nothing says what `" + declaration.name + "` holds; it is " +
                 describe_in_english(type) + ".");
      else if (variable.has_initial_value && natural.is_known() &&
               natural != type)
        note(declaration.location,
             "`" + declaration.name + "` holds " + describe_in_english(type) +
                 ", because it is given one later on.");
      declaration.declared_type = type;
    }

    for (auto &list : lists) {
      unit_index = list.unit_index;
      auto element = types.resolve(list.element);
      if (element == value_type::nothing() || element == value_type::pointer())
        continue; // Not a valid list; the checker explains why.
      if (element.is_record()) {
        std::vector<value_type> kinds;
        for (const auto item : list.items)
          kinds.push_back(types.resolve(item));
        if (std::ranges::any_of(kinds, [&](const auto &p_kind) {
              return p_kind != element;
            })) {
          const auto shared = common_traits(kinds);
          // With several, the first: its value can still be asked for
          // the others' phrases (the checker allows it).
          if (!shared.empty()) {
            element = value_type::record_of(shared.front());
            note(list.list->location,
                 "This list holds " + describe_in_english(element) +
                     "s: its items are all of them.");
          }
        }
      }
      if (!element.is_known()) {
        note(list.list->location,
             "Nothing says what this list holds; it holds whole numbers.");
        element = value_type::integer();
      }
      list.list->type_argument = element;
    }

    // A default method's types were worked out like any function's.
    for (auto &unit : target.units)
      for (auto &trait : unit.traits)
        for (auto &method : trait.methods)
          if (method.has_default)
            if (const auto found = functions.find(method.name);
                found != functions.end()) {
              for (std::size_t index = 0;
                   index < std::min(method.parameters.size(),
                                    found->second.declaration->parameters.size());
                   ++index)
                method.parameters[index].type =
                    found->second.declaration->parameters[index].type;
              method.return_type = found->second.declaration->return_type;
            }

    for (auto &condition : conditions) {
      if (types.resolve(condition.slot) != value_type::integer())
        continue;
      unit_index = condition.unit_index;
      note(condition.condition->location,
           "A number used as a yes-or-no condition means \"is not zero\".");
      expression wrapped{.kind = expression::kind_type::call,
                         .text = "!=",
                         .location = condition.condition->location};
      wrapped.arguments.push_back(std::move(*condition.condition));
      wrapped.arguments.push_back({.kind = expression::kind_type::integer,
                                   .location = wrapped.location});
      *condition.condition = std::move(wrapped);
    }
  }

  // The types a trait fixes are given to the functions that provide its
  // methods before anything is inferred: an impl's method takes the
  // record as its value and the trait's other types.
  auto pin_trait_types() -> void {
    std::map<string_type, function *> by_name;
    for (auto &unit : target.units) {
      for (auto &trait : unit.traits)
        for (const auto &method : trait.methods)
          method_owner.emplace(method.name, &trait);
      for (auto &function : unit.functions)
        by_name.emplace(function.name, &function);
      for (const auto &impl : unit.impls)
        implemented.emplace(impl.trait, impl.type);
    }
    for (const auto &unit : target.units) {
      for (const auto &impl : unit.impls) {
        const auto *const owner = find_trait(target, impl.trait);
        if (owner == nullptr)
          continue;
        for (const auto &name : impl.methods) {
          const auto *const method = owner->find(name);
          const auto found = by_name.find(impl_function_name(name, impl.type));
          if (method == nullptr || method->has_default ||
              found == by_name.end())
            continue;
          auto &function = *found->second;
          for (std::size_t index = 0;
               index < std::min(function.parameters.size(),
                                method->parameters.size());
               ++index)
            if (!function.parameters[index].type.is_known())
              function.parameters[index].type =
                  index == 0 ? value_type::record_of(impl.type)
                             : method->parameters[index].type;
          if (!function.return_type.is_known())
            function.return_type = method->return_type;
        }
      }
    }
  }

  // An impl's version of a default method has the default's types.
  auto join_overrides() -> void {
    for (const auto &unit : target.units) {
      for (const auto &impl : unit.impls) {
        const auto *const owner = find_trait(target, impl.trait);
        if (owner == nullptr)
          continue;
        for (const auto &name : impl.methods) {
          const auto *const method = owner->find(name);
          const auto written = functions.find(impl_function_name(name, impl.type));
          const auto base = functions.find(name);
          if (method == nullptr || !method->has_default ||
              written == functions.end() || base == functions.end())
            continue;
          const auto count = std::min(written->second.parameters.size(),
                                      base->second.parameters.size());
          for (std::size_t index = 1; index < count; ++index)
            types.unify(written->second.parameters[index],
                        base->second.parameters[index]);
          types.unify(written->second.result, base->second.result);
        }
      }
    }
  }

  // Records that implement one trait in common: what a list of different
  // records can be a list of.
  auto common_traits(const std::vector<value_type> &p_kinds) const
      -> std::vector<string_type> {
    std::vector<string_type> result;
    for (const auto &[name, owner] : method_owner) {
      (void)name;
      if (std::ranges::find(result, owner->name) != result.end())
        continue;
      const bool all = std::ranges::all_of(p_kinds, [&](const auto &p_kind) {
        return p_kind.is_record() &&
               (p_kind.record == owner->name ||
                implemented.contains({owner->name, p_kind.record}));
      });
      if (all)
        result.push_back(owner->name);
    }
    std::ranges::sort(result);
    return result;
  }

public:
  explicit elaborator(program &p_program) : target(p_program) {}

  auto run() -> std::vector<diagnostic> {
    for (const auto &function : libc_functions())
      c_functions[function.name] = &function;
    for (unit_index = 0; unit_index < target.units.size(); ++unit_index)
      for (const auto &function : target.units[unit_index].externs)
        c_functions[function.name] = &function;

    for (unit_index = 0; unit_index < target.units.size(); ++unit_index) {
      for (auto &record : target.units[unit_index].records) {
        for (auto &field : record.fields) {
          const auto slot = types.of(field.type, true);
          types.add_record_field(record.name, field.name, slot);
          fields.push_back(
              {&field, record.name, slot, record.location, unit_index});
        }
      }
    }
    pin_trait_types();
    for (unit_index = 0; unit_index < target.units.size(); ++unit_index) {
      for (auto &function : target.units[unit_index].functions) {
        function_info info{&function, unit_index};
        for (const auto &parameter : function.parameters)
          info.parameters.push_back(types.of(parameter.type, true));
        info.result = types.of(function.return_type, true);
        functions.emplace(function.name, std::move(info));
      }
    }
    join_overrides();
    for (unit_index = 0; unit_index < target.units.size(); ++unit_index)
      for (auto &function : target.units[unit_index].functions)
        add_implicit_return(function);

    scopes.emplace_back();
    for (unit_index = 0; unit_index < target.units.size(); ++unit_index)
      for (auto &statement : target.units[unit_index].statements)
        walk(statement);
    scopes.pop_back();

    for (auto &[name, info] : functions) {
      unit_index = info.unit_index;
      current_function = &info;
      scopes.emplace_back();
      for (std::size_t index = 0; index < info.parameters.size(); ++index)
        scopes.back()[info.declaration->parameters[index].name] =
            info.parameters[index];
      for (auto &statement : info.declaration->body)
        walk(statement);
      scopes.pop_back();
      current_function = nullptr;
    }

    types.solve();
    std::vector<std::pair<inference::trait_hint, string_type>> unclear;
    while (true) {
      unclear.clear();
      if (!types.settle_hints(unclear))
        break;
      types.solve();
    }
    for (const auto &[hint, names] : unclear)
      problems.push_back(
          {hint.unit_index, hint.location,
           "The phrases used on this value belong to the traits '" + names +
               "', so Ovis can't tell which one it is: write its type."});
    write_back();
    std::ranges::stable_sort(notes, [](const auto &p_left, const auto &p_right) {
      return std::pair(p_left.unit_index, p_left.location.line) <
             std::pair(p_right.unit_index, p_right.location.line);
    });
    return std::move(notes);
  }

  auto take_problems() -> std::vector<diagnostic> {
    return std::move(problems);
  }
};

} // namespace

auto closest_name(string_view_type p_name,
                  const std::vector<string_type> &p_candidates)
    -> std::optional<string_type> {
  const auto key = loose(p_name);
  if (key.empty())
    return std::nullopt;
  const auto limit = key.size() <= 3 ? 0 : key.size() <= 6 ? 1 : 2;

  std::optional<string_type> best;
  std::size_t best_distance = SIZE_MAX;
  bool is_unique = false;
  for (const auto &candidate : p_candidates) {
    if (candidate == p_name)
      continue;
    const auto distance = edit_distance(key, loose(candidate));
    if (distance > static_cast<std::size_t>(limit) &&
        !(distance == 0 && limit == 0))
      continue;
    if (distance < best_distance) {
      best = candidate;
      best_distance = distance;
      is_unique = true;
    } else if (distance == best_distance) {
      is_unique = false;
    }
  }
  return is_unique ? best : std::nullopt;
}

auto elaborate(program &p_program, std::vector<diagnostic> *p_problems)
    -> std::vector<diagnostic> {
  elaborator work(p_program);
  auto notes = work.run();
  if (p_problems != nullptr)
    *p_problems = work.take_problems();
  return notes;
}

} // namespace ovis::core
