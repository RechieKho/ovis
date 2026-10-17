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

auto is_printable(const value_type &p_type) -> bool {
  return !p_type.is_list && p_type.base != base_type::nothing &&
         p_type.base != base_type::pointer && p_type.is_known();
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
  std::map<string_type, value_type> globals;
  std::vector<std::map<string_type, value_type>> scopes;
  std::size_t unit_index = 0;
  std::size_t loop_depth = 0;
  // Unset while checking top-level statements.
  const function *current_function = nullptr;

  [[noreturn]] static auto fail(source_location p_location,
                                string_type p_message) -> void {
    throw check_failure{p_location, std::move(p_message)};
  }

  auto report(source_location p_location, string_type p_message) -> void {
    diagnostics.push_back({unit_index, p_location, std::move(p_message)});
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
        names.insert(name + " " + type.to_string());
    for (const auto &[name, type] : globals)
      names.insert(name + " " + type.to_string());
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

  auto expect(const expression &p_expression, const value_type &p_type,
              string_view_type p_what) -> void {
    if (!is_assignable(p_expression.type, p_type))
      fail(p_expression.location, string_type(p_what) + " must be " +
                                      p_type.to_string() + " but is " +
                                      p_expression.type.to_string() + ".");
  }

  auto expect_numeric(const expression &p_call) -> value_type {
    value_type result = value_type::integer();
    for (const auto &argument : p_call.arguments) {
      if (!argument.type.is_numeric()) {
        string_type hint;
        if (p_call.text == "+" && argument.type == value_type::string())
          hint = " Text cannot be added; to show several values use "
                 "(print a b c) instead.";
        fail(argument.location, "'" + p_call.text +
                                    "' needs numbers but got " +
                                    argument.type.to_string() + "." + hint);
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
          fail(argument.location, "Cannot join " + argument.type.to_string() +
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
      if (!(left.is_numeric() && right.is_numeric()) &&
          !(left == right && !left.is_list &&
            left.base != base_type::nothing) &&
          !(is_text_or_pointer(left) && is_text_or_pointer(right)))
        fail(p_call.location, "Cannot compare " + left.to_string() +
                                  " with " + right.to_string() + ".");
      return value_type::boolean();
    }
    if (name == "<" || name == "<=" || name == ">" || name == ">=") {
      check_count(p_call, 2, 2);
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
        for (const auto &argument : arguments)
          expect(argument, *p_call.type_argument, "Every item of the list");
        return value_type::list_of(p_call.type_argument->base);
      }
      if (arguments.empty())
        fail(p_call.location,
             "An empty list has no element type; use (array int 0) instead.");
      auto element = arguments[0].type;
      for (const auto &argument : arguments) {
        if (element.is_numeric() && argument.type.is_numeric())
          element = promote(element, argument.type);
        else if (argument.type != element)
          fail(argument.location, "All list elements must have the same type.");
      }
      if (element.is_list || element == value_type::nothing() ||
          element == value_type::pointer())
        fail(p_call.location, "Lists can only hold int, float, bool or str.");
      return value_type::list_of(element.base);
    }
    if (name == "array") {
      check_count(p_call, 1, 1);
      expect(arguments[0], value_type::integer(), "The array size");
      return value_type::list_of(p_call.type_argument->base);
    }
    if (name == "append") {
      check_count(p_call, 2, 2);
      if (!arguments[0].type.is_list)
        fail(arguments[0].location, "'append' needs a list as first argument.");
      expect(arguments[1], arguments[0].type.element(), "The appended value");
      return value_type::nothing();
    }
    if (name == "sort" || name == "reverse") {
      check_count(p_call, 1, 1);
      if (!arguments[0].type.is_list)
        fail(arguments[0].location, "'" + name + "' needs a list.");
      if (name == "sort" && arguments[0].type.base == base_type::boolean)
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
             "Cannot turn " + arguments[0].type.to_string() + " into text.");
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

    const auto found = functions.find(name);
    if (found == functions.end()) {
      if (const auto c_found = c_functions.find(name);
          c_found != c_functions.end())
        return check_c_call(p_call, *c_found->second);
      fail(p_call.location, "Unknown function '" + name +
                                "'. Known functions: " + known_functions() +
                                ". Define it with (fn ...) if it is meant to "
                                "be a new function.");
    }
    const auto &callee = *found->second.declaration;
    check_count(p_call, callee.parameters.size(), callee.parameters.size());
    for (std::size_t index = 0; index < arguments.size(); ++index)
      expect(arguments[index], callee.parameters[index].type,
             "Argument '" + callee.parameters[index].name + "' of '" + name +
                 "'");
    return callee.return_type;
  }

  // (ref NAME): the address of a variable, so a C function can write to it.
  auto check_ref(expression &p_ref, bool p_is_allowed = false) -> value_type {
    if (!p_is_allowed)
      fail(p_ref.location, "(ref NAME) can only be passed to a C function.");
    if (p_ref.arguments.size() != 1 ||
        p_ref.arguments[0].kind != expression::kind_type::variable)
      fail(p_ref.location, "Expected (ref NAME) with a variable name.");
    check_expression(p_ref.arguments[0]);
    if (lookup(p_ref.arguments[0].text) == nullptr)
      fail(p_ref.location, "(ref NAME) needs a variable; '" +
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
               what + " writes " + wanted.to_string() + " into '" +
                   variable.text + "', which holds " +
                   variable.type.to_string() + ".");
      }
    }
    for (std::size_t index = count; index < arguments.size(); ++index) {
      const auto &type = arguments[index].type;
      if (type.is_list || type == value_type::nothing())
        fail(arguments[index].location,
             "Cannot pass " + type.to_string() + " to '" + p_callee.name +
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
                                   value.type.to_string() + ".");
      } else if (string_view_type("fFeEgGaA").contains(letter)) {
        if (value.type == value_type::integer())
          add_note(value.location,
                   spec + " shows a float, so the int is converted.");
        else if (value.type != value_type::real())
          fail(value.location, spec + " needs a number but got " +
                                   value.type.to_string() + ".");
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
             "'" + p_expression.text + "' is a function; call it as (" +
                 p_expression.text + " ...).");
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
    for (const auto &parameter : p_function.parameters)
      if (is_open(parameter.type))
        report(p_function.location, "The type of parameter '" +
                                        parameter.name + "' is not known.");
    if (is_open(p_function.return_type))
      report(p_function.location, "The return type of '" + p_function.name +
                                      "' is not known.");
  }

  auto check_let(statement &p_statement) -> void {
    if (p_statement.declared_type && !p_statement.declared_type->is_known())
      fail(p_statement.location,
           "The type of '" + p_statement.name + "' is not known.");
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
          fail(value.location, "'" + value.text + "' does not return a value.");
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
        is_assignable(type, *existing)) {
      p_statement.kind = statement::kind_type::set;
      return;
    }
    if (existing != nullptr)
      fail(p_statement.location, "'" + p_statement.name +
                                     "' already exists with type " +
                                     existing->to_string() + ".");
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
          fail(value.location, "'" + value.text + "' does not return a value.");
        if (value.type == value_type::pointer())
          fail(value.location, "A pointer cannot be printed.");
      }
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
        fail(values[0].location, "'each' needs a list to go through.");
      scopes.push_back({{p_statement.name, values[0].type.element()}});
      check_loop_body(p_statement.body);
      scopes.pop_back();
      break;
    case kind::break_:
    case kind::continue_:
      if (loop_depth == 0)
        fail(p_statement.location, "break and continue only work in a loop.");
      break;
    case kind::return_: {
      const auto expected = current_function != nullptr
                                ? current_function->return_type
                                : value_type::integer();
      if (values.empty()) {
        if (current_function != nullptr && expected != value_type::nothing())
          fail(p_statement.location,
               "'" + current_function->name + "' must return a " +
                   expected.to_string() + ".");
        break;
      }
      check_expression(values[0]);
      if (expected == value_type::nothing())
        fail(p_statement.location, "'" + current_function->name +
                                       "' has return type void, so it cannot "
                                       "return a value.");
      expect(values[0], expected, "The returned value");
      break;
    }
    case kind::evaluate:
      if (values[0].kind != expression::kind_type::call)
        fail(p_statement.location, "This value is not used; did you mean "
                                   "(print ...)?");
      check_expression(values[0]);
      break;
    case kind::pass:
      break;
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

auto describe_declarations(const unit &p_unit) -> std::vector<string_type> {
  // Plain text, not Ovis Core: shown as `(fn NAME (PARAMS) TYPE)`, models
  // copied the declaration into their own code as an empty function.
  std::vector<string_type> result;
  for (const auto &function : p_unit.functions) {
    string_type call = "(" + function.name;
    string_type types;
    for (const auto &parameter : function.parameters) {
      call += " " + parameter.name;
      types += (types.empty() ? " where " : ", ") + parameter.name + " is " +
               parameter.type.to_string();
    }
    result.push_back(function.name + ": a function, used as `" + call + ")`" +
                     types + "; gives back " +
                     function.return_type.to_string());
  }
  for (const auto &statement : p_unit.statements)
    if (statement.kind == statement::kind_type::let)
      result.push_back(statement.name + ": a variable" +
                       (statement.declared_type
                            ? " holding " + statement.declared_type->to_string()
                            : string_type()));
  return result;
}

} // namespace ovis::core
