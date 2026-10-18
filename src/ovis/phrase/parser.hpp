#ifndef OVIS_PHRASE_PARSER_HPP
#define OVIS_PHRASE_PARSER_HPP

#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include <ovis/phrase/lexer.hpp>
#include <ovis/phrase/templates.hpp>

// The phrase front end's line parser, shared by its parts (private to
// src/ovis/phrase).
namespace ovis::phrase::internal {

using clock_type = std::chrono::steady_clock;

auto milliseconds_since(clock_type::time_point p_start) -> double;
auto lower(string_view_type p_text) -> string_type;
// Words are equal ignoring case and a plural ending: "decimal" is
// "Decimals".
auto same_word(string_view_type p_left, string_view_type p_right) -> bool;
auto is_article(const token &p_token) -> bool;
auto is_article_word(string_view_type p_word) -> bool;
// Text as written in the source and in Ovis Core: quoted, with escapes.
auto quote(string_view_type p_text) -> string_type;

// A value's type while parsing: a base type and how many list levels.
struct value_kind {
  // int, float, str, bool, ptr, or ? while it isn't known.
  string_type base = "?";
  std::size_t depth = 0;

  auto known() const -> bool { return base != "?"; }
  auto is_number() const -> bool {
    return depth == 0 && (base == "int" || base == "float");
  }
  auto element() const -> value_kind { return {base, depth - 1}; }
  auto core() const -> string_type {
    auto result = base == "?" ? string_type("int") : base;
    for (std::size_t level = 0; level < depth; ++level)
      result += "[]";
    return result;
  }
  auto english() const -> string_type;
  friend auto operator==(const value_kind &, const value_kind &)
      -> bool = default;
};

// Operator precedence, loosest first.
enum precedence : int {
  or_level = 1,
  and_level,
  not_level,
  comparison_level,
  sum_level,
  product_level,
  negation_level,
  primary_level
};

// One reading of a span of tokens.
struct parse {
  enum class shape_type { atom, operation, phrase, hole };

  shape_type shape = shape_type::atom;
  string_type core;
  // The reading as source text: what a `Read as:` line would say.
  string_type reading;
  value_kind type;
  int level = primary_level;
  // Spans nothing could read, allowed only when looking for one (and then
  // at most one per reading).
  std::size_t holes = 0;
  std::size_t hole_begin = 0;
  std::size_t hole_end = 0;
};

// Type names written in words, for slots of type `type`: each match's
// length in tokens and its type.
auto type_words(const std::vector<token> &p_tokens, std::size_t p_position,
                std::size_t p_end)
    -> std::vector<std::pair<std::size_t, value_kind>>;

// What each slot of a template matched; a variadic slot holds several.
struct slot_fills {
  std::vector<std::vector<parse>> values;
  // For a slot of type `type`.
  std::vector<std::optional<value_kind>> types;
};

// The parse a template gives with these fills, if their types fit.
auto build_phrase(const phrase_template &p_template, const pattern &p_pattern,
                  const slot_fills &p_fills) -> std::optional<parse>;

// A function called as `name(args)`: a user function or a C function.
struct callable {
  // Its name in Ovis Core.
  string_type core;
  value_kind result;
  // The number of arguments, unless it takes any number.
  std::optional<std::size_t> arity;
};

// What lines are read against.
struct vocabulary {
  std::vector<phrase_template> templates;
  // By the name written in calls.
  std::map<string_type, callable> functions;
};

// Every reading of every span of one line, computed once each (a chart
// parser). With `p_allow_holes` a span nothing reads may be a hole, to find
// which part of the line is unclear.
class line_parser {
  struct state;
  std::unique_ptr<state> impl;

public:
  line_parser(const std::vector<token> &p_tokens,
              const std::map<string_type, value_kind> &p_scope,
              const vocabulary &p_vocabulary, bool p_allow_holes);
  ~line_parser();

  auto expressions(std::size_t p_begin, std::size_t p_end)
      -> const std::vector<parse> &;
  // Readings of the whole span as a statement phrase.
  auto statements(std::size_t p_begin, std::size_t p_end, bool p_block)
      -> std::vector<parse>;
};

// What kind of line it is, decided by its first words, and the span that
// must be read as an expression or a statement phrase.
struct line_shape {
  enum class kind_type {
    assign,
    phrase,
    if_,
    else_if,
    else_,
    while_,
    each,
    repeat,
    stop,
    skip
  };

  kind_type kind = kind_type::phrase;
  // The variable assigned or looped over.
  string_type name;
  std::size_t begin = 0;
  std::size_t end = 0;
  bool takes_block = false;
  // Why the line can't be read, if it can't.
  string_type problem;

  auto is_statement() const -> bool { return kind == kind_type::phrase; }
};

auto shape_of(const std::vector<token> &p_tokens) -> line_shape;

auto readings_of(line_parser &p_parser, const line_shape &p_shape)
    -> std::vector<parse>;

} // namespace ovis::phrase::internal

#endif // OVIS_PHRASE_PARSER_HPP
