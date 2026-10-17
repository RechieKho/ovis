#include <map>

#include <ovis/core/ast.hpp>

namespace ovis::core {

namespace {

auto fail(const sexpr &p_form, const string_type &p_message) -> error {
  return error("line " + std::to_string(p_form.location.line) + ": " +
               p_message + " In: " + p_form.to_string());
}

// Spellings that mean the same operation. The checker and code generator only
// ever see the canonical name on the right.
auto canonical_operation(string_view_type p_name) -> string_type {
  static const std::map<string_view_type, string_view_type> aliases = {
      {"==", "="},      {"eq", "="},        {"<>", "!="},    {"/=", "!="},
      {"mod", "%"},     {"&&", "and"},      {"||", "or"},    {"!", "not"},
      {"length", "len"}, {"size", "len"},   {"count", "len"}, {"at", "get"},
      {"nth", "get"},   {"push", "append"}, {"input", "read_int"},
  };
  if (const auto found = aliases.find(p_name); found != aliases.end())
    return string_type(found->second);
  return string_type(p_name);
}

auto parse_base_type(string_view_type p_name) -> std::optional<base_type> {
  static const std::map<string_view_type, base_type> names = {
      {"int", base_type::integer},     {"integer", base_type::integer},
      {"i64", base_type::integer},     {"float", base_type::real},
      {"real", base_type::real},       {"double", base_type::real},
      {"number", base_type::real},     {"f64", base_type::real},
      {"bool", base_type::boolean},    {"boolean", base_type::boolean},
      {"str", base_type::string},      {"string", base_type::string},
      {"text", base_type::string},     {"void", base_type::nothing},
      {"nothing", base_type::nothing}, {"none", base_type::nothing},
  };
  if (const auto found = names.find(p_name); found != names.end())
    return found->second;
  return std::nullopt;
}

auto parse_expression(const sexpr &p_form) -> expression {
  expression result{.location = p_form.location};
  switch (p_form.kind) {
  case sexpr::kind_type::integer:
    result.kind = expression::kind_type::integer;
    result.integer = p_form.integer;
    return result;
  case sexpr::kind_type::real:
    result.kind = expression::kind_type::real;
    result.real = p_form.real;
    return result;
  case sexpr::kind_type::string:
    result.kind = expression::kind_type::string;
    result.text = p_form.text;
    return result;
  case sexpr::kind_type::symbol:
    if (p_form.text == "true" || p_form.text == "false") {
      result.kind = expression::kind_type::boolean;
      result.boolean = p_form.text == "true";
    } else {
      result.kind = expression::kind_type::variable;
      result.text = p_form.text;
    }
    return result;
  case sexpr::kind_type::list:
    break;
  }

  if (p_form.head().empty())
    throw fail(p_form, "A call must start with the name of an operation or "
                       "function.");
  result.kind = expression::kind_type::call;
  result.text = canonical_operation(p_form.head());

  auto first_argument = p_form.items.begin() + 1;
  if (result.text == "array") {
    if (p_form.items.size() != 3)
      throw fail(p_form, "Expected (array TYPE COUNT).");
    result.type_argument = value_type::parse(p_form.items[1]);
    if (!result.type_argument || result.type_argument->is_list ||
        result.type_argument->base == base_type::nothing)
      throw fail(p_form.items[1], "Expected an element type such as int.");
    ++first_argument;
  }
  for (auto argument = first_argument; argument != p_form.items.end();
       ++argument)
    result.arguments.push_back(parse_expression(*argument));
  return result;
}

auto parse_statement(const sexpr &p_form) -> statement;

auto parse_body(std::vector<sexpr>::const_iterator p_begin,
                std::vector<sexpr>::const_iterator p_end)
    -> std::vector<statement> {
  std::vector<statement> result;
  for (auto form = p_begin; form != p_end; ++form)
    result.push_back(parse_statement(*form));
  return result;
}

// `(then a b)`, `(do a b)` and `(else a b)` are blocks, anything else is a
// single statement.
auto parse_block(const sexpr &p_form) -> std::vector<statement> {
  const auto head = p_form.head();
  if (head == "then" || head == "do" || head == "else" || head == "begin")
    return parse_body(p_form.items.begin() + 1, p_form.items.end());
  return {parse_statement(p_form)};
}

auto expect_name(const sexpr &p_form) -> string_type {
  if (!p_form.is_symbol())
    throw fail(p_form, "Expected a name.");
  return p_form.text;
}

auto parse_statement(const sexpr &p_form) -> statement {
  statement result{.location = p_form.location};
  const auto head = p_form.head();
  const auto &items = p_form.items;
  const auto size = items.size();
  using kind = statement::kind_type;

  if (head == "let" || head == "var" || head == "define") {
    if (size < 3 || size > 4)
      throw fail(p_form, "Expected (let NAME [TYPE] VALUE).");
    result.kind = kind::let;
    result.name = expect_name(items[1]);
    if (size == 4) {
      result.declared_type = value_type::parse(items[2]);
      if (!result.declared_type)
        throw fail(items[2], "Unknown type.");
      result.values.push_back(parse_expression(items[3]));
    } else if (auto type = value_type::parse(items[2]);
               type && (items[2].is_list() ||
                        parse_base_type(items[2].text).has_value() ||
                        items[2].text.ends_with("[]"))) {
      result.declared_type = type;
    } else {
      result.values.push_back(parse_expression(items[2]));
    }
    return result;
  }

  if (head == "set" || head == "assign") {
    if (size != 3)
      throw fail(p_form, "Expected (set NAME VALUE).");
    if (items[1].head() == "get" && items[1].items.size() == 3) {
      result.kind = kind::put;
      result.name = expect_name(items[1].items[1]);
      result.values.push_back(parse_expression(items[1].items[2]));
    } else {
      result.kind = kind::set;
      result.name = expect_name(items[1]);
    }
    result.values.push_back(parse_expression(items[2]));
    return result;
  }

  if (head == "put") {
    if (size != 4)
      throw fail(p_form, "Expected (put LIST INDEX VALUE).");
    result.kind = kind::put;
    result.name = expect_name(items[1]);
    result.values.push_back(parse_expression(items[2]));
    result.values.push_back(parse_expression(items[3]));
    return result;
  }

  if (head == "print" || head == "say" || head == "display") {
    result.kind = kind::print;
    for (std::size_t index = 1; index < size; ++index)
      result.values.push_back(parse_expression(items[index]));
    return result;
  }

  if (head == "if" || head == "when") {
    if (size < 3 || size > 4)
      throw fail(p_form, "Expected (if CONDITION (then ...) (else ...)).");
    result.kind = kind::if_;
    result.values.push_back(parse_expression(items[1]));
    result.body = parse_block(items[2]);
    if (size == 4)
      result.else_body = parse_block(items[3]);
    return result;
  }

  if (head == "while") {
    if (size < 2)
      throw fail(p_form, "Expected (while CONDITION BODY...).");
    result.kind = kind::while_;
    result.values.push_back(parse_expression(items[1]));
    result.body = parse_body(items.begin() + 2, items.end());
    return result;
  }

  if (head == "for") {
    if (size < 4)
      throw fail(p_form, "Expected (for NAME FIRST LAST BODY...).");
    result.kind = kind::for_;
    result.name = expect_name(items[1]);
    result.values.push_back(parse_expression(items[2]));
    result.values.push_back(parse_expression(items[3]));
    result.body = parse_body(items.begin() + 4, items.end());
    return result;
  }

  if (head == "each" || head == "foreach" || head == "for-each") {
    if (size < 3)
      throw fail(p_form, "Expected (each NAME LIST BODY...).");
    result.kind = kind::each;
    result.name = expect_name(items[1]);
    result.values.push_back(parse_expression(items[2]));
    result.body = parse_body(items.begin() + 3, items.end());
    return result;
  }

  if (head == "break" || head == "continue" || head == "pass") {
    if (size != 1)
      throw fail(p_form, "Expected (" + string_type(head) + ").");
    result.kind = head == "break"      ? kind::break_
                  : head == "continue" ? kind::continue_
                                       : kind::pass;
    return result;
  }

  if (head == "return") {
    if (size > 2)
      throw fail(p_form, "Expected (return [VALUE]).");
    result.kind = kind::return_;
    if (size == 2)
      result.values.push_back(parse_expression(items[1]));
    return result;
  }

  if (head == "fn" || head == "function" || head == "defn")
    throw fail(p_form, "Functions can only be defined at the top level.");
  if (head == "then" || head == "do" || head == "begin")
    throw fail(p_form, "A block is only allowed directly inside an if.");

  if (!p_form.is_list())
    throw fail(p_form, "Expected a statement such as (print ...).");
  result.kind = kind::evaluate;
  result.values.push_back(parse_expression(p_form));
  return result;
}

auto parse_function(const sexpr &p_form) -> function {
  const auto &items = p_form.items;
  if (items.size() < 3 || !items[2].is_list())
    throw fail(p_form, "Expected (fn NAME ((PARAMETER TYPE) ...) "
                       "RETURN-TYPE BODY...).");

  function result{.name = expect_name(items[1]), .location = p_form.location};
  for (const auto &declaration : items[2].items) {
    if (!declaration.is_list() || declaration.items.size() != 2)
      throw fail(declaration, "Expected a parameter as (NAME TYPE).");
    const auto type = value_type::parse(declaration.items[1]);
    if (!type || *type == value_type::nothing())
      throw fail(declaration.items[1], "Unknown parameter type.");
    result.parameters.push_back({expect_name(declaration.items[0]), *type});
  }

  auto body_begin = items.begin() + 3;
  if (body_begin != items.end()) {
    if (const auto type = value_type::parse(*body_begin)) {
      result.return_type = *type;
      ++body_begin;
    }
  }
  result.body = parse_body(body_begin, items.end());
  return result;
}

} // namespace

auto value_type::to_string() const -> string_type {
  string_type result;
  switch (base) {
  case base_type::nothing:
    result = "void";
    break;
  case base_type::integer:
    result = "int";
    break;
  case base_type::real:
    result = "float";
    break;
  case base_type::boolean:
    result = "bool";
    break;
  case base_type::string:
    result = "str";
    break;
  }
  return is_list ? result + "[]" : result;
}

auto value_type::parse(const sexpr &p_form) -> std::optional<value_type> {
  if (p_form.is_list()) {
    if (p_form.items.size() != 2 ||
        (p_form.head() != "list" && p_form.head() != "array"))
      return std::nullopt;
    auto element = parse(p_form.items[1]);
    if (!element || element->is_list || element->base == base_type::nothing)
      return std::nullopt;
    return list_of(element->base);
  }
  if (!p_form.is_symbol())
    return std::nullopt;

  string_view_type name = p_form.text;
  const bool is_list = name.ends_with("[]");
  if (is_list)
    name.remove_suffix(2);
  const auto base = parse_base_type(name);
  if (!base || (is_list && *base == base_type::nothing))
    return std::nullopt;
  return value_type{*base, is_list};
}

auto parse_unit(const std::vector<sexpr> &p_forms) -> unit {
  unit result;
  for (const auto &form : p_forms) {
    const auto head = form.head();
    if (head == "fn" || head == "function" || head == "defn")
      result.functions.push_back(parse_function(form));
    else
      result.statements.push_back(parse_statement(form));
  }
  return result;
}

auto parse_unit(string_view_type p_text) -> unit {
  try {
    return parse_unit(read_sexprs(p_text));
  } catch (const syntax_error &p_error) {
    throw error("line " + std::to_string(p_error.location.line) + ": " +
                p_error.what());
  }
}

} // namespace ovis::core
