#include <algorithm>
#include <deque>
#include <functional>
#include <set>

#include <ovis/core/monomorphise.hpp>

namespace ovis::core {

namespace {

// Core text with each `(var NAME)` replaced: a pointer's pointee, a
// phrase's signature, a generic record's types.
auto substitute_text(const string_type &p_text, const type_bindings &p_bindings)
    -> string_type {
  if (p_text.find("(var ") == string_type::npos)
    return p_text;
  const std::function<sexpr(const sexpr &)> replace = [&](const sexpr &p_form) {
    if (p_form.is_list() && p_form.items.size() == 2 && p_form.head() == "var" &&
        p_form.items[1].is_symbol())
      if (const auto found = p_bindings.find(p_form.items[1].text);
          found != p_bindings.end())
        return read_sexprs(found->second.to_string()).front();
    auto result = p_form;
    for (auto &item : result.items)
      item = replace(item);
    return result;
  };
  string_type result;
  for (const auto &form : read_sexprs(p_text))
    result += (result.empty() ? "" : " ") + replace(form).to_string();
  return result;
}

auto substitute(expression &p_expression, const type_bindings &p_bindings)
    -> void {
  p_expression.type = substitute(p_expression.type, p_bindings);
  if (p_expression.type_argument)
    p_expression.type_argument = substitute(*p_expression.type_argument, p_bindings);
  for (auto &type : p_expression.type_arguments)
    type = substitute(type, p_bindings);
  // `(load (var item) p)`: what is at a pointer, written as Core text.
  for (auto &field : p_expression.fields)
    field = substitute_text(field, p_bindings);
  for (auto &argument : p_expression.arguments)
    substitute(argument, p_bindings);
}

auto substitute(std::vector<statement> &p_body, const type_bindings &p_bindings)
    -> void {
  for (auto &statement : p_body) {
    if (statement.declared_type)
      statement.declared_type = substitute(*statement.declared_type, p_bindings);
    for (auto &value : statement.values)
      substitute(value, p_bindings);
    substitute(statement.body, p_bindings);
    substitute(statement.else_body, p_bindings);
  }
}

} // namespace

auto substitute(const value_type &p_type, const type_bindings &p_bindings)
    -> value_type {
  if (!p_type.has_variables())
    return p_type;
  if (p_type.is_variable()) {
    const auto found = p_bindings.find(p_type.record);
    return found != p_bindings.end() ? found->second : p_type;
  }
  if (p_type.is_list)
    return value_type::list_of(substitute(p_type.element(), p_bindings));
  if (p_type.is_map())
    return value_type::map_of(substitute(p_type.map_key(), p_bindings),
                              substitute(p_type.map_value(), p_bindings));
  if (p_type.is_record()) {
    std::vector<value_type> arguments;
    for (const auto &argument : p_type.record_arguments())
      arguments.push_back(substitute(argument, p_bindings));
    auto result = value_type::record_of(p_type.record, arguments);
    result.nullable = p_type.nullable;
    return result;
  }
  auto result = p_type;
  result.pointee = substitute_text(p_type.pointee, p_bindings);
  // `(many (var item))` for whole numbers is `(many int)`: a C number's
  // own spelling where there is one.
  if (result.is_typed_pointer() && !result.is_phrase())
    if (const auto forms = read_sexprs(result.pointee); forms.size() == 1)
      if (const auto canonical = parse_pointee(forms.front()))
        result.pointee = *canonical;
  return result;
}

auto substitute(function &p_function, const type_bindings &p_bindings) -> void {
  for (auto &parameter : p_function.parameters)
    parameter.type = substitute(parameter.type, p_bindings);
  p_function.return_type = substitute(p_function.return_type, p_bindings);
  substitute(p_function.body, p_bindings);
}

auto match_type(const value_type &p_generic, const value_type &p_given,
                type_bindings &p_bindings) -> bool {
  if (!p_generic.has_variables())
    return true;
  if (p_generic.is_variable()) {
    if (!p_given.is_known())
      return true;
    const auto [found, added] = p_bindings.emplace(p_generic.record, p_given);
    return added || found->second == p_given;
  }
  if (p_generic.is_list) {
    if (!p_given.is_list)
      return false;
    return match_type(p_generic.element(), p_given.element(), p_bindings);
  }
  if (p_generic.is_map()) {
    if (!p_given.is_map())
      return false;
    return match_type(p_generic.map_key(), p_given.map_key(), p_bindings) &&
           match_type(p_generic.map_value(), p_given.map_value(), p_bindings);
  }
  if (p_generic.is_record()) {
    if (!p_given.is_record() || p_given.record != p_generic.record)
      return false;
    const auto generic = p_generic.record_arguments();
    const auto given = p_given.record_arguments();
    if (generic.size() != given.size())
      return false;
    for (std::size_t index = 0; index < generic.size(); ++index)
      if (!match_type(generic[index], given[index], p_bindings))
        return false;
    return true;
  }
  // A pointer to a type variable's values: what the given one points at.
  if (p_generic.is_typed_pointer() && p_given.is_typed_pointer()) {
    const auto generic = read_sexprs(p_generic.pointee);
    const auto given = read_sexprs(p_given.pointee);
    if (generic.size() != 1 || given.size() != 1)
      return false;
    const auto inner = value_type::parse(generic.front());
    auto other = value_type::parse(given.front());
    if (!other)
      if (const auto pointee = read_pointee(p_given.pointee))
        other = pointee->kind == pointee_type::kind_type::number
                    ? value_type::number(pointee->number)
                    : pointee_value(*pointee);
    return inner && other && match_type(*inner, *other, p_bindings);
  }
  return true;
}

auto mangle(const value_type &p_type) -> string_type {
  if (p_type.is_list)
    return "list_" + mangle(p_type.element());
  if (p_type.is_map())
    return "map_" + mangle(p_type.map_key()) + "_" + mangle(p_type.map_value());
  if (p_type.is_sized())
    return p_type.pointee;
  switch (p_type.base) {
  case base_type::nothing:
    return "void";
  case base_type::integer:
    return "int";
  case base_type::real:
    return "float";
  case base_type::boolean:
    return "bool";
  case base_type::string:
    return "str";
  case base_type::memory:
    return "memory";
  case base_type::unknown:
    return "unknown";
  case base_type::variable:
    return "var_" + p_type.record;
  case base_type::map:
    break;
  case base_type::record: {
    string_type result = (p_type.nullable ? "maybe_record_" : "record_") +
                         p_type.record;
    for (const auto &argument : p_type.record_arguments())
      result += "__" + mangle(argument);
    return result;
  }
  case base_type::pointer: {
    if (p_type.kind == pointer_kind::raw)
      return "ptr";
    string_type result = p_type.kind == pointer_kind::many ? "many_"
                         : p_type.kind == pointer_kind::c  ? "cptr_"
                                                           : "ptr_";
    for (const auto character : p_type.pointee)
      if (std::isalnum(static_cast<unsigned char>(character)) != 0)
        result.push_back(character);
      else if (character == ' ')
        result.push_back('_');
    return result;
  }
  }
  return "unknown";
}

auto copy_name(string_view_type p_name, const std::vector<value_type> &p_types)
    -> string_type {
  string_type result(p_name);
  for (const auto &type : p_types) {
    auto part = mangle(type);
    // A record of the program is its name: `push__book`, `stack__int`.
    if (type.is_record() && !type.nullable && part.starts_with("record_"))
      part = part.substr(7);
    result += "__" + part;
  }
  return result;
}

} // namespace ovis::core
