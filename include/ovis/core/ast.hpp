#ifndef OVIS_CORE_AST_HPP
#define OVIS_CORE_AST_HPP

#include <optional>
#include <vector>

#include <ovis/core/libc.hpp>
#include <ovis/core/sexpr.hpp>
#include <ovis/def.hpp>

// The abstract syntax tree of Ovis Core, the strict language the English
// source is translated into.
namespace ovis::core {

// `unknown` (written `?`) only exists between parsing and elaboration: the
// translator may leave types out and the elaborator infers them.
enum class base_type { nothing, integer, real, boolean, string, pointer, unknown };

struct value_type {
  base_type base = base_type::nothing;
  bool is_list = false;

  static constexpr auto nothing() -> value_type { return {}; }
  static constexpr auto integer() -> value_type { return {base_type::integer}; }
  static constexpr auto real() -> value_type { return {base_type::real}; }
  static constexpr auto boolean() -> value_type { return {base_type::boolean}; }
  static constexpr auto string() -> value_type { return {base_type::string}; }
  static constexpr auto pointer() -> value_type { return {base_type::pointer}; }
  static constexpr auto unknown() -> value_type { return {base_type::unknown}; }
  static constexpr auto list_of(base_type p_base) -> value_type {
    return {p_base, true};
  }

  auto is_numeric() const -> bool {
    return !is_list && (base == base_type::integer || base == base_type::real);
  }
  auto element() const -> value_type { return {base}; }
  auto is_known() const -> bool { return base != base_type::unknown; }
  auto to_string() const -> string_type;

  // Accepts the type spellings an LLM is likely to produce: `int`, `float`,
  // `int[]`, `(list int)` and so on.
  static auto parse(const sexpr &p_form) -> std::optional<value_type>;

  friend auto operator==(const value_type &, const value_type &)
      -> bool = default;
};

struct expression {
  enum class kind_type { integer, real, boolean, string, variable, call };

  kind_type kind = kind_type::integer;
  max_int_type integer = 0;
  max_float_type real = 0;
  bool boolean = false;
  // String contents, variable name or the called operation.
  string_type text;
  std::vector<expression> arguments;
  // The element type of `(array TYPE COUNT)`, and of an empty `(list)` once
  // the elaborator has worked it out.
  std::optional<value_type> type_argument;
  source_location location;

  // Filled in by the checker.
  value_type type;
};

struct statement {
  enum class kind_type {
    let,
    set,
    put,
    print,
    if_,
    while_,
    for_,
    each,
    break_,
    continue_,
    return_,
    evaluate,
    pass
  };

  kind_type kind = kind_type::pass;
  string_type name;
  std::optional<value_type> declared_type;
  std::vector<expression> values;
  std::vector<statement> body;
  std::vector<statement> else_body;
  source_location location;
};

struct parameter {
  string_type name;
  value_type type;
};

struct function {
  string_type name;
  std::vector<parameter> parameters;
  value_type return_type;
  std::vector<statement> body;
  source_location location;
};

// Everything one English paragraph translated into.
struct unit {
  std::vector<function> functions;
  // C functions declared with `(extern ...)`.
  std::vector<c_function> externs;
  // Libraries requested with `(link "name")`.
  std::vector<string_type> links;
  // Top-level statements, run in order as part of the program's entry point.
  // Top-level `let`s become globals visible to every function.
  std::vector<statement> statements;
};

struct program {
  std::vector<unit> units;
};

// The type as an author would say it: "a whole number", "a list of text".
auto describe_in_english(const value_type &p_type) -> string_type;

// Every word the parser gives a meaning as the head of a form (statements,
// blocks, `fn`, `extern`, and the accepted spellings of built-ins).
auto core_keywords() -> std::vector<string_view_type>;

// Turns the S-expressions of one paragraph into a unit. Throws `error`.
auto parse_unit(const std::vector<sexpr> &p_forms) -> unit;
auto parse_unit(string_view_type p_text) -> unit;

} // namespace ovis::core

#endif // OVIS_CORE_AST_HPP
