#include <algorithm>
#include <map>
#include <set>

#include <ovis/core/checker.hpp>

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
  return p_from == p_to ||
         (p_from == value_type::integer() && p_to == value_type::real());
}

auto promote(const value_type &p_left, const value_type &p_right)
    -> value_type {
  return p_left == value_type::real() || p_right == value_type::real()
             ? value_type::real()
             : value_type::integer();
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
  // Unset while checking top-level statements.
  const function *current_function = nullptr;

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
        !traits.contains(p_to.record))
      return false;
    if (impls.contains(impl_key(p_to.record, p_from.record)))
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

  auto expect(const expression &p_expression, const value_type &p_type,
              string_view_type p_what) -> void {
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
    if (!can_assign(p_expression.type, p_type) && !is_list_literal &&
        !(p_type.is_record() && is_null(p_expression)))
      fail(p_expression.location, string_type(p_what) + " must be " +
                                      describe_in_english(p_type) + " but is " +
                                      describe_in_english(p_expression.type) + ".");
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

  auto record_of(const expression &p_object, string_view_type p_what)
      -> const record & {
    if (!p_object.type.is_record())
      fail(p_object.location, string_type(p_what) + " needs a record but got " +
                                  describe_in_english(p_object.type) + ".");
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

  auto check_new(const expression &p_call) -> value_type {
    const auto &type = *p_call.type_argument;
    check_record_exists(type, p_call.location);
    const auto &declaration = *records.at(type.record);
    std::set<string_type> given;
    for (std::size_t index = 0; index < p_call.arguments.size(); ++index) {
      const auto &name = p_call.fields[index];
      const auto &field = field_of(declaration, name, p_call.location);
      if (!given.insert(name).second)
        fail(p_call.location, "The field '" + name + "' is given twice.");
      expect(p_call.arguments[index], field.type, "The field '" + name + "'");
    }
    std::vector<string_type> missing;
    for (const auto &field : declaration.fields)
      if (!given.contains(field.name))
        missing.push_back(field.name);
    if (!missing.empty())
      fail(p_call.location, "A new " + declaration.name +
                                " needs a value for every field; missing: " +
                                join(missing) + ".");
    return type;
  }

  auto expect_numeric(const expression &p_call) -> value_type {
    value_type result = value_type::integer();
    for (const auto &argument : p_call.arguments) {
      if (!argument.type.is_numeric()) {
        string_type hint;
        if (p_call.text == "+" && argument.type == value_type::string())
          hint = " To show several values, separate them with commas.";
        fail(argument.location, "'" + p_call.text +
                                    "' needs numbers but got " +
                                    describe_in_english(argument.type) + "." + hint);
      }
      result = promote(result, argument.type);
    }
    return result;
  }

  auto expect_booleans(const expression &p_call) -> void {
    for (const auto &argument : p_call.arguments)
      expect(argument, value_type::boolean(),
             "Every argument of '" + p_call.text + "'");
  }

  auto check_call(expression &p_call) -> value_type {
    if (p_call.text == "ref")
      return check_ref(p_call);
    // (ref x) is only meaningful as an argument of a C function.
    const bool is_c_call = !is_builtin(p_call.text) &&
                           !functions.contains(p_call.text) &&
                           c_functions.contains(p_call.text);
    for (auto &argument : p_call.arguments) {
      if (is_c_call && argument.kind == expression::kind_type::call &&
          argument.text == "ref")
        argument.type = check_ref(argument, true);
      else
        check_expression(argument);
    }

    const auto &name = p_call.text;
    auto &arguments = p_call.arguments;

    if (name == "new")
      return check_new(p_call);
    if (name == "field") {
      const auto &declaration = record_of(arguments[0], "A field");
      return field_of(declaration, p_call.fields[0], p_call.location).type;
    }
    if (name == "-") {
      check_count(p_call, 1, SIZE_MAX);
      return expect_numeric(p_call);
    }
    if (name == "+" || name == "concat") {
      check_count(p_call, name == "+" ? 2 : 1, SIZE_MAX);
      // Adding anything to text joins them as text.
      bool has_text = name == "concat";
      for (const auto &argument : arguments)
        has_text = has_text || argument.type == value_type::string();
      if (!has_text)
        return expect_numeric(p_call);
      for (const auto &argument : arguments)
        if (!is_printable(argument.type))
          fail(argument.location, "Cannot join " + describe_in_english(argument.type) +
                                      " to text.");
      return value_type::string();
    }
    if (name == "*" || name == "min" || name == "max") {
      check_count(p_call, 2, SIZE_MAX);
      return expect_numeric(p_call);
    }
    if (name == "/") {
      check_count(p_call, 2, SIZE_MAX);
      expect_numeric(p_call);
      return value_type::real();
    }
    if (name == "div") {
      check_count(p_call, 2, 2);
      for (const auto &argument : arguments)
        expect(argument, value_type::integer(), "Each argument of 'div'");
      return value_type::integer();
    }
    if (name == "%") {
      check_count(p_call, 2, 2);
      return expect_numeric(p_call);
    }
    if (name == "=" || name == "!=") {
      check_count(p_call, 2, 2);
      const auto &left = arguments[0].type;
      const auto &right = arguments[1].type;
      const auto is_text_or_pointer = [](const value_type &p_type) {
        return p_type == value_type::string() || p_type == value_type::pointer();
      };
      const bool is_record_and_null =
          (left.is_record() && is_null(arguments[1])) ||
          (right.is_record() && is_null(arguments[0]));
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
        expect_numeric(p_call);
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
    if (name == "float" || name == "sqrt" || name == "floor") {
      check_count(p_call, 1, 1);
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
      if (!arguments[0].type.is_list)
        fail(arguments[0].location, "'len' needs a list.");
      return value_type::integer();
    }
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
        for (const auto &argument : arguments)
          expect(argument, *p_call.type_argument, "Every item of the list");
        return value_type::list_of(*p_call.type_argument);
      }
      if (arguments.empty())
        fail(p_call.location,
             "An empty list has no element type; say what it holds, as in "
             "\"empty list of whole numbers\".");
      auto element = arguments[0].type;
      for (const auto &argument : arguments) {
        if (element.is_numeric() && argument.type.is_numeric())
          element = promote(element, argument.type);
        else if (argument.type != element)
          fail(argument.location, "All list elements must have the same type.");
      }
      if (element.is_list || element == value_type::nothing() ||
          element == value_type::pointer())
        fail(p_call.location,
             "Lists can only hold numbers, text, true and false, or records.");
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
    if (name == "free" || name == "keep") {
      check_count(p_call, 1, 1);
      const auto &type = arguments[0].type;
      const bool freeable = type.is_list || type == value_type::string() ||
                            type.is_record() ||
                            (name == "free" && (type == value_type::pointer() ||
                                                type == value_type::memory()));
      if (!freeable)
        fail(arguments[0].location,
             "'" + name + "' works on text, lists and records" +
                 (name == "free" ? ", C pointers and memories." : "."));
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
      if (name == "sort" && arguments[0].type.base != base_type::integer &&
          arguments[0].type.base != base_type::real &&
          arguments[0].type.base != base_type::string)
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
      if (!is_printable(arguments[0].type))
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
          c_found != c_functions.end())
        return check_c_call(p_call, *c_found->second);
      fail(p_call.location, "Unknown function '" + name +
                                "'. Known functions: " + known_functions() +
                                ".");
    }
    const auto &callee = *found->second.declaration;
    check_count(p_call, callee.parameters.size(), callee.parameters.size());
    for (std::size_t index = 0; index < arguments.size(); ++index)
      expect(arguments[index], callee.parameters[index].type,
             "Argument '" + callee.parameters[index].name + "' of '" + name +
                 "'");
    return callee.return_type;
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
    for (std::size_t index = 1; index < arguments.size(); ++index)
      expect(arguments[index], signature.parameters[index].type,
             "Argument '" + signature.parameters[index].name + "' of '" + name +
                 "'");
    if (!traits.contains(self.type.record)) {
      const auto &written = impls.at(impl_key(p_owner.name, self.type.record))
                                ->methods;
      if (std::ranges::find(written, name) != written.end())
        p_call.text = impl_function_name(name, self.type.record);
    }
    return signature.return_type;
  }

  // (ref NAME): the address of a variable, so a C function can write to it.
  auto check_ref(expression &p_ref, bool p_is_allowed = false) -> value_type {
    if (!p_is_allowed)
      fail(p_ref.location, "Only a C function can be given a variable with ref.");
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
      const auto &argument = arguments[index];
      const auto what = "Argument " + std::to_string(index + 1) + " of '" +
                        p_callee.name + "'";
      // C pointers and text are both addresses: text can go where a data
      // pointer is expected, and null where text is.
      if ((parameter == c_type::pointer_type &&
           argument.type == value_type::string()) ||
          (parameter == c_type::string_type &&
           argument.type == value_type::pointer()))
        continue;
      expect(argument, ovis_type_of(parameter), what);
      const auto pointee = index < p_callee.pointees.size()
                               ? p_callee.pointees[index]
                               : std::nullopt;
      if (argument.text == "ref" &&
          argument.kind == expression::kind_type::call && pointee) {
        const auto &variable = argument.arguments[0];
        const auto wanted = ovis_type_of(*pointee);
        if (variable.type != wanted)
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
    return ovis_type_of(p_callee.result);
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
      const auto &value = p_call.arguments[argument++];
      const auto spec = format.text.substr(conversion.begin,
                                           conversion.end - conversion.begin);
      const auto letter = conversion.conversion;
      if (string_view_type("diouxXc").contains(letter)) {
        if (value.type == value_type::real())
          add_note(value.location, spec + " shows a whole number, so the "
                                          "float is rounded toward zero.");
        else if (value.type != value_type::integer() &&
                 value.type != value_type::boolean())
          fail(value.location, spec + " needs a number but got " +
                                   describe_in_english(value.type) + ".");
      } else if (string_view_type("fFeEgGaA").contains(letter)) {
        if (value.type == value_type::integer())
          add_note(value.location,
                   spec + " shows a float, so the int is converted.");
        else if (value.type != value_type::real())
          fail(value.location, spec + " needs a number but got " +
                                   describe_in_english(value.type) + ".");
      } else if (letter == 's') {
        expect(value, value_type::string(), spec + "'s value");
      } else if (letter == 'p') {
        if (value.type != value_type::pointer() &&
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
      p_expression.type = value_type::integer();
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
      break;
    }
    case kind::print:
      for (auto &value : values) {
        check_expression(value);
        if (value.type == value_type::nothing())
          fail(value.location, "'" + value.text +
                                   "' gives back nothing, so it has no value "
                                   "to use here.");
        if (value.type == value_type::pointer())
          fail(value.location, "A pointer cannot be printed.");
        if (value.type == value_type::memory())
          fail(value.location, "A memory cannot be printed.");
      }
      break;
    case kind::with_memory:
      if (!values.empty()) {
        check_expression(values[0]);
        expect(values[0], value_type::memory(), "What 'with' works in");
      }
      check_block(p_statement.body);
      break;
    case kind::if_:
      check_condition(values[0]);
      check_block(p_statement.body);
      check_block(p_statement.else_body);
      break;
    case kind::while_:
      check_condition(values[0]);
      check_loop_body(p_statement.body);
      break;
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
      if (!values[0].type.is_list)
        fail(values[0].location, "There is no list to go through here.");
      scopes.push_back({{p_statement.name, values[0].type.element()}});
      check_loop_body(p_statement.body);
      scopes.pop_back();
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
      expect(values[1], field.type, "The new value of '" + field.name + "'");
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
        else if (!records.contains(impl.type))
          report(impl.location, "There is no record called '" + impl.type +
                                    "' to implement '" + impl.trait + "'.");
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
            const auto expected =
                index == 0 ? p_self : p_method.parameters[index].type;
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
        for (const auto &method : owner.methods)
          if (!method.has_default &&
              std::ranges::find(impl.methods, method.name) == impl.methods.end())
            missing.push_back(method.name);
        if (!missing.empty())
          report(impl.location, "'" + impl.type + "' doesn't implement '" +
                                    impl.trait + "' yet; it still needs: " +
                                    quoted(missing) + ".");
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
          if (function != functions.end())
            same_signature(*function->second.declaration, *method,
                           value_type::record_of(impl.type));
        }
      }
    }
  }

  auto check_function(function &p_function) -> void {
    current_function = &p_function;
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

    for (unit_index = 0; unit_index < target.units.size(); ++unit_index) {
      for (auto &function : target.units[unit_index].functions) {
        check_types_known(function);
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

    for (const auto &[name, entry] : functions) {
      unit_index = entry.unit_index;
      check_function(*entry.declaration);
    }
    return std::move(diagnostics);
  }
};

} // namespace

auto check(program &p_program, std::vector<diagnostic> *p_notes)
    -> std::vector<diagnostic> {
  return checker(p_program, p_notes).run();
}

} // namespace ovis::core
