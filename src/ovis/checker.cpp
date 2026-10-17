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

const std::set<string_view_type> builtin_names = {
    "+",     "-",    "*",     "/",   "%",      "=",        "!=",
    "<",     "<=",   ">",     ">=",  "and",    "or",       "not",
    "int",   "float", "sqrt", "pow", "floor",  "abs",      "min",
    "max",   "len",  "get",   "list", "array", "append",   "read_int",
    "read_float", "random"};

class checker {
  struct function_entry {
    function *declaration;
    std::size_t unit_index;
  };

  program &target;
  std::vector<diagnostic> diagnostics;
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
    for (auto &argument : p_call.arguments)
      check_expression(argument);

    const auto &name = p_call.text;
    auto &arguments = p_call.arguments;

    if (name == "-") {
      check_count(p_call, 1, SIZE_MAX);
      return expect_numeric(p_call);
    }
    if (name == "+" || name == "*" || name == "/" || name == "min" ||
        name == "max") {
      check_count(p_call, 2, SIZE_MAX);
      return expect_numeric(p_call);
    }
    if (name == "%") {
      check_count(p_call, 2, 2);
      return expect_numeric(p_call);
    }
    if (name == "=" || name == "!=") {
      check_count(p_call, 2, 2);
      const auto &left = arguments[0].type;
      const auto &right = arguments[1].type;
      if (!(left.is_numeric() && right.is_numeric()) &&
          !(left == right && !left.is_list &&
            left.base != base_type::nothing))
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
      if (element.is_list || element == value_type::nothing())
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
    if (name == "read_int" || name == "read_float") {
      check_count(p_call, 0, 0);
      return name == "read_int" ? value_type::integer() : value_type::real();
    }
    if (name == "random") {
      check_count(p_call, 2, 2);
      expect(arguments[0], value_type::integer(), "The lowest random value");
      expect(arguments[1], value_type::integer(), "The highest random value");
      return value_type::integer();
    }

    const auto found = functions.find(name);
    if (found == functions.end())
      fail(p_call.location, "Unknown function '" + name +
                                "'. Known functions: " + known_functions() +
                                ". Define it with (fn ...) if it is meant to "
                                "be a new function.");
    const auto &callee = *found->second.declaration;
    check_count(p_call, callee.parameters.size(), callee.parameters.size());
    for (std::size_t index = 0; index < arguments.size(); ++index)
      expect(arguments[index], callee.parameters[index].type,
             "Argument '" + callee.parameters[index].name + "' of '" + name +
                 "'");
    return callee.return_type;
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

  auto check_let(statement &p_statement) -> void {
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
  explicit checker(program &p_program) : target(p_program) {}

  auto run() -> std::vector<diagnostic> {
    for (unit_index = 0; unit_index < target.units.size(); ++unit_index) {
      for (auto &function : target.units[unit_index].functions) {
        if (function.name == "main" || builtin_names.contains(function.name))
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

auto check(program &p_program) -> std::vector<diagnostic> {
  return checker(p_program).run();
}

auto describe_declarations(const unit &p_unit) -> std::vector<string_type> {
  std::vector<string_type> result;
  for (const auto &function : p_unit.functions) {
    string_type text = "(fn " + function.name + " (";
    for (std::size_t index = 0; index < function.parameters.size(); ++index) {
      if (index != 0)
        text += ' ';
      text += "(" + function.parameters[index].name + " " +
              function.parameters[index].type.to_string() + ")";
    }
    result.push_back(text + ") " + function.return_type.to_string() + ")");
  }
  for (const auto &statement : p_unit.statements)
    if (statement.kind == statement::kind_type::let)
      result.push_back(
          "(let " + statement.name +
          (statement.declared_type ? " " + statement.declared_type->to_string()
                                   : string_type()) +
          ")");
  return result;
}

} // namespace ovis::core
