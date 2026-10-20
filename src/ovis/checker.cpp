#include <algorithm>
#include <cctype>
#include <deque>
#include <functional>
#include <map>
#include <set>

#include <ovis/core/c_import.hpp>
#include <ovis/core/checker.hpp>
#include <ovis/core/monomorphise.hpp>

namespace ovis::core {

namespace {

struct check_failure {
  source_location location;
  string_type message;
};

auto join(const std::vector<string_type> &p_parts) -> string_type {
  string_type result;
  for (const auto &part : p_parts) {
    if (!result.empty())
      result += ", ";
    result += part;
  }
  return result.empty() ? "none" : result;
}

auto is_assignable(const value_type &p_from, const value_type &p_to) -> bool {
  // A phrase only fits a phrase of its convention and signature, or `void *`.
  if (p_from.is_phrase() || p_to.is_phrase())
    return p_from == p_to ||
           (p_from.is_phrase() && p_to == value_type::pointer());
  if (p_from.is_typed_pointer() && !p_to.is_list &&
      p_to.base == base_type::pointer)
    // Any pointer is C's `void *`; one, many and C pointers to the same
    // thing go where each other goes (C takes `T *` for all of them).
    return p_to.kind == pointer_kind::raw || p_to.pointee == p_from.pointee;
  // A record goes where one that may be nothing does, not the other way.
  if (p_from.base == base_type::record && p_to.base == base_type::record &&
      p_from.nullable != p_to.nullable)
    return p_to.nullable && p_from.or_nothing() == p_to;
  // A whole number goes where any number does, a decimal where a decimal
  // does; one that doesn't fit is an error when the program runs
  // (numbers.md §3 rule 5).
  if (p_from.is_numeric() && p_to.is_numeric())
    return p_to.is_decimal() || p_from.is_whole();
  return p_from == p_to;
}

// The operations a number written in the program can be part of and still
// take the type of what it meets: `b + 2 * 3` is a byte.
auto is_literal_number(const expression &p_expression) -> bool {
  using kind = expression::kind_type;
  if (p_expression.kind == kind::integer || p_expression.kind == kind::real)
    return true;
  static const std::set<string_view_type> operations = {
      "+", "-", "*", "%", "div", "min", "max", "abs"};
  return p_expression.kind == kind::call &&
         operations.contains(p_expression.text) &&
         !p_expression.arguments.empty() &&
         std::ranges::all_of(p_expression.arguments, is_literal_number);
}

// Whether a value can be joined to text.
auto is_printable(const value_type &p_type) -> bool {
  return !p_type.is_list && p_type.base != base_type::nothing &&
         p_type.base != base_type::pointer &&
         p_type.base != base_type::memory &&
         p_type.base != base_type::record && p_type.is_known();
}

// Names in quotes, so reports can put each in the author's words.
auto quoted(const std::vector<string_type> &p_parts) -> string_type {
  std::vector<string_type> result;
  for (const auto &part : p_parts)
    result.push_back("'" + part + "'");
  return join(result);
}

class checker {
  struct function_entry {
    function *declaration;
    std::size_t unit_index;
  };

  program &target;
  std::vector<diagnostic> diagnostics;
  std::vector<diagnostic> *notes;
  std::map<string_type, const c_function *> c_functions;
  std::map<string_type, function_entry> functions;
  std::map<string_type, const record *> records;
  std::map<string_type, const trait *> traits;
  // `TRAIT\nTYPE` → the impl; and each trait method → its trait.
  std::map<string_type, const impl *> impls;
  std::map<string_type, const trait *> method_owner;
  std::map<string_type, value_type> globals;
  std::vector<std::map<string_type, value_type>> scopes;
  std::size_t unit_index = 0;
  std::size_t loop_depth = 0;
  // How many deferred blocks the statement being checked is inside, and the
  // loop depth where the innermost began.
  std::size_t defer_depth = 0;
  std::size_t defer_loop_floor = 0;
  // The blocks around the statement being checked, innermost last: each
  // one's label, the type it gives (if it gives one) and how many `at the
  // end` blocks were open around it.
  struct block_frame {
    string_type label;
    std::optional<value_type> gives;
    std::size_t defer_depth;
  };
  std::vector<block_frame> block_frames;
  // Unset while checking top-level statements.
  const function *current_function = nullptr;
  // Copies of functions for lists of numbers of a size (numbers.md §5),
  // and their units: added to the units once checking is done, as the
  // units' function lists can't grow while `functions` points into them.
  std::deque<function> copies;
  std::vector<std::size_t> copy_units;
  std::size_t copies_checked = 0;
  // Generic functions as the elaborator left them, which their copies are
  // made from (docs/proposals/generics.md §6), and their units.
  std::map<string_type, std::pair<function, std::size_t>> generic_sources;

  // The traits a type variable of the function being checked has.
  auto bounds_of(const value_type &p_type) const
      -> const std::vector<string_type> * {
    if (!p_type.is_variable() || current_function == nullptr)
      return nullptr;
    for (const auto &parameter : current_function->type_parameters)
      if (parameter.name == p_type.record)
        return &parameter.bounds;
    return nullptr;
  }

  static auto is_builtin_trait(string_view_type p_trait) -> bool {
    return p_trait == "comparable" || p_trait == "equatable" ||
           p_trait == "showable" || p_trait == "number" || p_trait == "num";
  }

  // Whether a type has the trait a bound asks for (§3): numbers and text
  // are comparable and equatable, a record by the phrases its author wrote
  // (`comes_before__book`) or its impls; a type variable by its bounds.
  auto meets(const value_type &p_type, const string_type &p_trait) const
      -> bool {
    if (p_type.is_variable()) {
      const auto *const bounds = bounds_of(p_type);
      if (bounds == nullptr)
        return false;
      if (std::ranges::find(*bounds, p_trait) != bounds->end())
        return true;
      // `num` and `number` are one; a trait requiring another has it.
      return std::ranges::any_of(*bounds, [&](const string_type &p_bound) {
        if ((p_bound == "num" && p_trait == "number") ||
            (p_bound == "number" && p_trait == "num"))
          return true;
        // A number is comparable, equatable and showable.
        if ((p_bound == "num" || p_bound == "number") &&
            (p_trait == "comparable" || p_trait == "equatable" ||
             p_trait == "showable"))
          return true;
        const auto found = traits.find(p_bound);
        return found != traits.end() &&
               std::ranges::find(found->second->requires_traits, p_trait) !=
                   found->second->requires_traits.end();
      });
    }
    if (p_trait == "number" || p_trait == "num")
      return p_type.is_numeric();
    if (p_trait == "showable")
      return p_type.is_known() && !p_type.is_pointer() &&
             p_type != value_type::memory() && p_type != value_type::nothing();
    const auto has_phrase = [&](string_view_type p_phrase) {
      return p_type.is_record() &&
             (functions.contains(string_type(p_phrase) + "__" + p_type.record) ||
              generic_impl_function(p_phrase, p_type));
    };
    if (p_trait == "comparable")
      return p_type.is_numeric() || p_type == value_type::string() ||
             has_phrase("comes_before");
    if (p_trait == "equatable")
      return p_type.is_numeric() || p_type == value_type::string() ||
             p_type == value_type::boolean() || has_phrase("is_same_as");
    if (!traits.contains(p_trait))
      return false;
    if (!p_type.is_record())
      return p_type.is_known() && impls.contains(impl_key(p_trait, p_type.to_string()));
    return p_type.record == p_trait || implements(p_type, p_trait);
  }

  // A generic record's impl phrase for every use (G2): whether there is one.
  auto generic_impl_function(string_view_type p_phrase,
                             const value_type &p_type) const -> bool {
    return !p_type.arguments.empty() &&
           generic_sources.contains(impl_function_name(p_phrase, p_type.record));
  }

  // What to write so a type has a trait it lacks.
  static auto bound_hint(const value_type &p_type, const string_type &p_trait)
      -> string_type {
    if (!p_type.is_record())
      return "";
    const auto record = describe_in_english(p_type);
    const auto name = record.substr(record.find(' ') + 1);
    if (p_trait == "comparable")
      return " Say how two compare: `to (a: " + name + ") comes before (b: " +
             name + "):`.";
    if (p_trait == "equatable")
      return " Say when two are the same: `to (a: " + name +
             ") is the same as (b: " + name + "):`.";
    if (p_trait == "showable")
      return "";
    return " Write `" + record + " is " +
           describe_in_english(value_type::record_of(p_trait)) + ":`.";
  }

  // `p_type`'s values may be used as `p_trait` asks, or the build stops.
  auto expect_trait(const value_type &p_type, const string_type &p_trait,
                    source_location p_location, string_view_type p_what)
      -> void {
    if (meets(p_type, p_trait))
      return;
    string_type trait = p_trait == "num" ? "number" : p_trait;
    if (p_type.is_variable())
      fail(p_location, string_type(p_what) + " needs " +
                           describe_in_english(p_type) + " to be " + trait +
                           ": write `any " + trait + " " + p_type.record +
                           "` where it is first named.");
    fail(p_location, string_type(p_what) + " needs " + trait + " values, but " +
                         describe_in_english(p_type) + " isn't one." +
                         bound_hint(p_type, p_trait));
  }

  // A call of a generic function (§3, §6): its type variables are what
  // the elaborator found here (or the arguments say); the arguments must
  // fit and meet the bounds. With concrete types it calls the copy made
  // for them (`push__int`), made once.
  auto check_generic_call(expression &p_call, const function_entry &p_entry)
      -> value_type {
    const auto &callee = *p_entry.declaration;
    check_count(p_call, callee.parameters.size(), callee.parameters.size());
    type_bindings bindings;
    const auto is_complete = [](const value_type &p_type) {
      return p_type.is_known() &&
             p_type.to_string().find('?') == string_type::npos;
    };
    for (std::size_t index = 0; index < callee.type_parameters.size() &&
                                index < p_call.type_arguments.size();
         ++index)
      if (is_complete(p_call.type_arguments[index]))
        bindings[callee.type_parameters[index].name] =
            p_call.type_arguments[index];
    for (std::size_t index = 0; index < p_call.arguments.size(); ++index)
      match_type(callee.parameters[index].type, p_call.arguments[index].type,
                 bindings);
    std::vector<value_type> types;
    for (const auto &parameter : callee.type_parameters) {
      const auto found = bindings.find(parameter.name);
      if (found == bindings.end())
        fail(p_call.location,
             "Ovis can't tell what `" + parameter.name + "` is in this use of '" +
                 callee.name + "': say what it is, as in `an empty list of "
                               "whole numbers`, or give a variable a type.");
      types.push_back(found->second);
    }
    for (std::size_t index = 0; index < p_call.arguments.size(); ++index) {
      const auto type = substitute(callee.parameters[index].type, bindings);
      const auto what = "Argument '" + callee.parameters[index].name +
                        "' of '" + callee.name + "'";
      expect(p_call.arguments[index], type, what);
    }
    for (std::size_t index = 0; index < types.size(); ++index)
      for (const auto &bound : callee.type_parameters[index].bounds)
        expect_trait(types[index], bound, p_call.location,
                     "'" + callee.name + "'");
    if (std::ranges::none_of(types, &value_type::has_variables)) {
      p_call.text = make_copy(callee.name, types, bindings);
      p_call.type_arguments.clear();
    } else {
      p_call.type_arguments = types;
    }
    return substitute(callee.return_type, bindings);
  }

  // The copy of a generic function for these types, made once (§6); it
  // is checked after the functions written.
  auto make_copy(const string_type &p_generic,
                 const std::vector<value_type> &p_types,
                 const type_bindings &p_bindings) -> string_type {
    auto name = copy_name(p_generic, p_types);
    if (functions.contains(name))
      return name;
    const auto &[source, unit] = generic_sources.at(p_generic);
    auto &copy = copies.emplace_back(source);
    substitute(copy, p_bindings);
    copy.name = name;
    copy.type_parameters.clear();
    copy.copy_of = p_generic;
    copy.copied_for = p_types;
    copy_units.push_back(unit);
    functions.emplace(name, function_entry{&copy, unit});
    return name;
  }

  auto in_copy() const -> bool {
    return current_function != nullptr && !current_function->copy_of.empty();
  }

  // A record compared: the phrases its author wrote, which a generic's
  // bounds ask for (`(< a b)` is `(comes_before__book a b)`); without them
  // `=` is the same record and `<` an error.
  auto lower_record_comparison(expression &p_call) -> bool {
    if (p_call.arguments.size() != 2 || is_null(p_call.arguments[0]) ||
        is_null(p_call.arguments[1]))
      return false;
    const auto &left = p_call.arguments[0].type;
    const auto &right = p_call.arguments[1].type;
    if (!left.is_record() || left.without_nothing() != right.without_nothing())
      return false;
    const auto &name = p_call.text;
    const bool is_equality = name == "=" || name == "!=";
    const auto function =
        (is_equality ? "is_same_as__" : "comes_before__") + left.record;
    if (!functions.contains(function))
      return false;
    const bool swapped = name == ">" || name == "<=";
    const bool negated = name == "!=" || name == "<=" || name == ">=";
    expression call{.kind = expression::kind_type::call,
                    .text = function,
                    .location = p_call.location};
    call.arguments.push_back(std::move(p_call.arguments[swapped ? 1 : 0]));
    call.arguments.push_back(std::move(p_call.arguments[swapped ? 0 : 1]));
    if (negated) {
      p_call.text = "not";
      p_call.arguments.clear();
      p_call.arguments.push_back(std::move(call));
    } else {
      p_call = std::move(call);
    }
    return true;
  }

  // In a copy, a record joined to text where its type variable was: the
  // `text of` its author wrote.
  auto show_records_in_copy(expression &p_call) -> void {
    if (!in_copy())
      return;
    for (auto &argument : p_call.arguments) {
      if (!argument.type.is_record() ||
          !functions.contains("text_of__" + argument.type.record))
        continue;
      expression call{.kind = expression::kind_type::call,
                      .text = "text_of__" + argument.type.record,
                      .location = argument.location};
      call.arguments.push_back(std::move(argument));
      argument = std::move(call);
      check_expression(argument);
    }
  }

  // In a copy, a record shown where its type variable was: by the `text
  // of` its author wrote, as the phrases show one (a list of them `[a,
  // b]`).
  auto show_in_copy(expression &p_value) -> void {
    if (!in_copy())
      return;
    const auto &type = p_value.type;
    if (type.is_record() && functions.contains("text_of__" + type.record)) {
      expression call{.kind = expression::kind_type::call,
                      .text = "text_of__" + type.record,
                      .location = p_value.location};
      call.arguments.push_back(std::move(p_value));
      p_value = std::move(call);
      check_expression(p_value);
    } else if (type.is_list && type.inner_lists == 0 &&
               type.element().is_record() &&
               functions.contains("text_of__" + type.record) &&
               functions.contains("prelude_list_text")) {
      expression call{.kind = expression::kind_type::call,
                      .text = "prelude_list_text",
                      .location = p_value.location};
      call.arguments.push_back(std::move(p_value));
      p_value = std::move(call);
      check_expression(p_value);
    }
  }

  // Arithmetic on a type variable's values (§3): the same variable, which
  // must be a number; numbers written in the program take its type.
  auto check_variable_arithmetic(expression &p_call) -> std::optional<value_type> {
    const auto variable = std::ranges::find_if(
        p_call.arguments,
        [](const expression &p_argument) { return p_argument.type.is_variable(); });
    if (variable == p_call.arguments.end())
      return std::nullopt;
    const auto type = variable->type;
    for (const auto &argument : p_call.arguments)
      if (argument.type != type && !is_literal_number(argument))
        fail(argument.location, "'" + p_call.text + "' can't mix " +
                                    describe_in_english(type) + " and " +
                                    describe_in_english(argument.type) + ".");
    expect_trait(type, "number", p_call.location, "'" + p_call.text + "'");
    return type;
  }

  [[noreturn]] static auto fail(source_location p_location,
                                string_type p_message) -> void {
    throw check_failure{p_location, std::move(p_message)};
  }

  auto report(source_location p_location, string_type p_message) -> void {
    diagnostics.push_back({unit_index, p_location, std::move(p_message)});
  }

  static auto impl_key(string_view_type p_trait, string_view_type p_type)
      -> string_type {
    return string_type(p_trait) + "\n" + string_type(p_type);
  }

  // Whether the record implements the trait: an impl for it, or for every
  // use of its generic record whose bounds this use's types meet.
  auto implements(const value_type &p_record, const string_type &p_trait) const
      -> bool {
    const auto found = impls.find(impl_key(p_trait, p_record.record));
    if (found == impls.end())
      return false;
    return impl_bounds_met(*found->second, p_record);
  }

  auto impl_bounds_met(const impl &p_impl, const value_type &p_record) const
      -> bool {
    if (p_impl.type_parameters.empty())
      return true;
    const auto arguments = p_record.record_arguments();
    for (std::size_t index = 0;
         index < std::min(arguments.size(), p_impl.type_parameters.size());
         ++index)
      for (const auto &bound : p_impl.type_parameters[index].bounds)
        if (!meets(arguments[index], bound))
          return false;
    return true;
  }

  // What the type variables of a generic record stand for in this use.
  auto record_bindings(const record &p_record, const value_type &p_use) const
      -> type_bindings {
    type_bindings result;
    const auto arguments = p_use.record_arguments();
    for (std::size_t index = 0;
         index < std::min(arguments.size(), p_record.type_parameters.size());
         ++index)
      result[p_record.type_parameters[index]] = arguments[index];
    return result;
  }

  // A field's type in this use of its record (`items` of a stack of whole
  // numbers is a list of whole numbers).
  auto field_type(const record &p_record, const record_field &p_field,
                  const value_type &p_use) const -> value_type {
    if (p_record.type_parameters.empty())
      return p_field.type;
    return substitute(p_field.type, record_bindings(p_record, p_use));
  }

  // A value of a trait is any record that implements it; they share one
  // representation, so no conversion is needed.
  auto can_assign(const value_type &p_from, const value_type &p_to) const
      -> bool {
    if (is_assignable(p_from, p_to))
      return true;
    if (p_from.is_list && p_to.is_list &&
        p_from.element().is_record() && p_to.element().is_record())
      return can_assign(p_from.element(), p_to.element());
    if (!p_from.is_record() || !p_to.is_record() ||
        !traits.contains(p_to.record) || (p_from.nullable && !p_to.nullable))
      return false;
    if (implements(p_from, p_to.record))
      return true;
    // A trait's value for another trait when a record has both: the first
    // trait of a list of such records stands for it, and the phrases of the
    // other are checked when the program runs.
    return traits.contains(p_from.record) &&
           std::ranges::any_of(impls, [&](const auto &p_impl) {
             return p_impl.second->trait == p_to.record &&
                    impls.contains(
                        impl_key(p_from.record, p_impl.second->type));
           });
  }

  auto at_top_level() const -> bool {
    return current_function == nullptr && scopes.size() == 1;
  }

  auto lookup(const string_type &p_name) const -> const value_type * {
    for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope)
      if (const auto found = scope->find(p_name); found != scope->end())
        return &found->second;
    if (const auto found = globals.find(p_name); found != globals.end())
      return &found->second;
    return nullptr;
  }

  auto known_variables() const -> string_type {
    std::set<string_type> names;
    for (const auto &scope : scopes)
      for (const auto &[name, type] : scope)
        names.insert(name + " (" + describe_in_english(type) + ")");
    for (const auto &[name, type] : globals)
      names.insert(name + " (" + describe_in_english(type) + ")");
    return join({names.begin(), names.end()});
  }

  auto known_functions() const -> string_type {
    std::vector<string_type> names;
    for (const auto &[name, entry] : functions)
      names.push_back(name);
    return join(names);
  }

  auto declare(const string_type &p_name, value_type p_type) -> void {
    if (at_top_level())
      globals[p_name] = p_type;
    else
      scopes.back()[p_name] = p_type;
  }

  auto check_count(const expression &p_call, std::size_t p_minimum,
                   std::size_t p_maximum) -> void {
    const auto count = p_call.arguments.size();
    if (count >= p_minimum && count <= p_maximum)
      return;
    string_type expected =
        p_minimum == p_maximum ? std::to_string(p_minimum)
        : p_maximum == SIZE_MAX
            ? "at least " + std::to_string(p_minimum)
            : std::to_string(p_minimum) + " to " + std::to_string(p_maximum);
    fail(p_call.location, "'" + p_call.text + "' takes " + expected +
                              " argument(s) but got " + std::to_string(count) +
                              ".");
  }

  // `null` written as a value: the empty record, where a record goes.
  auto is_null(const expression &p_expression) -> bool {
    return p_expression.kind == expression::kind_type::variable &&
           p_expression.text == "null" && lookup(p_expression.text) == nullptr &&
           p_expression.type == value_type::pointer();
  }

  // A number written in the program takes the type it is put in, if it
  // fits (numbers.md §3 rule 2); `p_must_fit` makes one that doesn't an
  // error, else it keeps its own type.
  auto give_number_type(expression &p_number, const value_type &p_type,
                        bool p_must_fit) -> bool {
    using kind = expression::kind_type;
    if (!p_type.is_numeric() || !is_literal_number(p_number) ||
        (p_number.type.is_decimal() && p_type.is_whole()))
      return false;
    if (!literal_fits(p_number, p_type)) {
      if (!p_must_fit)
        return false;
      fail(p_number.location,
           literal_text(p_number) + " doesn't fit in " +
               describe_in_english(p_type) + " (" + number_range(p_type) + ").");
    }
    const std::function<void(expression &)> retype = [&](expression &p_part) {
      p_part.type = p_type;
      if (p_part.kind == kind::call)
        for (auto &argument : p_part.arguments)
          retype(argument);
    };
    retype(p_number);
    return true;
  }

  // Whether each number written in it fits (`-5` as negative five).
  static auto literal_fits(const expression &p_number, const value_type &p_type)
      -> bool {
    using kind = expression::kind_type;
    if (p_number.kind == kind::integer)
      return fits_number(p_number.integer, p_number.is_big, p_type);
    if (p_number.kind == kind::real)
      return p_type.is_decimal();
    if (p_number.text == "-" && p_number.arguments.size() == 1 &&
        p_number.arguments[0].kind == kind::integer && !p_number.arguments[0].is_big)
      return fits_number(-p_number.arguments[0].integer, false, p_type);
    return std::ranges::all_of(p_number.arguments, [&](const expression &p_part) {
      return literal_fits(p_part, p_type);
    });
  }

  static auto literal_text(const expression &p_number) -> string_type {
    using kind = expression::kind_type;
    if (p_number.kind == kind::integer)
      return p_number.is_big
                 ? std::to_string(static_cast<max_uint_type>(p_number.integer))
                 : std::to_string(p_number.integer);
    if (p_number.text == "-" && p_number.arguments.size() == 1 &&
        p_number.arguments[0].kind == kind::integer)
      return "-" + literal_text(p_number.arguments[0]);
    return "This number";
  }

  // The type numbers are worked out in together (numbers.md §3): the
  // common type of those that aren't written in the program, which those
  // that are take on.
  auto unify_numbers(expression &p_call, std::vector<expression> &p_numbers,
                     bool p_must_fit) -> value_type {
    std::optional<value_type> result;
    bool has_decimal_literal = false;
    bool has_big_literal = false;
    for (auto &number : p_numbers) {
      if (!number.type.is_numeric()) {
        string_type hint;
        if (p_call.text == "+" && number.type == value_type::string())
          hint = " To show several values, separate them with commas.";
        fail(number.location, "'" + p_call.text + "' needs numbers but got " +
                                  describe_in_english(number.type) + "." + hint);
      }
      if (is_literal_number(number)) {
        has_decimal_literal = has_decimal_literal || number.type.is_decimal();
        has_big_literal = has_big_literal ||
                          (number.kind == expression::kind_type::integer &&
                           number.is_big);
        continue;
      }
      if (!result) {
        result = number.type;
        continue;
      }
      const auto common = common_number(*result, number.type);
      if (!common)
        fail(p_call.location,
             "'" + p_call.text + "' can't mix " + describe_in_english(*result) +
                 " and " + describe_in_english(number.type) +
                 ": no type holds both. Say which with `as a whole number` "
                 "or `as a uint64`.");
      result = *common;
    }
    if (!result)
      result = has_decimal_literal ? value_type::real()
               : has_big_literal   ? value_type::number(c_type::size_type)
                                   : value_type::integer();
    else if (has_decimal_literal && result->is_whole())
      result = value_type::real();
    // Those that don't fit keep their own type, and widen the rest.
    for (auto &number : p_numbers)
      if (is_literal_number(number) &&
          !give_number_type(number, *result, p_must_fit)) {
        const auto common = common_number(*result, number.type);
        if (!common)
          fail(number.location, literal_text(number) + " doesn't fit in " +
                                    describe_in_english(*result) + ".");
        result = *common;
      }
    return *result;
  }

  auto expect(expression &p_expression, const value_type &p_type,
              string_view_type p_what) -> void {
    // A number written in the program where a type variable's value goes
    // is one of them (`sum = 0` of numbers): in each copy it takes that
    // number's type. The variable must be a number.
    if (p_type.is_variable() && is_literal_number(p_expression)) {
      expect_trait(p_type, "number", p_expression.location, p_what);
      const std::function<void(expression &)> retype = [&](expression &p_part) {
        p_part.type = p_type;
        for (auto &argument : p_part.arguments)
          retype(argument);
      };
      retype(p_expression);
      return;
    }
    give_number_type(p_expression, p_type, true);
    // Said as what it is, not as a type mismatch with "nothing".
    if (p_expression.type == value_type::nothing() &&
        p_expression.kind == expression::kind_type::call &&
        p_type != value_type::nothing())
      fail(p_expression.location, "'" + p_expression.text +
                                      "' gives back nothing, so it has no "
                                      "value to use here.");
    // A list written out holds whatever its items can be taken as: a list
    // of circles and squares can be a list of shapes.
    const bool is_list_literal =
        p_expression.kind == expression::kind_type::call &&
        p_expression.text == "list" && p_expression.type.is_list &&
        p_type.is_list &&
        can_assign(p_expression.type.element(), p_type.element());
    if (p_type.is_record() && is_null(p_expression) && !may_be_nothing(p_type))
      fail(p_expression.location,
           string_type(p_what) + " is " + describe_in_english(p_type) +
               ", which is never nothing. Say it may be: " +
               describe_in_english(p_type.or_nothing()) + ".");
    if (!can_assign(p_expression.type, p_type) && !is_list_literal &&
        !(p_type.is_record() && is_null(p_expression)))
      fail(p_expression.location, string_type(p_what) + " must be " +
                                      describe_in_english(p_type) + " but is " +
                                      describe_in_english(p_expression.type) + ".");
  }

  // Whether a record type can hold nothing: `or nothing`, or a C struct
  // (what C gives back is a C pointer, checked when the program runs).
  auto may_be_nothing(const value_type &p_type) const -> bool {
    if (!p_type.is_record() || p_type.nullable)
      return true;
    const auto found = records.find(p_type.record);
    return found != records.end() && found->second->is_c;
  }

  // Narrowing (references.md §3): the variables known not to be nothing
  // here, and those a condition makes so when it is true (or false).
  std::set<string_type> narrowed;

  auto narrowing(const expression &p_condition, bool p_when_true) const
      -> std::set<string_type> {
    std::set<string_type> result;
    if (p_condition.kind != expression::kind_type::call)
      return result;
    const auto &name = p_condition.text;
    const auto &arguments = p_condition.arguments;
    if ((name == "=" || name == "!=") && arguments.size() == 2) {
      // `x != null` true, or `x = null` false.
      if ((name == "!=") != p_when_true)
        return result;
      for (std::size_t index = 0; index < 2; ++index)
        if (arguments[index].kind == expression::kind_type::variable &&
            arguments[1 - index].kind == expression::kind_type::variable &&
            arguments[1 - index].text == "null")
          result.insert(arguments[index].text);
      return result;
    }
    if (name == "not" && arguments.size() == 1)
      return narrowing(arguments[0], !p_when_true);
    // Both true, or (for `or`) both false.
    if ((name == "and" && p_when_true) || (name == "or" && !p_when_true))
      for (const auto &argument : arguments) {
        const auto part = narrowing(argument, p_when_true);
        result.insert(part.begin(), part.end());
      }
    return result;
  }

  // Whether a block always leaves (`give back`, `stop`, `skip`, `leave`, or
  // an `if` whose branches both do).
  static auto always_leaves(const std::vector<statement> &p_block) -> bool {
    if (p_block.empty())
      return false;
    using kind = statement::kind_type;
    const auto &last = p_block.back();
    if (last.kind == kind::return_ || last.kind == kind::break_ ||
        last.kind == kind::continue_ || last.kind == kind::leave)
      return true;
    return last.kind == kind::if_ && always_leaves(last.body) &&
           always_leaves(last.else_body);
  }

  // The variables a block sets anywhere in it.
  static auto assigned_in(const std::vector<statement> &p_block,
                          std::set<string_type> &p_names) -> void {
    for (const auto &statement : p_block) {
      if (statement.kind == statement::kind_type::set ||
          statement.kind == statement::kind_type::let ||
          (statement.kind == statement::kind_type::block &&
           !statement.name.empty()))
        p_names.insert(statement.name);
      assigned_in(statement.body, p_names);
      assigned_in(statement.else_body, p_names);
    }
  }

  // After `x = value`: narrowed if the value is never nothing.
  auto narrow_after_set(const string_type &p_name, const value_type &p_value)
      -> void {
    if (p_value.is_record() && !p_value.nullable)
      narrowed.insert(p_name);
    else
      narrowed.erase(p_name);
  }

  // A record type names a record the program declares.
  auto type_exists(const value_type &p_type) const -> bool {
    return p_type.base != base_type::record ||
           records.contains(p_type.record) || traits.contains(p_type.record);
  }

  auto check_type_exists(const value_type &p_type, source_location p_location)
      -> void {
    if (!type_exists(p_type))
      fail(p_location, "There is no record called '" + p_type.record + "'.");
  }

  // Only records have fields and can be made: not a trait.
  auto check_record_exists(const value_type &p_type, source_location p_location)
      -> void {
    check_type_exists(p_type, p_location);
    if (traits.contains(p_type.record))
      fail(p_location, "'" + p_type.record +
                           "' is a trait, not a record: it has no fields and "
                           "can't be made.");
  }

  // A value that may be nothing can't be used as a record until checked.
  auto check_not_nothing(const expression &p_object, string_view_type p_what)
      -> void {
    if (p_object.type.nullable)
      fail(p_object.location,
           string_type(p_what) + " needs a record, but this may be nothing "
                                 "here. Check it first: `if " +
               (p_object.kind == expression::kind_type::variable
                    ? p_object.text
                    : string_type("it")) +
               " is not nothing:`.");
  }

  auto record_of(const expression &p_object, string_view_type p_what)
      -> const record & {
    if (!p_object.type.is_record())
      fail(p_object.location, string_type(p_what) + " needs a record but got " +
                                  describe_in_english(p_object.type) + ".");
    check_not_nothing(p_object, p_what);
    check_record_exists(p_object.type, p_object.location);
    return *records.at(p_object.type.record);
  }

  auto field_of(const record &p_record, const string_type &p_field,
                source_location p_location) -> const record_field & {
    const auto *const found = p_record.find(p_field);
    if (found == nullptr) {
      std::vector<string_type> names;
      for (const auto &field : p_record.fields)
        names.push_back(field.name);
      auto owner = describe_in_english(value_type::record_of(p_record.name));
      owner.front() = 'A';
      fail(p_location, owner + " has no field '" + p_field +
                           "'. Its fields are: " + join(names) + ".");
    }
    return *found;
  }

  // A C struct's `char *` field (zlib's `next_in`) also takes an array of
  // bytes or a pointer: C keeps an address there.
  static auto fits_c_field(const record &p_record, const record_field &p_field,
                           const expression &p_value) -> bool {
    if (!p_record.is_c || p_field.type != value_type::string() ||
        p_field.text_length > 0)
      return false;
    return p_value.type.is_pointer() ||
           (p_value.type.is_c_array() &&
            (p_value.type.pointee == "u8" || p_value.type.pointee == "i8"));
  }

  auto check_new(expression &p_call) -> value_type {
    const auto &type = *p_call.type_argument;
    check_record_exists(type, p_call.location);
    const auto &declaration = *records.at(type.record);
    // A generic record's types are said, or decided by the fields given.
    if (!declaration.type_parameters.empty()) {
      const auto arguments = type.record_arguments();
      if (arguments.size() != declaration.type_parameters.size() ||
          std::ranges::any_of(arguments, [](const value_type &p_argument) {
            return !p_argument.is_known() ||
                   p_argument.to_string().find('?') != string_type::npos;
          })) {
        auto name = declaration.name;
        std::ranges::replace(name, '_', ' ');
        fail(p_call.location, "Say what a " + name + " holds: `a new " + name +
                                  " of …`, or give it a field that says.");
      }
    } else if (!type.arguments.empty()) {
      fail(p_call.location, "'" + declaration.name + "' takes no types.");
    }
    std::set<string_type> given;
    for (std::size_t index = 0; index < p_call.arguments.size(); ++index) {
      const auto &name = p_call.fields[index];
      const auto &field = field_of(declaration, name, p_call.location);
      if (!given.insert(name).second)
        fail(p_call.location, "The field '" + name + "' is given twice.");
      if (!fits_c_field(declaration, field, p_call.arguments[index]))
        expect(p_call.arguments[index], field_type(declaration, field, type),
               "The field '" + name + "'");
    }
    std::vector<string_type> missing;
    for (const auto &field : declaration.fields)
      if (!given.contains(field.name))
        missing.push_back(field.name);
    // A C struct starts zeroed, as C's `= {0}`.
    if (!missing.empty() && !declaration.is_c)
      fail(p_call.location, "A new " + declaration.name +
                                " needs a value for every field; missing: " +
                                join(missing) + ".");
    return type;
  }

  auto expect_numeric(expression &p_call) -> value_type {
    return unify_numbers(p_call, p_call.arguments, true);
  }

  // Whole numbers of any size, worked out in their common type.
  auto expect_whole(expression &p_call) -> value_type {
    const auto type = unify_numbers(p_call, p_call.arguments, true);
    if (!type.is_whole())
      fail(p_call.location, "'" + p_call.text + "' needs whole numbers, not " +
                                describe_in_english(type) + ".");
    return type;
  }

  auto expect_booleans(expression &p_call) -> void {
    for (auto &argument : p_call.arguments)
      expect(argument, value_type::boolean(),
             "Every argument of '" + p_call.text + "'");
  }

  // The records a variable's value (or a list's items) can be, where the
  // program says so: a list written out, or a variable given one. A list
  // of circles and squares is a list of the first trait they share; a
  // phrase of another trait on its items is checked here with these, and
  // otherwise when the program runs.
  std::map<string_type, std::set<string_type>> known_records;

  auto records_in(const expression &p_value) const
      -> std::optional<std::set<string_type>> {
    if (p_value.kind == expression::kind_type::variable) {
      const auto found = known_records.find(p_value.text);
      if (found == known_records.end())
        return std::nullopt;
      return found->second;
    }
    if (p_value.kind != expression::kind_type::call)
      return std::nullopt;
    if (p_value.text == "get" && !p_value.arguments.empty())
      return records_in(p_value.arguments[0]);
    if (p_value.text == "new" && p_value.type.is_record() &&
        !traits.contains(p_value.type.record))
      return std::set{p_value.type.record};
    if (p_value.text != "list")
      return std::nullopt;
    std::set<string_type> result;
    for (const auto &item : p_value.arguments) {
      if (item.type.is_record() && !item.type.is_list &&
          !traits.contains(item.type.record)) {
        result.insert(item.type.record);
        continue;
      }
      const auto inner = records_in(item);
      if (!inner)
        return std::nullopt;
      result.insert(inner->begin(), inner->end());
    }
    return result;
  }

  // After `NAME = VALUE`: what its records can be now.
  auto remember_records(const string_type &p_name, const expression &p_value)
      -> void {
    const auto holds_records =
        p_value.type.is_record() ||
        (p_value.type.is_list && p_value.type.element().is_record());
    auto records = holds_records ? records_in(p_value) : std::nullopt;
    if (p_value.type.is_record() && !p_value.type.is_list &&
        !traits.contains(p_value.type.record))
      records = std::set{p_value.type.record};
    if (records)
      known_records[p_name] = std::move(*records);
    else
      known_records.erase(p_name);
  }

  // Each record `p_value` can be must implement `p_trait`.
  auto check_known_records(const expression &p_value, const string_type &p_trait,
                           const string_type &p_what) -> void {
    const auto records = records_in(p_value);
    if (!records || !traits.contains(p_trait))
      return;
    for (const auto &record : *records)
      if (record != p_trait && !impls.contains(impl_key(p_trait, record)))
        fail(p_value.location,
             p_what + " needs " +
                 describe_in_english(value_type::record_of(p_trait)) +
                 ", but this can be " +
                 describe_in_english(value_type::record_of(record)) +
                 ", which isn't one.");
  }

  // A call may change a list it is given, and a phrase the program's
  // variables: forget what they held.
  auto forget_records(const expression &p_call) -> void {
    static const std::set<string_view_type> reading = {"get", "len", "list",
                                                       "new", "field", "print",
                                                       "to_str"};
    if (reading.contains(p_call.text))
      return;
    for (const auto &argument : p_call.arguments)
      if (argument.kind == expression::kind_type::variable &&
          argument.type.is_list)
        known_records.erase(argument.text);
    if (functions.contains(p_call.text) || method_owner.contains(p_call.text))
      std::erase_if(known_records, [&](const auto &p_entry) {
        return globals.contains(p_entry.first) &&
               lookup(p_entry.first) == &globals.at(p_entry.first);
      });
  }

  auto check_call(expression &p_call) -> value_type {
    auto type = check_call_unforgetting(p_call);
    forget_records(p_call);
    return type;
  }

  auto check_call_unforgetting(expression &p_call) -> value_type {
    if (p_call.text == "ref")
      return check_ref(p_call);
    // A C macro's type or field argument, which only a macro takes.
    if (p_call.text == "type" || p_call.text == "member")
      return value_type::nothing();
    // Its argument is a phrase's name, not a value.
    if (p_call.text == "phrase-of") {
      if (p_call.arguments.size() != 1 ||
          p_call.arguments[0].kind != expression::kind_type::variable)
        fail(p_call.location, "Expected (phrase-of NAME).");
      return phrase_value(p_call.arguments[0]);
    }
    // (ref x) is only meaningful as an argument of a C function.
    const bool is_c_call = !is_builtin(p_call.text) &&
                           !functions.contains(p_call.text) &&
                           c_functions.contains(p_call.text);
    // `and` / `or` evaluate a part only when the ones before allow it, so
    // a check before narrows what comes after: `p is not nothing and …`.
    const bool is_logical = p_call.text == "and" || p_call.text == "or";
    const auto before = narrowed;
    for (auto &argument : p_call.arguments) {
      if (is_c_call && argument.kind == expression::kind_type::call &&
          argument.text == "ref" && argument.arguments.size() == 1 &&
          argument.arguments[0].kind == expression::kind_type::variable)
        argument.type = check_ref(argument, true);
      else
        check_expression(argument);
      if (is_logical) {
        const auto narrows = narrowing(argument, p_call.text == "and");
        narrowed.insert(narrows.begin(), narrows.end());
      }
    }
    if (is_logical)
      narrowed = before;

    const auto &name = p_call.text;
    auto &arguments = p_call.arguments;

    if (name == "new")
      return check_new(p_call);
    if (name == "field") {
      const auto &declaration = record_of(arguments[0], "A field");
      return field_type(declaration,
                        field_of(declaration, p_call.fields[0], p_call.location),
                        arguments[0].type);
    }
    if (name == "-") {
      check_count(p_call, 1, SIZE_MAX);
      if (const auto type = check_variable_arithmetic(p_call))
        return *type;
      return expect_numeric(p_call);
    }
    if (name == "+" || name == "concat") {
      check_count(p_call, name == "+" ? 2 : 1, SIZE_MAX);
      // Adding anything to text joins them as text.
      bool has_text = name == "concat";
      for (const auto &argument : arguments)
        has_text = has_text || argument.type == value_type::string();
      if (!has_text) {
        if (const auto type = check_variable_arithmetic(p_call))
          return *type;
        return expect_numeric(p_call);
      }
      show_records_in_copy(p_call);
      for (const auto &argument : arguments) {
        if (argument.type.is_variable()) {
          expect_trait(argument.type, "showable", argument.location,
                       "Joining it to text");
          continue;
        }
        if (!is_printable(argument.type))
          fail(argument.location, "Cannot join " + describe_in_english(argument.type) +
                                      " to text.");
      }
      return value_type::string();
    }
    if (name == "*" || name == "min" || name == "max") {
      check_count(p_call, 2, SIZE_MAX);
      if (const auto type = check_variable_arithmetic(p_call))
        return *type;
      return expect_numeric(p_call);
    }
    if (name == "/") {
      // Real division: of whole numbers, a decimal.
      check_count(p_call, 2, SIZE_MAX);
      if (const auto type = check_variable_arithmetic(p_call))
        return *type;
      const auto type = expect_numeric(p_call);
      return type.is_decimal() ? type : value_type::real();
    }
    if (name == "div") {
      check_count(p_call, 2, 2);
      return expect_whole(p_call);
    }
    if (name == "%") {
      check_count(p_call, 2, 2);
      if (const auto type = check_variable_arithmetic(p_call))
        return *type;
      return expect_numeric(p_call);
    }
    if (name == "=" || name == "!=" || name == "<" || name == "<=" ||
        name == ">" || name == ">=") {
      check_count(p_call, 2, 2);
      if (lower_record_comparison(p_call))
        return check_call_unforgetting(p_call);
      // A type variable's values (§3): the same variable, with the trait.
      const auto &left = p_call.arguments[0].type;
      const auto &right = p_call.arguments[1].type;
      if (left.is_variable() || right.is_variable()) {
        const bool is_equality = name == "=" || name == "!=";
        const auto &variable = left.is_variable() ? left : right;
        if (left.without_nothing() != right.without_nothing())
          fail(p_call.location, "Cannot compare " + describe_in_english(left) +
                                    " with " + describe_in_english(right) + ".");
        expect_trait(variable, is_equality ? "equatable" : "comparable",
                     p_call.location, "'" + name + "'");
        return value_type::boolean();
      }
    }
    if (name == "=" || name == "!=") {
      check_count(p_call, 2, 2);
      const auto &left = arguments[0].type;
      const auto &right = arguments[1].type;
      const auto is_text_or_pointer = [](const value_type &p_type) {
        return p_type == value_type::string() || p_type.is_pointer();
      };
      const bool is_record_and_null =
          (left.is_record() && is_null(arguments[1])) ||
          (right.is_record() && is_null(arguments[0]));
      if (is_record_and_null) {
        const auto &record = left.is_record() ? left : right;
        const auto &value = left.is_record() ? arguments[0] : arguments[1];
        if (!may_be_nothing(record) &&
            !(value.kind == expression::kind_type::variable &&
              narrowed.contains(value.text)))
          fail(p_call.location,
               describe_in_english(record) + " is never nothing, so this is " +
                   (name == "=" ? "always false" : "always true") +
                   ". If it may be, say so: " +
                   describe_in_english(record.or_nothing()) + ".");
      }
      // The same record, whether or not either may be nothing.
      if (left.is_record() && right.is_record() &&
          left.without_nothing() == right.without_nothing())
        return value_type::boolean();
      if (left.is_numeric() && right.is_numeric()) {
        unify_numbers(p_call, arguments, false);
        return value_type::boolean();
      }
      if (!(left.is_numeric() && right.is_numeric()) && !is_record_and_null &&
          !(left == right && !left.is_list &&
            left.base != base_type::nothing) &&
          !(is_text_or_pointer(left) && is_text_or_pointer(right)))
        fail(p_call.location, "Cannot compare " + describe_in_english(left) +
                                  " with " + describe_in_english(right) + ".");
      return value_type::boolean();
    }
    if (name == "<" || name == "<=" || name == ">" || name == ">=") {
      check_count(p_call, 2, 2);
      // Text is ordered as in a dictionary.
      if (arguments[0].type != value_type::string() ||
          arguments[1].type != value_type::string())
        unify_numbers(p_call, arguments, false);
      return value_type::boolean();
    }
    if (name == "and" || name == "or") {
      check_count(p_call, 2, SIZE_MAX);
      expect_booleans(p_call);
      return value_type::boolean();
    }
    if (name == "not") {
      check_count(p_call, 1, 1);
      expect_booleans(p_call);
      return value_type::boolean();
    }
    if (name == "int") {
      check_count(p_call, 1, 1);
      if (arguments[0].type != value_type::boolean())
        expect_numeric(p_call);
      return value_type::integer();
    }
    if (name == "as" || name == "wrap-to" || name == "clamp-to") {
      check_count(p_call, 1, 1);
      // A type variable's number: in each copy, that number made one.
      if (arguments[0].type.is_variable()) {
        expect_trait(arguments[0].type, "number", arguments[0].location,
                     "'" + name + "'");
        return *p_call.type_argument;
      }
      if (!arguments[0].type.is_numeric())
        fail(arguments[0].location,
             "Only a number can be made " +
                 describe_in_english(*p_call.type_argument) + ", not " +
                 describe_in_english(arguments[0].type) + ".");
      // `300 as a byte` is known not to fit now.
      if (name == "as")
        give_number_type(arguments[0], *p_call.type_argument, true);
      return *p_call.type_argument;
    }
    if (is_bit_operation(name))
      return check_bits(p_call);
    // `(wrapping E)`, `(saturating E)`: E's whole-number arithmetic keeps
    // the low bits, or the nearest value, where it would be too big.
    if (name == "wrapping" || name == "saturating") {
      check_count(p_call, 1, 1);
      if (!arguments[0].type.is_whole())
        fail(arguments[0].location,
             "Only whole numbers wrap or are clamped, not " +
                 describe_in_english(arguments[0].type) + ".");
      return arguments[0].type;
    }
    // A decimal's bits as a whole number of its size, and back.
    if (name == "float-bits" || name == "bits-float") {
      check_count(p_call, 1, 1);
      const auto &type = arguments[0].type;
      if (name == "float-bits") {
        if (!type.is_decimal())
          fail(arguments[0].location, "Only a decimal's bits can be read this "
                                      "way, not " + describe_in_english(type) + "'s.");
        return value_type::number(type.is_sized() ? c_type::uint_type
                                                  : c_type::size_type);
      }
      if (!type.is_whole() ||
          (number_bits(*type.number_type()) != 32 &&
           number_bits(*type.number_type()) != 64))
        fail(arguments[0].location, "A decimal is made of 32 or 64 bits, not " +
                                        describe_in_english(type) + ".");
      return number_bits(*type.number_type()) == 32
                 ? value_type::number(c_type::float_type)
                 : value_type::real();
    }
    // A number's bytes, lowest first (the machine's order and most files'),
    // or highest first; and a number read from bytes.
    if (name == "to-bytes" || name == "to-bytes-big") {
      check_count(p_call, 1, 1);
      // Text's bytes are its characters' UTF-8 bytes, in order.
      if (name == "to-bytes" && arguments[0].type == value_type::string())
        return value_type::list_of(value_type::number(c_type::uint8_type));
      if (!arguments[0].type.is_numeric())
        fail(arguments[0].location, "Only a number has bytes this way, not " +
                                        describe_in_english(arguments[0].type) + ".");
      return value_type::list_of(value_type::number(c_type::uint8_type));
    }
    if (name == "from-bytes" || name == "from-bytes-big") {
      check_count(p_call, 2, 2);
      const auto &bytes = arguments[0].type;
      if (!bytes.is_c_array() || (bytes.pointee != "u8" && bytes.pointee != "i8"))
        fail(arguments[0].location, "A number is read from a list of bytes, "
                                    "not " + describe_in_english(bytes) + ".");
      expect(arguments[1], value_type::integer(), "Where the number starts");
      return *p_call.type_argument;
    }
    if (name == "to-hex" || name == "to-binary") {
      check_count(p_call, 1, 2);
      if (!arguments[0].type.is_whole())
        fail(arguments[0].location,
             "Only a whole number can be shown in " +
                 string_type(name == "to-hex" ? "hex" : "binary") + ", not " +
                 describe_in_english(arguments[0].type) + ".");
      if (arguments.size() == 2)
        expect(arguments[1], value_type::integer(), "How many digits");
      return value_type::string();
    }
    if (name == "from-hex") {
      check_count(p_call, 1, 1);
      expect(arguments[0], value_type::string(), "The hex digits");
      return value_type::integer();
    }
    if (name == "float" || name == "sqrt" || name == "floor") {
      check_count(p_call, 1, 1);
      if (arguments[0].type.is_variable())
        expect_trait(arguments[0].type, "number", arguments[0].location,
                     "'" + name + "'");
      else
        expect_numeric(p_call);
      return value_type::real();
    }
    if (name == "pow") {
      check_count(p_call, 2, 2);
      expect_numeric(p_call);
      return value_type::real();
    }
    if (name == "abs") {
      check_count(p_call, 1, 1);
      return expect_numeric(p_call);
    }
    if (name == "len") {
      check_count(p_call, 1, 1);
      if (!arguments[0].type.is_list && !arguments[0].type.is_map())
        fail(arguments[0].location, "'len' needs a list.");
      return value_type::integer();
    }
    if (name.starts_with("map-"))
      return check_map(p_call);
    if (name == "get") {
      check_count(p_call, 2, 2);
      if (!arguments[0].type.is_list)
        fail(arguments[0].location, "'get' needs a list as first argument.");
      expect(arguments[1], value_type::integer(), "The index");
      return arguments[0].type.element();
    }
    if (name == "list") {
      // The elaborator decides the element type from how the list is used.
      if (p_call.type_argument) {
        check_type_exists(*p_call.type_argument, p_call.location);
        for (auto &argument : arguments)
          expect(argument, *p_call.type_argument, "Every item of the list");
        return value_type::list_of(*p_call.type_argument);
      }
      if (arguments.empty())
        fail(p_call.location,
             "An empty list has no element type; say what it holds, as in "
             "\"empty list of whole numbers\".");
      auto element = arguments[0].type;
      if (element.is_numeric() &&
          std::ranges::all_of(arguments, [](const expression &p_item) {
            return p_item.type.is_numeric();
          }))
        element = unify_numbers(p_call, arguments, true);
      for (const auto &argument : arguments)
        if (!element.is_numeric() && argument.type != element)
          fail(argument.location, "All list elements must have the same type.");
      if (element == value_type::nothing() || element.is_pointer())
        fail(p_call.location,
             "Lists can only hold numbers, text, true and false, records or "
             "other lists.");
      return value_type::list_of(element);
    }
    if (name == "array") {
      check_count(p_call, 1, 1);
      expect(arguments[0], value_type::integer(), "The array size");
      return value_type::list_of(*p_call.type_argument);
    }
    if (name == "append") {
      check_count(p_call, 2, 2);
      if (!arguments[0].type.is_list)
        fail(arguments[0].location, "'append' needs a list as first argument.");
      expect(arguments[1], arguments[0].type.element(), "The appended value");
      return value_type::nothing();
    }
    if (name == "new-memory") {
      check_count(p_call, 0, 0);
      return value_type::memory();
    }
    if (name == "clear-memory") {
      check_count(p_call, 1, 1);
      expect(arguments[0], value_type::memory(), "What 'clear' empties");
      return value_type::nothing();
    }
    if (name == "free" || name == "keep" || name == "copy") {
      check_count(p_call, 1, 1);
      const auto &type = arguments[0].type;
      const bool freeable = type.is_list || type == value_type::string() ||
                            type.is_record() || type.is_map() ||
                            (name == "free" && (type.is_pointer() ||
                                                type == value_type::memory()));
      if (!freeable)
        fail(arguments[0].location,
             "'" + name + "' works on text, lists and records" +
                 (name == "free" ? ", C pointers and memories." : "."));
      // Room a record points at has no length Ovis can trust (generics.md
      // §5): its items can't be followed yet.
      if ((name == "keep" || name == "copy") && type.is_record() &&
          records.contains(type.record))
        for (const auto &field : records.at(type.record)->fields)
          if (field.type.is_typed_pointer() && field.type.kind == pointer_kind::many)
            fail(arguments[0].location,
                 "'" + name + "' can't follow " + describe_in_english(type) +
                     ": its '" + field.name + "' points at room for items "
                     "whose number Ovis doesn't know. Copy the items into a "
                     "list first.");
      return name == "free" ? value_type::nothing() : type;
    }
    if (name == "remove-at") {
      check_count(p_call, 2, 2);
      if (!arguments[0].type.is_list)
        fail(arguments[0].location, "'remove-at' needs a list as first argument.");
      expect(arguments[1], value_type::integer(), "The removed item's index");
      return value_type::nothing();
    }
    if (name == "sort" || name == "reverse") {
      check_count(p_call, 1, 1);
      if (!arguments[0].type.is_list)
        fail(arguments[0].location, "'" + name + "' needs a list.");
      if (name == "sort" && arguments[0].type.element().is_variable()) {
        expect_trait(arguments[0].type.element(), "comparable",
                     arguments[0].location, "'sort'");
        return value_type::nothing();
      }
      // Records are sorted by the `comes before` their author wrote: the
      // prelude's insertion sort, which keeps equal ones in order.
      if (name == "sort" && arguments[0].type.inner_lists == 0 &&
          arguments[0].type.element().is_record() &&
          functions.contains("prelude_insertion_sort")) {
        p_call.text = "prelude_insertion_sort";
        p_call.type_arguments.clear();
        return check_call_unforgetting(p_call);
      }
      if (name == "sort" &&
          (arguments[0].type.inner_lists > 0 ||
           (arguments[0].type.base != base_type::integer &&
            arguments[0].type.base != base_type::real &&
            arguments[0].type.base != base_type::string)))
        fail(arguments[0].location, "Only lists of numbers or text can be "
                                    "sorted.");
      return value_type::nothing();
    }
    if (name == "read_int" || name == "read_float" || name == "read_line") {
      check_count(p_call, 0, 0);
      return name == "read_int"    ? value_type::integer()
             : name == "read_float" ? value_type::real()
                                    : value_type::string();
    }
    if (name == "to_str") {
      check_count(p_call, 1, 1);
      // An array of bytes is text C wrote (references.md §4): read up to
      // its first zero byte.
      if (arguments[0].type.is_c_array() &&
          (arguments[0].type.pointee == "u8" || arguments[0].type.pointee == "i8"))
        return value_type::string();
      // In a copy, a record made text where its type variable was: the
      // `text of` its author wrote.
      if (in_copy() && arguments[0].type.is_record() &&
          functions.contains("text_of__" + arguments[0].type.record)) {
        p_call.text = "text_of__" + arguments[0].type.record;
        return check_call_unforgetting(p_call);
      }
      if (arguments[0].type.is_variable())
        expect_trait(arguments[0].type, "showable", arguments[0].location,
                     "Making it text");
      else if (!is_printable(arguments[0].type))
        fail(arguments[0].location,
             "Cannot turn " + describe_in_english(arguments[0].type) + " into text.");
      return value_type::string();
    }
    if (name == "substring") {
      check_count(p_call, 3, 3);
      expect(arguments[0], value_type::string(), "The text");
      expect(arguments[1], value_type::integer(), "The start");
      expect(arguments[2], value_type::integer(), "The length");
      return value_type::string();
    }
    if (name == "set-char") {
      check_count(p_call, 3, 3);
      expect(arguments[0], value_type::string(), "The text");
      expect(arguments[1], value_type::integer(), "The index");
      expect(arguments[2], value_type::integer(), "The character code");
      return value_type::nothing();
    }
    if (name == "load" || name == "store" || name == "size-of" ||
        name == "offset" || name == "cast" || name == "view" ||
        name == "alloc" || name == "alloc-more")
      return check_raw(p_call);
    if (name == "call") {
      if (arguments.empty() || !arguments[0].type.is_phrase())
        fail(p_call.location, "'call' needs a phrase to call, not " +
                                  describe_in_english(arguments.empty()
                                                          ? value_type::nothing()
                                                          : arguments[0].type) +
                                  ".");
      const auto signature = read_signature(arguments[0].type);
      if (!signature)
        fail(p_call.location, "This phrase's type can't be read.");
      check_count(p_call, signature->parameters.size() + 1,
                  signature->parameters.size() + 1);
      for (std::size_t index = 1; index < arguments.size(); ++index)
        expect(arguments[index], signature->parameters[index - 1],
               "Argument " + std::to_string(index) + " of the phrase");
      return signature->result;
    }
    if (name == "char_at") {
      check_count(p_call, 2, 2);
      expect(arguments[0], value_type::string(), "The text");
      expect(arguments[1], value_type::integer(), "The index");
      return value_type::integer();
    }
    if (name == "chr" || name == "buffer") {
      check_count(p_call, 1, 1);
      expect(arguments[0], value_type::integer(),
             name == "chr" ? "The character code" : "The buffer size");
      return value_type::string();
    }
    if (name == "random") {
      check_count(p_call, 2, 2);
      expect(arguments[0], value_type::integer(), "The lowest random value");
      expect(arguments[1], value_type::integer(), "The highest random value");
      return value_type::integer();
    }

    if (const auto method = method_owner.find(name);
        method != method_owner.end())
      return check_method_call(p_call, *method->second);

    const auto found = functions.find(name);
    if (found == functions.end()) {
      if (const auto c_found = c_functions.find(name);
          c_found != c_functions.end()) {
        if (c_found->second->is_generic_macro())
          return check_generic_macro(p_call, *c_found->second);
        return check_c_call(p_call, *c_found->second);
      }
      fail(p_call.location, "Unknown function '" + name +
                                "'. Known functions: " + known_functions() +
                                ".");
    }
    if (found->second.declaration->is_generic())
      return check_generic_call(p_call, found->second);
    copy_for_sized_lists(p_call, found->second);
    const auto &callee = *functions.at(p_call.text).declaration;
    check_count(p_call, callee.parameters.size(), callee.parameters.size());
    for (std::size_t index = 0; index < arguments.size(); ++index) {
      const auto &type = callee.parameters[index].type;
      const auto what = "Argument '" + callee.parameters[index].name +
                        "' of '" + name + "'";
      // An untyped parameter is one type (generics.md §3, G2): say how it
      // takes any.
      if (callee.parameters[index].is_inferred && !callee.name.starts_with("prelude_") &&
          !can_assign(arguments[index].type, type) && !is_null(arguments[index]) &&
          !(is_literal_number(arguments[index]) && type.is_numeric()))
        fail(arguments[index].location,
             what + " is " + describe_in_english(type) +
                 " (from how it is used) but here it is " +
                 describe_in_english(arguments[index].type) +
                 ". For a phrase that takes any type, write (" +
                 callee.parameters[index].name + ": any thing) in its header.");
      expect(arguments[index], type, what);
      if (type.is_record() || (type.is_list && type.element().is_record()))
        check_known_records(arguments[index],
                            type.is_list ? type.element().record : type.record,
                            what);
    }
    return callee.return_type;
  }

  // A phrase written for a list of whole numbers or decimals given a list
  // of numbers of a size (`to total of (xs: list of whole numbers)` given
  // int16s): it is called as a copy whose list parameters are that list
  // (`total_of__i16`), made once; its other types stay, and its locals hold
  // what they did. The library's phrases are generic instead
  // (`prelude_sum_int (forall (item number)) …`).
  auto copy_for_sized_lists(expression &p_call, const function_entry &p_entry)
      -> void {
    const auto &callee = *p_entry.declaration;
    if (p_call.arguments.size() != callee.parameters.size())
      return;
    string_type suffix;
    auto parameters = callee.parameters;
    for (std::size_t index = 0; index < parameters.size(); ++index) {
      const auto &wanted = parameters[index].type;
      const auto &given = p_call.arguments[index].type;
      if (wanted.is_list && wanted.inner_lists == 0 && wanted.pointee.empty() &&
          (wanted.base == base_type::integer || wanted.base == base_type::real) &&
          given.is_c_array() && given.base == wanted.base) {
        parameters[index].type = given;
        suffix += "__" + given.pointee;
      }
    }
    if (suffix.empty())
      return;
    const auto name = callee.name + suffix;
    if (!functions.contains(name)) {
      auto &copy = copies.emplace_back(callee);
      copy.name = name;
      copy.parameters = std::move(parameters);
      copy_units.push_back(p_entry.unit_index);
      functions.emplace(name, function_entry{&copy, p_entry.unit_index});
    }
    p_call.text = name;
  }

  // A generic C macro the elaborator left (generics.md §9): in a generic
  // phrase, on a type variable's values, it is checked in each copy (C
  // states no bounds), where its arguments' types are known: the copy of the
  // macro for them is made here.
  std::deque<c_function> macro_copies;

  auto check_generic_macro(expression &p_call, const c_function &p_macro)
      -> value_type {
    const bool on_variables = std::ranges::any_of(
        p_call.arguments,
        [](const expression &p_argument) { return p_argument.type.has_variables(); });
    if (on_variables && current_function != nullptr &&
        current_function->is_generic())
      return p_call.arguments.empty() ? value_type::nothing()
                                      : p_call.arguments[0].type;
    string_type problem;
    const auto arguments = macro_arguments_of(
        target, p_macro, p_call.arguments,
        [&](std::size_t p_index) { return p_call.arguments[p_index].type; }, true,
        problem);
    if (!problem.empty())
      fail(p_call.location, problem);
    const auto [headers, flags] = headers_of(target);
    const auto answers = copy_c_macros({{&p_macro, *arguments, headers, flags}});
    if (!answers[0].copy)
      fail(p_call.location, "The C macro '" + p_macro.name +
                                "' can't be given these: clang says \"" +
                                answers[0].problem + "\".");
    auto copy = *answers[0].copy;
    copy.result_record = macro_result_record(target, copy);
    std::vector<expression> values;
    for (std::size_t at = 0; at < p_call.arguments.size(); ++at) {
      const auto role = at < p_macro.macro_roles.size() ? p_macro.macro_roles[at]
                                                        : c_function::macro_role::value;
      if (role != c_function::macro_role::type && role != c_function::macro_role::field)
        values.push_back(std::move(p_call.arguments[at]));
    }
    p_call.arguments = std::move(values);
    p_call.text = copy.name;
    if (!c_functions.contains(copy.name)) {
      auto &kept = macro_copies.emplace_back(std::move(copy));
      c_functions[kept.name] = &kept;
    }
    return check_c_call(p_call, *c_functions.at(p_call.text));
  }

  // A call of a trait's method. On a value of the trait it is dispatched
  // when the program runs (codegen); on a record it calls that record's
  // impl directly.
  auto check_method_call(expression &p_call, const trait &p_owner)
      -> value_type {
    const auto &name = p_call.text;
    const auto &signature = *p_owner.find(name);
    auto &arguments = p_call.arguments;
    check_count(p_call, signature.parameters.size(), signature.parameters.size());
    const auto &self = arguments[0];
    // A type variable with the trait as a bound (§3): in each copy, the
    // record's impl.
    if (self.type.is_variable()) {
      expect_trait(self.type, p_owner.name, self.location, "'" + name + "'");
      for (std::size_t index = 1; index < arguments.size(); ++index)
        expect(arguments[index],
               signature.is_same(index) ? self.type
                                        : signature.parameters[index].type,
               "Argument '" + signature.parameters[index].name + "' of '" +
                   name + "'");
      return signature.return_type;
    }
    // A number or text with an impl (§4): its phrase, called directly; a
    // default the impl leaves out is copied for it.
    if (!self.type.is_record() && self.type.is_known() &&
        impls.contains(impl_key(p_owner.name, self.type.to_string()))) {
      for (std::size_t index = 1; index < arguments.size(); ++index)
        expect(arguments[index],
               signature.is_same(index) ? self.type
                                        : signature.parameters[index].type,
               "Argument '" + signature.parameters[index].name + "' of '" +
                   name + "'");
      const auto &chosen = *impls.at(impl_key(p_owner.name, self.type.to_string()));
      const auto own = impl_function_name(name, chosen.type);
      if (std::ranges::find(chosen.methods, name) != chosen.methods.end())
        p_call.text = own;
      else if (signature.has_default && functions.contains(name)) {
        if (!functions.contains(own)) {
          const auto &entry = functions.at(name);
          auto &copy = copies.emplace_back(*entry.declaration);
          copy.name = own;
          // The trait's value is the number or text throughout.
          const auto number = self.type;
          for_each_type(copy, [&](value_type &p_type) {
            if (p_type.base == base_type::record && p_type.record == p_owner.name &&
                p_type.arguments.empty() && !p_type.nullable)
              p_type = p_type.is_list ? value_type::list_of(number) : number;
          });
          copy_units.push_back(entry.unit_index);
          functions.emplace(own, function_entry{&copy, entry.unit_index});
        }
        p_call.text = own;
      }
      return signature.return_type;
    }
    check_not_nothing(self, "'" + name + "'");
    // A value of another trait may do: a list of records that are two
    // traits has the first as its type. Whether this value does is checked
    // when the program runs.
    const auto shares_record = [&] {
      return self.type.is_record() && traits.contains(self.type.record) &&
             std::ranges::any_of(impls, [&](const auto &p_impl) {
               return p_impl.second->trait == p_owner.name &&
                      impls.contains(
                          impl_key(self.type.record, p_impl.second->type));
             });
    };
    if (!can_assign(self.type, value_type::record_of(p_owner.name)) &&
        !shares_record())
      fail(self.location,
           "'" + name + "' is a phrase of " +
               describe_in_english(value_type::record_of(p_owner.name)) +
               ", but " + describe_in_english(self.type) +
               " doesn't implement it.");
    check_known_records(self, p_owner.name, "'" + name + "'");
    for (std::size_t index = 1; index < arguments.size(); ++index)
      expect(arguments[index], signature.parameters[index].type,
             "Argument '" + signature.parameters[index].name + "' of '" + name +
                 "'");
    // `(a) comes before (b)`: on two records, the same one. A value of
    // the trait beside a record is checked when the program runs.
    bool direct = !traits.contains(self.type.record);
    for (std::size_t index = 1; index < arguments.size(); ++index) {
      if (!signature.is_same(index) || !direct)
        continue;
      const auto &other = arguments[index].type;
      if (other.is_record() && traits.contains(other.record))
        direct = false;
      else if (!can_assign(self.type, other))
        fail(arguments[index].location,
             "'" + name + "' needs two values of the same kind: " +
                 describe_in_english(self.type) + " and " +
                 describe_in_english(other) + " aren't.");
    }
    if (direct) {
      const auto &chosen = *impls.at(impl_key(p_owner.name, self.type.record));
      const auto &written = chosen.methods;
      if (std::ranges::find(written, name) != written.end()) {
        p_call.text = impl_function_name(name, self.type.record);
        // For every stack (G2): this stack's copy of the phrase.
        if (!chosen.type_parameters.empty()) {
          const auto types = self.type.record_arguments();
          type_bindings bindings;
          for (std::size_t index = 0;
               index < std::min(types.size(), chosen.type_parameters.size());
               ++index)
            bindings[chosen.type_parameters[index].name] = types[index];
          if (std::ranges::none_of(types, &value_type::has_variables))
            p_call.text = make_copy(p_call.text, types, bindings);
          else
            p_call.type_arguments = types;
        }
      }
    }
    return signature.return_type;
  }

  // A whole number's bits (numbers.md §4): the result has the number's
  // type (two numbers' common one); a bit's place, a count of places, and
  // the counts of bits are whole numbers of any size.
  auto check_bits(expression &p_call) -> value_type {
    const auto &name = p_call.text;
    auto &arguments = p_call.arguments;
    if (name == "bit-and" || name == "bit-or" || name == "bit-xor") {
      check_count(p_call, 2, SIZE_MAX);
      return expect_whole(p_call);
    }
    const std::size_t count = name == "bit-field"  ? 3
                              : name == "bit-not" || name == "count-ones" ||
                                      name == "leading-zeros" ||
                                      name == "trailing-zeros" ||
                                      name == "byte-swap"
                                  ? 1
                                  : 2;
    check_count(p_call, count, count);
    auto &value = arguments[0];
    if (!value.type.is_whole())
      fail(value.location, "'" + name + "' works on the bits of a whole number, "
                                        "not " +
                               describe_in_english(value.type) + ".");
    for (std::size_t index = 1; index < arguments.size(); ++index)
      if (!arguments[index].type.is_whole())
        fail(arguments[index].location,
             "A bit's place is a whole number, not " +
                 describe_in_english(arguments[index].type) + ".");
    if (name == "bit-test")
      return value_type::boolean();
    if (name == "count-ones" || name == "leading-zeros" ||
        name == "trailing-zeros")
      return value_type::integer();
    return value.type;
  }

  // Maps: `map-new map-put map-get map-has map-remove map-keys`.
  auto check_map(expression &p_call) -> value_type {
    const auto &name = p_call.text;
    auto &arguments = p_call.arguments;
    if (name == "map-new") {
      // Keys are kept as 64 bits each (the runtime's table), values are
      // widened and narrowed back.
      const auto key = p_call.type_argument->map_key();
      if (key != value_type::integer() && key != value_type::string() &&
          key != value_type::real())
        fail(p_call.location, "A map's keys are numbers or text, not " +
                                  describe_in_english(key) + ".");
      check_type_exists(p_call.type_argument->map_value(), p_call.location);
      return *p_call.type_argument;
    }
    const std::size_t count = name == "map-put"    ? 3
                              : name == "map-keys" ? 1
                                                   : 2;
    check_count(p_call, count, count);
    const auto &map = arguments[0].type;
    if (!map.is_map())
      fail(arguments[0].location,
           "This needs a map but is " + describe_in_english(map) + ".");
    if (count > 1)
      expect(arguments[1], map.map_key(), "The key");
    if (name == "map-put") {
      expect(arguments[2], map.map_value(), "The value");
      return value_type::nothing();
    }
    if (name == "map-get")
      return map.map_value();
    if (name == "map-has")
      return value_type::boolean();
    if (name == "map-keys")
      return value_type::list_of(map.map_key());
    return value_type::nothing(); // map-remove
  }

  // `(phrase-of NAME)`: the phrase NAME as a value, typed by its
  // parameters and result.
  auto phrase_value(const expression &p_name) -> value_type {
    const auto found = functions.find(p_name.text);
    if (found == functions.end() || found->second.declaration == nullptr)
      fail(p_name.location, "'" + p_name.text + "' is not a phrase the program "
                                "defines, so it can't be given as a value.");
    if (method_owner.contains(p_name.text))
      fail(p_name.location, "'" + p_name.text + "' is a trait's phrase; give "
                                "a phrase of its own that calls it.");
    const auto &function = *found->second.declaration;
    std::vector<value_type> parameters;
    for (const auto &parameter : function.parameters)
      parameters.push_back(parameter.type);
    return phrase_type(parameters, function.return_type);
  }

  // An Ovis phrase can be given where C wants this function when each type
  // is the same once C's is read as Ovis's.
  static auto phrase_fits_c(const value_type &p_phrase, const value_type &p_c)
      -> bool {
    const auto ovis = read_signature(p_phrase);
    const auto c = read_signature(p_c);
    if (!ovis || !c || ovis->parameters.size() != c->parameters.size())
      return false;
    const auto same = [](const value_type &p_ovis, const value_type &p_c_side) {
      return p_ovis == p_c_side ||
             (p_c_side == value_type::pointer() &&
              (p_ovis.is_pointer() || p_ovis.is_record() ||
               p_ovis == value_type::string())) ||
             (p_c_side == value_type::string() && p_ovis == value_type::pointer());
    };
    for (std::size_t index = 0; index < c->parameters.size(); ++index)
      if (!same(ovis->parameters[index], c->parameters[index]))
        return false;
    return same(ovis->result, c->result);
  }

  // What is at a pointer's address, or a build error saying why it can't
  // be read.
  auto pointee_of(const expression &p_call, const string_type &p_text)
      -> pointee_type {
    const auto pointee = read_pointee(p_text);
    if (!pointee)
      fail(p_call.location, "'" + p_text + "' is not a type a pointer can point at.");
    if (pointee->kind == pointee_type::kind_type::structure &&
        !records.contains(pointee->record))
      fail(p_call.location, "There is no record called '" + pointee->record + "'.");
    if (pointee->kind == pointee_type::kind_type::value)
      check_type_exists(pointee->value, p_call.location);
    return *pointee;
  }

  // `load store offset cast size-of`: C's pointer operations.
  auto check_raw(expression &p_call) -> value_type {
    const auto &name = p_call.text;
    auto &arguments = p_call.arguments;
    const auto expect_pointer = [&](const expression &p_value) {
      if (p_value.type.is_list || p_value.type.base != base_type::pointer)
        fail(p_value.location, "This needs a pointer but is " +
                                   describe_in_english(p_value.type) + ".");
    };
    if (name == "size-of") {
      pointee_of(p_call, p_call.fields[0]);
      return value_type::integer();
    }
    // Room for N items (generics.md §5), zeroed, in the current memory; and
    // the same room grown, its items copied.
    if (name == "alloc" || name == "alloc-more") {
      const auto pointee = pointee_of(p_call, p_call.fields[0]);
      if (pointee.kind == pointee_type::kind_type::value &&
          (pointee.value == value_type::nothing() ||
           pointee.value == value_type::memory()))
        fail(p_call.location, "There is no room to make for " +
                                  describe_in_english(pointee.value) + ".");
      const auto type = value_type::pointer_to(pointer_kind::many, p_call.fields[0]);
      if (name == "alloc") {
        expect(arguments[0], value_type::integer(), "How many items");
        return type;
      }
      expect_pointer(arguments[0]);
      if (!can_assign(arguments[0].type, type))
        fail(arguments[0].location, "This must be " + describe_in_english(type) +
                                        " but is " +
                                        describe_in_english(arguments[0].type) + ".");
      expect(arguments[1], value_type::integer(), "How many items it held");
      expect(arguments[2], value_type::integer(), "How many items it holds now");
      return type;
    }
    if (name == "load") {
      expect_pointer(arguments[0]);
      return pointee_value(pointee_of(p_call, p_call.fields[0]));
    }
    if (name == "view") {
      expect_pointer(arguments[0]);
      expect(arguments[1], value_type::integer(), "How many items");
      const auto pointee = pointee_of(p_call, p_call.fields[0]);
      if (pointee.kind == pointee_type::kind_type::number)
        return value_type::list_of(c_number_item(pointee.number));
      if (pointee.kind == pointee_type::kind_type::structure ||
          pointee.value.is_pointer() || pointee.value.base == base_type::memory)
        fail(p_call.location, "An array holds numbers, text or records' "
                              "pointers, not this.");
      return value_type::list_of(pointee.value);
    }
    if (name == "store") {
      expect_pointer(arguments[0]);
      const auto pointee = pointee_of(p_call, p_call.fields[0]);
      if (pointee.kind == pointee_type::kind_type::structure)
        fail(p_call.location, "A whole record can't be stored at an address; "
                              "set its fields.");
      expect(arguments[1], pointee_value(pointee), "The value stored");
      return value_type::nothing();
    }
    if (name == "offset") {
      expect_pointer(arguments[0]);
      expect(arguments[1], value_type::integer(), "How far to move");
      if (!p_call.fields.empty())
        pointee_of(p_call, p_call.fields[0]);
      else if (!arguments[0].type.is_typed_pointer())
        fail(p_call.location, "Say what to count in (bytes, int32s…): the "
                              "pointer doesn't say what it points at.");
      return arguments[0].type;
    }
    // cast: between pointers, records and text, and a pointer's address as
    // a whole number.
    const auto &to = *p_call.type_argument;
    const auto &from = arguments[0].type;
    const auto is_address = [](const value_type &p_type) {
      return !p_type.is_list && (p_type.base == base_type::pointer ||
                                 p_type.base == base_type::string ||
                                 p_type.base == base_type::record);
    };
    if (to.is_typed_pointer() || (to.base == base_type::record && !to.is_list))
      check_type_exists(to.is_typed_pointer() ? value_type::pointer() : to,
                        p_call.location);
    if (to.is_typed_pointer())
      pointee_of(p_call, to.pointee);
    const bool allowed =
        (is_address(to) && !to.is_record() &&
         (is_address(from) || from.is_list || from == value_type::integer())) ||
        (to.is_record() && is_address(from)) ||
        (to == value_type::integer() && is_address(from)) ||
        (to.base == base_type::pointer && !to.is_list && from.is_list);
    // Text is bytes: only an array of them reads as text.
    const bool bytes_as_text =
        to != value_type::string() || !from.is_list ||
        (from.is_c_array() && (from.pointee == "u8" || from.pointee == "i8"));
    if (!allowed || !bytes_as_text)
      fail(p_call.location, "Can't change " + describe_in_english(from) +
                                " into " + describe_in_english(to) + ".");
    return to;
  }

  // (ref X): the address of a variable, a field or a list item, a typed
  // pointer like any other value (references.md §6). Given straight to a C
  // function, a variable's address is C's pointer to it, converted through
  // a temporary when the sizes differ.
  auto check_ref(expression &p_ref, bool p_is_allowed = false) -> value_type {
    if (!p_is_allowed) {
      if (p_ref.arguments.size() != 1)
        fail(p_ref.location, "ref needs a variable, a field or a list item.");
      auto &target = p_ref.arguments[0];
      const bool is_place =
          target.kind == expression::kind_type::variable ||
          (target.kind == expression::kind_type::call &&
           (target.text == "field" || target.text == "get"));
      if (!is_place)
        fail(p_ref.location, "ref needs a variable, a field or a list item.");
      check_expression(target);
      if (target.kind == expression::kind_type::variable &&
          lookup(target.text) == nullptr)
        fail(p_ref.location, "ref needs a variable; '" + target.text +
                                 "' is not one.");
      // A truth value in a list is kept in a byte; a C field as C keeps it.
      if (target.text == "get" && target.type == value_type::boolean())
        return value_type::pointer_to(pointer_kind::one, "u8");
      if (target.text == "field" && target.arguments[0].type.is_record())
        if (const auto record = records.find(target.arguments[0].type.record);
            record != records.end() && record->second->is_c) {
          const auto *const field = record->second->find(target.fields[0]);
          if (field != nullptr && field->storage)
            return value_type::pointer_to(
                pointer_kind::one,
                c_storage_name(*field->storage));
          if (field != nullptr && field->text_length > 0)
            return value_type::pointer_to(pointer_kind::many, "u8");
          if (field != nullptr && field->inline_record)
            return value_type::pointer_to(pointer_kind::one,
                                          "(struct " + field->type.record + ")");
        }
      return value_type::pointer_to(pointer_kind::one, target.type.to_string());
    }
    if (p_ref.arguments.size() != 1 ||
        p_ref.arguments[0].kind != expression::kind_type::variable)
      fail(p_ref.location, "ref needs a variable's name.");
    check_expression(p_ref.arguments[0]);
    if (lookup(p_ref.arguments[0].text) == nullptr)
      fail(p_ref.location, "ref needs a variable; '" +
                               p_ref.arguments[0].text + "' is not one.");
    return value_type::pointer();
  }

  auto check_c_call(expression &p_call, const c_function &p_callee)
      -> value_type {
    auto &arguments = p_call.arguments;
    const auto count = p_callee.parameters.size();
    check_count(p_call, count, p_callee.is_variadic ? SIZE_MAX : count);
    for (std::size_t index = 0; index < count; ++index) {
      const auto parameter = p_callee.parameters[index];
      auto &argument = arguments[index];
      const auto what = "Argument " + std::to_string(index + 1) + " of '" +
                        p_callee.name + "'";
      // C pointers and text are both addresses: text can go where a data
      // pointer is expected, and null where text is.
      if ((parameter == c_type::pointer_type &&
           argument.type == value_type::string()) ||
          (parameter == c_type::string_type &&
           argument.type == value_type::pointer()))
        continue;
      const auto pointee = index < p_callee.pointees.size()
                               ? p_callee.pointees[index]
                               : std::nullopt;
      // A list or array gives C its items (references.md §4), when they are
      // what C keeps there: `double *` takes a list of decimals, `char *` an
      // array of bytes, `void *` anything.
      if (argument.type.is_list && argument.type.inner_lists == 0 &&
          (parameter == c_type::pointer_type ||
           parameter == c_type::string_type)) {
        const auto items = argument.type.is_c_array()
                               ? argument.type.pointee
                               : argument.type.element().to_string();
        const bool fits =
            parameter == c_type::string_type
                ? items == "u8" || items == "i8"
                : !pointee || items == c_storage_name(*pointee);
        if (fits)
          continue;
        fail(argument.location,
             what + " takes " +
                 (parameter == c_type::string_type
                      ? string_type("text or an array of bytes")
                      : "an array of " + c_storage_name(*pointee)) +
                 ", not " + describe_in_english(argument.type) + ".");
      }
      // A struct the header defines: its record, or a pointer (null).
      const auto wanted = c_parameter_type(p_callee, index);
      if ((wanted.is_record() || wanted.is_phrase()) &&
          argument.type == value_type::pointer())
        continue;
      // An Ovis phrase where C wants a function: called through a C
      // function written for it, when the types are the same.
      if (wanted.is_phrase() && argument.type.kind == pointer_kind::phrase &&
          argument.type.is_phrase()) {
        if (argument.kind != expression::kind_type::call ||
            argument.text != "phrase-of")
          fail(argument.location,
               what + " is a C function: give the phrase by its name here "
                      "(Ovis writes a C function for each phrase it gives C).");
        if (!phrase_fits_c(argument.type, wanted))
          fail(argument.location,
               what + " is " + describe_in_english(wanted) + ", not " +
                   describe_in_english(argument.type) + ".");
        continue;
      }
      expect(argument, wanted, what);
      if (argument.text == "ref" &&
          argument.kind == expression::kind_type::call && pointee) {
        const auto &variable = argument.arguments[0];
        const auto wanted = ovis_type_of(*pointee);
        // A variable of the C number's own size is passed as it is.
        if (variable.type != wanted &&
            variable.type != value_type::number(*pointee))
          fail(variable.location,
               what + " writes " + describe_in_english(wanted) + " into '" +
                   variable.text + "', which holds " +
                   describe_in_english(variable.type) + ".");
      }
    }
    for (std::size_t index = count; index < arguments.size(); ++index) {
      const auto &type = arguments[index].type;
      if (type.is_list || type == value_type::nothing() || type.is_record())
        fail(arguments[index].location,
             "Cannot pass " + describe_in_english(type) + " to '" + p_callee.name +
                 "'.");
    }
    if (p_callee.format_index)
      check_format(p_call, p_callee);
    return c_result_type(p_callee);
  }

  // Checks printf-style calls whose format is written out.
  auto check_format(expression &p_call, const c_function &p_callee) -> void {
    const auto &format = p_call.arguments[*p_callee.format_index];
    if (format.kind != expression::kind_type::string)
      return;
    const auto conversions = parse_format(format.text);
    std::size_t expected = 0;
    for (const auto &conversion : conversions)
      expected += conversion.takes_star_width ? 2 : 1;
    const auto given = p_call.arguments.size() - p_callee.parameters.size();
    if (given != expected)
      fail(format.location, "The format \"" + format.text + "\" needs " +
                                std::to_string(expected) +
                                " value(s) but got " + std::to_string(given) +
                                ".");

    auto argument = p_callee.parameters.size();
    for (const auto &conversion : conversions) {
      if (conversion.takes_star_width) {
        expect(p_call.arguments[argument++], value_type::integer(),
               "The width given with *");
      }
      auto &value = p_call.arguments[argument++];
      const auto spec = format.text.substr(conversion.begin,
                                           conversion.end - conversion.begin);
      const auto letter = conversion.conversion;
      if (string_view_type("diouxXc").contains(letter)) {
        if (value.type.is_decimal())
          add_note(value.location, spec + " shows a whole number, so the "
                                          "float is rounded toward zero.");
        else if (!value.type.is_whole() &&
                 value.type != value_type::boolean())
          fail(value.location, spec + " needs a number but got " +
                                   describe_in_english(value.type) + ".");
      } else if (string_view_type("fFeEgGaA").contains(letter)) {
        if (value.type.is_whole())
          add_note(value.location,
                   spec + " shows a float, so the int is converted.");
        else if (!value.type.is_decimal())
          fail(value.location, spec + " needs a number but got " +
                                   describe_in_english(value.type) + ".");
      } else if (letter == 's') {
        expect(value, value_type::string(), spec + "'s value");
      } else if (letter == 'p') {
        if (!value.type.is_pointer() &&
            value.type != value_type::string())
          fail(value.location, "%p needs a pointer.");
      } else {
        fail(format.location, "The format conversion " + spec +
                                  " is not supported.");
      }
    }
  }

  auto add_note(source_location p_location, string_type p_message) -> void {
    if (notes != nullptr)
      notes->push_back({unit_index, p_location, std::move(p_message)});
  }

  auto check_expression(expression &p_expression) -> void {
    using kind = expression::kind_type;
    switch (p_expression.kind) {
    case kind::integer:
      // Above the largest signed one: only a uint64 holds it.
      p_expression.type = p_expression.is_big
                              ? value_type::number(c_type::size_type)
                              : value_type::integer();
      break;
    case kind::real:
      p_expression.type = value_type::real();
      break;
    case kind::boolean:
      p_expression.type = value_type::boolean();
      break;
    case kind::string:
      p_expression.type = value_type::string();
      break;
    case kind::variable:
      if (const auto *const type = lookup(p_expression.text)) {
        p_expression.type = *type;
        if (type->nullable && narrowed.contains(p_expression.text))
          p_expression.type = type->without_nothing();
        break;
      }
      if (const auto *const constant = find_constant(target, p_expression.text)) {
        using constant_kind = c_constant::kind_type;
        p_expression.type =
            constant->kind == constant_kind::real ? value_type::real()
            : constant->kind == constant_kind::null ||
                    p_expression.text.starts_with("std")
                ? value_type::pointer()
                : value_type::integer();
        break;
      }
      if (functions.contains(p_expression.text))
        fail(p_expression.location,
             "'" + p_expression.text + "' is a function; call it as " +
                 p_expression.text + "(...).");
      fail(p_expression.location,
           "Unknown variable '" + p_expression.text +
               "'. Known variables: " + known_variables() + ".");
    case kind::call:
      p_expression.type = check_call(p_expression);
      break;
    }
  }

  auto check_condition(expression &p_condition) -> void {
    check_expression(p_condition);
    expect(p_condition, value_type::boolean(), "The condition");
  }

  auto check_block(std::vector<statement> &p_body) -> void {
    scopes.emplace_back();
    for (auto &statement : p_body)
      check_statement(statement);
    scopes.pop_back();
  }

  auto check_loop_body(std::vector<statement> &p_body) -> void {
    ++loop_depth;
    check_block(p_body);
    --loop_depth;
  }

  auto check_types_known(const function &p_function) -> void {
    const auto is_open = [](const value_type &p_type) {
      return !p_type.is_known();
    };
    const auto is_missing = [&](const value_type &p_type) {
      return !type_exists(p_type);
    };
    for (const auto &parameter : p_function.parameters)
      if (is_open(parameter.type))
        report(p_function.location, "The type of parameter '" +
                                        parameter.name + "' is not known.");
      else if (is_missing(parameter.type))
        report(p_function.location, "There is no record called '" +
                                        parameter.type.record + "'.");
    if (is_open(p_function.return_type))
      report(p_function.location, "The return type of '" + p_function.name +
                                      "' is not known.");
    else if (is_missing(p_function.return_type))
      report(p_function.location, "There is no record called '" +
                                      p_function.return_type.record + "'.");
  }

  auto check_let(statement &p_statement) -> void {
    if (p_statement.declared_type && !p_statement.declared_type->is_known())
      fail(p_statement.location,
           "The type of '" + p_statement.name + "' is not known.");
    if (p_statement.declared_type)
      check_type_exists(*p_statement.declared_type, p_statement.location);
    value_type type = p_statement.declared_type.value_or(value_type::integer());
    try {
      if (!p_statement.values.empty()) {
        auto &value = p_statement.values.front();
        check_expression(value);
        if (!p_statement.declared_type)
          type = value.type;
        else
          expect(value, type, "The value of '" + p_statement.name + "'");
        if (type == value_type::nothing())
          fail(value.location, "'" + value.text +
                                   "' gives back nothing, so it has no value "
                                   "to use here.");
      }
    } catch (...) {
      // Still declare it so later uses don't produce follow-up errors.
      declare(p_statement.name, type);
      throw;
    }

    // Writing `let` for a variable that already exists is treated as an
    // assignment, pseudo-code tends to do that.
    const auto *const existing =
        at_top_level() ? (globals.contains(p_statement.name)
                              ? &globals[p_statement.name]
                              : nullptr)
                       : (scopes.back().contains(p_statement.name)
                              ? &scopes.back()[p_statement.name]
                              : nullptr);
    if (!p_statement.values.empty()) {
      remember_records(p_statement.name, p_statement.values.front());
      narrow_after_set(p_statement.name, p_statement.values.front().type);
    } else {
      known_records.erase(p_statement.name);
      narrowed.erase(p_statement.name);
    }
    if (existing != nullptr && !p_statement.values.empty() &&
        can_assign(type, *existing)) {
      p_statement.kind = statement::kind_type::set;
      return;
    }
    if (existing != nullptr)
      fail(p_statement.location, "'" + p_statement.name +
                                     "' already holds " +
                                     describe_in_english(*existing) + ".");
    p_statement.declared_type = type;
    declare(p_statement.name, type);
  }

  auto check_statement(statement &p_statement) -> void {
    try {
      check_statement_unguarded(p_statement);
    } catch (const check_failure &p_failure) {
      report(p_failure.location, p_failure.message);
    }
  }

  auto check_statement_unguarded(statement &p_statement) -> void {
    using kind = statement::kind_type;
    auto &values = p_statement.values;
    switch (p_statement.kind) {
    case kind::let:
      check_let(p_statement);
      break;
    case kind::set: {
      check_expression(values[0]);
      const auto *const type = lookup(p_statement.name);
      if (type == nullptr) {
        // Assigning to an unknown name declares it.
        p_statement.kind = kind::let;
        p_statement.declared_type = values[0].type;
        declare(p_statement.name, values[0].type);
        break;
      }
      expect(values[0], *type, "The new value of '" + p_statement.name + "'");
      remember_records(p_statement.name, values[0]);
      narrow_after_set(p_statement.name, values[0].type);
      break;
    }
    case kind::put: {
      const auto *const type = lookup(p_statement.name);
      if (type == nullptr || !type->is_list)
        fail(p_statement.location,
             "'" + p_statement.name + "' is not a list variable.");
      check_expression(values[0]);
      check_expression(values[1]);
      expect(values[0], value_type::integer(), "The index");
      expect(values[1], type->element(), "The stored value");
      known_records.erase(p_statement.name);
      break;
    }
    case kind::print:
      for (auto &value : values) {
        check_expression(value);
        if (value.type == value_type::nothing())
          fail(value.location, "'" + value.text +
                                   "' gives back nothing, so it has no value "
                                   "to use here.");
        if (value.type.is_pointer())
          fail(value.location, "A pointer cannot be printed.");
        if (value.type == value_type::memory())
          fail(value.location, "A memory cannot be printed.");
        if (value.type.is_variable())
          expect_trait(value.type, "showable", value.location, "Showing it");
        show_in_copy(value);
      }
      break;
    case kind::with_memory:
      if (!values.empty()) {
        check_expression(values[0]);
        expect(values[0], value_type::memory(), "What 'with' works in");
      }
      check_block(p_statement.body);
      break;
    case kind::if_: {
      check_condition(values[0]);
      const auto when_true = narrowing(values[0], true);
      const auto when_false = narrowing(values[0], false);
      const auto before = narrowed;
      narrowed.insert(when_true.begin(), when_true.end());
      check_block(p_statement.body);
      narrowed = before;
      narrowed.insert(when_false.begin(), when_false.end());
      check_block(p_statement.else_body);
      narrowed = before;
      // `if x is nothing: give back` — after it, x isn't.
      if (always_leaves(p_statement.body))
        narrowed.insert(when_false.begin(), when_false.end());
      if (always_leaves(p_statement.else_body))
        narrowed.insert(when_true.begin(), when_true.end());
      break;
    }
    case kind::while_: {
      // What the body sets may be nothing on the next pass; the condition
      // is read again before each.
      std::set<string_type> changed;
      assigned_in(p_statement.body, changed);
      for (const auto &name : changed)
        narrowed.erase(name);
      check_condition(values[0]);
      const auto before = narrowed;
      const auto when_true = narrowing(values[0], true);
      narrowed.insert(when_true.begin(), when_true.end());
      check_loop_body(p_statement.body);
      narrowed = before;
      break;
    }
    case kind::for_:
      check_expression(values[0]);
      check_expression(values[1]);
      expect(values[0], value_type::integer(), "The first value of a for loop");
      expect(values[1], value_type::integer(), "The last value of a for loop");
      scopes.push_back({{p_statement.name, value_type::integer()}});
      check_loop_body(p_statement.body);
      scopes.pop_back();
      break;
    case kind::each:
      check_expression(values[0]);
      if (!values[0].type.is_list && !values[0].type.is_map())
        fail(values[0].location, "There is no list to go through here.");
      // A map is gone through by its keys.
      scopes.push_back({{p_statement.name, values[0].type.is_map()
                                               ? values[0].type.map_key()
                                               : values[0].type.element()}});
      if (const auto records = records_in(values[0]))
        known_records[p_statement.name] = *records;
      else
        known_records.erase(p_statement.name);
      check_loop_body(p_statement.body);
      scopes.pop_back();
      known_records.erase(p_statement.name);
      break;
    case kind::defer: {
      // A deferred block runs while leaving, so it may not leave early; a
      // loop entirely inside it may still use stop and skip.
      const auto outer_floor = defer_loop_floor;
      defer_loop_floor = loop_depth;
      ++defer_depth;
      check_block(p_statement.body);
      --defer_depth;
      defer_loop_floor = outer_floor;
      break;
    }
    case kind::block: {
      std::optional<value_type> gives;
      if (!p_statement.name.empty()) {
        const auto *const type = lookup(p_statement.name);
        if (type == nullptr)
          fail(p_statement.location,
               "'" + p_statement.name + "' is not a variable.");
        gives = *type;
      }
      block_frames.push_back({p_statement.label, gives, defer_depth});
      check_block(p_statement.body);
      block_frames.pop_back();
      if (!p_statement.name.empty()) {
        known_records.erase(p_statement.name);
        narrowed.erase(p_statement.name);
      }
      if (gives && !always_leaves(p_statement.body))
        fail(p_statement.location,
             "This block gives '" + p_statement.name +
                 "' its value, so it must end by leaving with one: `leave "
                 "with VALUE` as its last line.");
      break;
    }
    case kind::leave: {
      const auto target = std::find_if(
          block_frames.rbegin(), block_frames.rend(), [&](const auto &p_block) {
            return p_statement.label.empty() || p_block.label == p_statement.label;
          });
      if (target == block_frames.rend())
        fail(p_statement.location,
             p_statement.label.empty()
                 ? string_type("'leave' only works inside a block ('do:').")
                 : "There is no block labelled ':" + p_statement.label +
                       "' around this.");
      if (target->defer_depth != defer_depth)
        fail(p_statement.location,
             "An 'at the end' block can't leave the block around it; it runs "
             "while leaving.");
      if (!target->gives) {
        if (!values.empty())
          fail(p_statement.location,
               "This block gives no value, so 'leave' can't give one.");
        break;
      }
      if (values.empty())
        fail(p_statement.location,
             "This block gives a value: leave it with one (`leave with "
             "VALUE`).");
      check_expression(values[0]);
      expect(values[0], *target->gives, "The block's value");
      break;
    }
    case kind::break_:
    case kind::continue_:
      if (loop_depth == 0)
        fail(p_statement.location, "break and continue only work in a loop.");
      if (defer_depth != 0 && loop_depth == defer_loop_floor)
        fail(p_statement.location,
             "An 'at the end' block can't leave the loop around it; it runs "
             "while leaving.");
      break;
    case kind::return_: {
      if (defer_depth != 0)
        fail(p_statement.location,
             "An 'at the end' block can't give back a value; it runs while "
             "leaving.");
      const auto expected = current_function != nullptr
                                ? current_function->return_type
                                : value_type::integer();
      if (values.empty()) {
        if (current_function != nullptr && expected != value_type::nothing())
          fail(p_statement.location,
               "'" + current_function->name + "' must give back " +
                   describe_in_english(expected) + ".");
        break;
      }
      check_expression(values[0]);
      if (expected == value_type::nothing())
        fail(p_statement.location, "'" + current_function->name +
                                       "' gives nothing back, so it cannot "
                                       "give back a value.");
      expect(values[0], expected, "The returned value");
      break;
    }
    case kind::evaluate:
      if (values[0].kind != expression::kind_type::call)
        fail(p_statement.location, "This value is not used; did you mean to "
                                   "show it?");
      check_expression(values[0]);
      break;
    case kind::set_field: {
      check_expression(values[0]);
      check_expression(values[1]);
      const auto &declaration = record_of(values[0], "Setting a field");
      const auto &field =
          field_of(declaration, p_statement.name, p_statement.location);
      if (!fits_c_field(declaration, field, values[1]))
        expect(values[1], field_type(declaration, field, values[0].type),
               "The new value of '" + field.name + "'");
      break;
    }
    case kind::pass:
      break;
    }
  }

  auto check_record(const record &p_record) -> void {
    std::set<string_type> names;
    for (const auto &field : p_record.fields) {
      if (!names.insert(field.name).second)
        report(p_record.location, "The field '" + field.name + "' of '" +
                                      p_record.name + "' is listed twice.");
      if (!field.type.is_known() ||
          (field.type.is_list && field.type.base == base_type::unknown))
        report(p_record.location, "The type of the field '" + field.name +
                                      "' of '" + p_record.name +
                                      "' is not known.");
      else if (!type_exists(field.type))
        report(p_record.location,
               "There is no record called '" + field.type.record + "'.");
    }
  }

  // Traits and impls. Names first, so types may name a trait.
  auto register_traits() -> void {
    for (unit_index = 0; unit_index < target.units.size(); ++unit_index) {
      for (const auto &trait : target.units[unit_index].traits) {
        if (records.contains(trait.name))
          report(trait.location, "'" + trait.name +
                                     "' is a record, so it can't also be a "
                                     "trait.");
        else if (!traits.emplace(trait.name, &trait).second)
          report(trait.location,
                 "The trait '" + trait.name + "' is defined twice.");
      }
    }
    for (unit_index = 0; unit_index < target.units.size(); ++unit_index) {
      for (const auto &impl : target.units[unit_index].impls) {
        if (!traits.contains(impl.trait))
          report(impl.location, "There is no trait called '" + impl.trait + "'.");
        else if (const auto plain = value_type::parse(read_sexprs(impl.type).front());
                 plain && !plain->is_record()) {
          // A number or text has the trait statically (generics.md §4): its
          // phrases are called directly, as a bound's are, but it isn't a
          // value of the trait (that would need it boxed).
          if (!plain->is_numeric() && *plain != value_type::string() &&
              *plain != value_type::boolean())
            report(impl.location, "Only records, numbers, text and truth values "
                                  "can implement a trait, not " +
                                      describe_in_english(*plain) + ".");
          else if (!impls.emplace(impl_key(impl.trait, impl.type), &impl).second)
            report(impl.location, describe_in_english(*plain) + " implements '" +
                                      impl.trait + "' twice.");
        } else if (!records.contains(impl.type))
          report(impl.location, "There is no record called '" + impl.type +
                                    "' to implement '" + impl.trait + "'.");
        else if (records.at(impl.type)->is_c)
          // Dispatch reads a hidden tag that C's structs don't have.
          report(impl.location, "'" + impl.type + "' is a C struct, which can't "
                                "implement a trait; make a record that holds "
                                "it.");
        else if (records.at(impl.type)->is_packed)
          report(impl.location, "'" + impl.type + "' is packed: its memory is "
                                "only its fields, so it can't implement a "
                                "trait; make a record that holds it.");
        else if (!impls.emplace(impl_key(impl.trait, impl.type), &impl).second)
          report(impl.location, "'" + impl.type + "' implements '" + impl.trait +
                                    "' twice.");
      }
    }
  }

  // After the functions are known: the methods' names and signatures, and
  // that every impl is whole.
  auto check_traits() -> void {
    const auto same_signature =
        [&](const function &p_function, const trait_method &p_method,
            const value_type &p_self) {
          if (p_function.parameters.size() != p_method.parameters.size()) {
            report(p_function.location,
                   "'" + p_method.name + "' takes " +
                       std::to_string(p_method.parameters.size()) +
                       " argument(s) in its trait but " +
                       std::to_string(p_function.parameters.size()) + " here.");
            return;
          }
          for (std::size_t index = 0; index < p_method.parameters.size();
               ++index) {
            const auto expected = index == 0 || p_method.is_same(index)
                                      ? p_self
                                      : p_method.parameters[index].type;
            if (p_function.parameters[index].type != expected)
              report(p_function.location,
                     "Argument '" + p_function.parameters[index].name +
                         "' of '" + p_method.name + "' must be " +
                         describe_in_english(expected) + " but is " +
                         describe_in_english(p_function.parameters[index].type) +
                         ".");
          }
          if (p_function.return_type != p_method.return_type)
            report(p_function.location,
                   "'" + p_method.name + "' must give back " +
                       describe_in_english(p_method.return_type) + " but gives " +
                       describe_in_english(p_function.return_type) + ".");
        };

    for (unit_index = 0; unit_index < target.units.size(); ++unit_index) {
      for (const auto &trait : target.units[unit_index].traits) {
        if (const auto found = traits.find(trait.name);
            found == traits.end() || found->second != &trait)
          continue;
        for (const auto &required : trait.requires_traits)
          if (!traits.contains(required))
            report(trait.location, "There is no trait called '" + required +
                                       "' for '" + trait.name + "' to require.");
        for (const auto &method : trait.methods) {
          if (is_builtin(method.name))
            report(trait.location, "'" + method.name +
                                       "' is built in; a trait can't use it.");
          else if (const auto [owner, added] =
                       method_owner.emplace(method.name, &trait);
                   !added)
            report(trait.location,
                   "'" + method.name + "' is a phrase of both '" +
                       owner->second->name + "' and '" + trait.name + "'.");
          const auto function = functions.find(method.name);
          if (method.has_default) {
            if (function != functions.end() && function->second.declaration)
              same_signature(*function->second.declaration, method,
                             value_type::record_of(trait.name));
          } else if (function != functions.end()) {
            report(trait.location, "'" + method.name +
                                       "' is also defined as a phrase of its "
                                       "own.");
          }
        }
      }
    }
    for (unit_index = 0; unit_index < target.units.size(); ++unit_index) {
      for (const auto &impl : target.units[unit_index].impls) {
        const auto found = impls.find(impl_key(impl.trait, impl.type));
        if (found == impls.end() || found->second != &impl)
          continue;
        const auto &owner = *traits.at(impl.trait);
        std::vector<string_type> missing;
        // A built-in trait's phrase a trait requires (`comes_before`) may be
        // the record's own function, written outside the impl.
        for (const auto &method : owner.methods)
          if (!method.has_default &&
              std::ranges::find(impl.methods, method.name) == impl.methods.end() &&
              !functions.contains(impl_function_name(method.name, impl.type)))
            missing.push_back(method.name);
        for (const auto &required : owner.requires_traits)
          if (traits.contains(required) &&
              !impls.contains(impl_key(required, impl.type)))
            report(impl.location, "'" + impl.type + "' is " + impl.trait +
                                      ", which must also be " + required +
                                      ": write 'a " + impl.type + " is a " +
                                      required + ":' too.");
        if (!missing.empty()) {
          // A number or text is said as one: "a whole number".
          const auto plain = value_type::parse(read_sexprs(impl.type).front());
          auto subject = plain && !plain->is_record()
                             ? describe_in_english(*plain)
                             : "'" + impl.type + "'";
          subject.front() = static_cast<char_type>(
              std::toupper(static_cast<unsigned char>(subject.front())));
          report(impl.location, subject + " doesn't implement '" + impl.trait +
                                    "' yet; it still needs: " + quoted(missing) +
                                    ".");
        }
        for (const auto &name : impl.methods) {
          const auto *const method = owner.find(name);
          if (method == nullptr) {
            report(impl.location, "'" + impl.trait + "' has no phrase '" + name +
                                      "'. Its phrases are: " + [&] {
                                        std::vector<string_type> names;
                                        for (const auto &each : owner.methods)
                                          names.push_back(each.name);
                                        return quoted(names);
                                      }() + ".");
            continue;
          }
          const auto function =
              functions.find(impl_function_name(name, impl.type));
          // For every stack: a stack of its type variables.
          auto self = value_type::record_of(impl.type);
          if (const auto plain = value_type::parse(read_sexprs(impl.type).front());
              plain && !plain->is_record())
            self = *plain;
          if (!impl.type_parameters.empty()) {
            std::vector<value_type> variables;
            for (const auto &parameter : impl.type_parameters)
              variables.push_back(value_type::variable(parameter.name));
            self = value_type::record_of(impl.type, variables);
          }
          if (function != functions.end())
            same_signature(*function->second.declaration, *method, self);
        }
      }
    }
  }

  // Each use of a generic record with an impl for every use gets that
  // impl's phrases, whose bounds its types meet, so a value of the trait
  // holding it dispatches to them (G2). Returns whether any were made.
  auto copy_generic_impls() -> bool {
    std::map<string_type, value_type> uses;
    const auto visit = [&](value_type &p_type) {
      if (p_type.base != base_type::record || p_type.arguments.empty() ||
          p_type.has_variables())
        return;
      auto own = value_type::record_of(p_type.record, p_type.record_arguments());
      uses.emplace(own.to_string(), own);
    };
    for (auto &unit : target.units) {
      for (auto &function : unit.functions)
        if (!function.is_generic())
          for_each_type(function, visit);
      for_each_type(unit.statements, visit);
    }
    for (auto &copy : copies)
      for_each_type(copy, visit);
    bool made = false;
    const auto saved = current_function;
    current_function = nullptr;
    for (const auto &[text, use] : uses)
      for (const auto &[key, each] : impls) {
        if (each->type != use.record || each->type_parameters.empty() ||
            !impl_bounds_met(*each, use))
          continue;
        const auto types = use.record_arguments();
        type_bindings bindings;
        for (std::size_t index = 0;
             index < std::min(types.size(), each->type_parameters.size()); ++index)
          bindings[each->type_parameters[index].name] = types[index];
        for (const auto &method : each->methods) {
          const auto generic = impl_function_name(method, use.record);
          if (!generic_sources.contains(generic) ||
              functions.contains(copy_name(generic, types)))
            continue;
          make_copy(generic, types, bindings);
          made = true;
        }
      }
    current_function = saved;
    return made;
  }

  // A copy's problems are its generic's, already reported, or (a C macro
  // checked per copy, §9) said with the types it was copied for.
  auto explain_copy_problems(std::size_t p_first, const function &p_copy)
      -> void {
    std::vector<diagnostic> kept(diagnostics.begin(),
                                 diagnostics.begin() + static_cast<std::ptrdiff_t>(p_first));
    for (auto index = p_first; index < diagnostics.size(); ++index) {
      auto problem = diagnostics[index];
      const bool known = std::ranges::any_of(kept, [&](const diagnostic &p_other) {
        return p_other.unit_index == problem.unit_index &&
               p_other.location.line == problem.location.line &&
               p_other.message == problem.message;
      });
      if (known)
        continue;
      std::vector<string_type> types;
      for (const auto &type : p_copy.copied_for)
        types.push_back(describe_in_english(type));
      problem.message += " (In '" + p_copy.copy_of + "' used with " +
                         join(types) + ".)";
      kept.push_back(std::move(problem));
    }
    diagnostics = std::move(kept);
  }

  // A generic's bounds name traits there are.
  auto check_bounds_exist(const function &p_function) -> void {
    for (const auto &parameter : p_function.type_parameters) {
      if (traits.contains(parameter.name) || records.contains(parameter.name))
        report(p_function.location,
               "'" + parameter.name + "' is a " +
                   (traits.contains(parameter.name) ? "trait" : "record") +
                   "; a type variable needs a name of its own.");
      for (const auto &bound : parameter.bounds)
        if (!is_builtin_trait(bound) && !traits.contains(bound))
          report(p_function.location, "There is no trait called '" + bound +
                                          "' for '" + parameter.name +
                                          "' to have.");
    }
  }

  auto check_function(function &p_function) -> void {
    current_function = &p_function;
    known_records.clear();
    narrowed.clear();
    scopes.emplace_back();
    for (const auto &parameter : p_function.parameters)
      scopes.back()[parameter.name] = parameter.type;
    // The function's body shares the parameters' scope.
    for (auto &statement : p_function.body)
      check_statement(statement);
    scopes.pop_back();
    current_function = nullptr;
  }

public:
  checker(program &p_program, std::vector<diagnostic> *p_notes)
      : target(p_program), notes(p_notes) {}

  auto run() -> std::vector<diagnostic> {
    for (const auto &function : libc_functions())
      c_functions[function.name] = &function;
    for (unit_index = 0; unit_index < target.units.size(); ++unit_index) {
      for (const auto &function : target.units[unit_index].externs) {
        if (is_builtin(function.name))
          report(function.location, "'" + function.name +
                                        "' is built in; it cannot be extern.");
        c_functions[function.name] = &function;
      }
    }

    for (unit_index = 0; unit_index < target.units.size(); ++unit_index)
      for (const auto &record : target.units[unit_index].records)
        if (!records.emplace(record.name, &record).second)
          report(record.location,
                 "The record '" + record.name + "' is defined twice.");
    register_traits();
    for (unit_index = 0; unit_index < target.units.size(); ++unit_index)
      for (const auto &record : target.units[unit_index].records)
        check_record(record);

    for (unit_index = 0; unit_index < target.units.size(); ++unit_index)
      for (const auto &function : target.units[unit_index].functions)
        if (function.is_generic())
          generic_sources.try_emplace(function.name, function, unit_index);
    for (unit_index = 0; unit_index < target.units.size(); ++unit_index) {
      for (auto &function : target.units[unit_index].functions) {
        check_types_known(function);
        check_bounds_exist(function);
        if (function.name == "main" || is_builtin(function.name))
          report(function.location, "'" + function.name +
                                        "' is reserved; pick another name.");
        else if (!functions.emplace(function.name,
                                    function_entry{&function, unit_index})
                      .second)
          report(function.location,
                 "The function '" + function.name + "' is defined twice.");
      }
    }

    check_traits();

    scopes.emplace_back();
    for (unit_index = 0; unit_index < target.units.size(); ++unit_index)
      for (auto &statement : target.units[unit_index].statements)
        check_statement(statement);
    scopes.pop_back();

    // The functions written; copies made while checking come after.
    std::vector<function_entry> written;
    for (const auto &[name, entry] : functions)
      written.push_back(entry);
    for (const auto &entry : written) {
      unit_index = entry.unit_index;
      check_function(*entry.declaration);
    }
    // Copies made while checking, which may make more.
    do {
      for (; copies_checked < copies.size(); ++copies_checked) {
        unit_index = copy_units[copies_checked];
        const auto first = diagnostics.size();
        check_function(copies[copies_checked]);
        if (!copies[copies_checked].copy_of.empty())
          explain_copy_problems(first, copies[copies_checked]);
      }
    } while (copy_generic_impls());
    for (std::size_t index = 0; index < copies.size(); ++index)
      target.units[copy_units[index]].functions.push_back(
          std::move(copies[index]));
    // Macro copies made for copies of generic phrases: in their macro's unit.
    for (auto &copy : macro_copies)
      for (auto &unit : target.units)
        if (std::ranges::find(unit.externs, copy.macro_name, &c_function::name) !=
            unit.externs.end()) {
          unit.externs.push_back(std::move(copy));
          break;
        }
    // Codegen sees the copies, not the generics.
    if (diagnostics.empty())
      for (auto &problem : make_record_copies(target))
        diagnostics.push_back(std::move(problem));
    return std::move(diagnostics);
  }
};

} // namespace

auto check(program &p_program, std::vector<diagnostic> *p_notes)
    -> std::vector<diagnostic> {
  return checker(p_program, p_notes).run();
}

} // namespace ovis::core
