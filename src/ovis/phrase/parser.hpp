#ifndef OVIS_PHRASE_PARSER_HPP
#define OVIS_PHRASE_PARSER_HPP

#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

#include <ovis/core/ast.hpp>
#include <ovis/phrase/lexer.hpp>
#include <ovis/phrase/templates.hpp>

// The phrase front end's line parser, shared by its parts (private to
// src/ovis/phrase).
namespace ovis::phrase::internal {

using clock_type = std::chrono::steady_clock;

auto milliseconds_since(clock_type::time_point p_start) -> double;
auto lower(string_view_type p_text) -> string_type;
// Words are equal ignoring case and a plural ending: "decimal" is
// "Decimals" (singulars of 3 letters or more).
auto same_word(string_view_type p_left, string_view_type p_right) -> bool;
auto is_article(const token &p_token) -> bool;
auto is_article_word(string_view_type p_word) -> bool;
// Text as written in the source and in Ovis Core: quoted, with escapes.
auto quote(string_view_type p_text) -> string_type;

// A value's type while parsing: a base type and how many list levels.
struct value_kind {
  // int, float, str, bool, ptr, record, or ? while it isn't known.
  string_type base = "?";
  std::size_t depth = 0;
  // The record's name in Ovis Core, when `base` is `record`.
  string_type record;
  // The Ovis Core type, when it says more than the rest: a typed pointer
  // or a phrase (`ptr` to the parser), an array of C numbers, a C number
  // named as a type (`i32`).
  string_type exact;

  auto known() const -> bool { return base != "?"; }
  auto is_number() const -> bool {
    return depth == 0 && (base == "int" || base == "float");
  }
  auto is_record() const -> bool { return depth == 0 && base == "record"; }
  auto with_depth(std::size_t p_depth) const -> value_kind {
    return {base, p_depth, record};
  }
  // Whether it names a C number (`int32`): only a pointer or an array can
  // hold one; as a value it is a whole number or a decimal.
  auto is_c_number() const -> bool {
    return depth == 0 && !exact.empty() && exact.front() != '(';
  }
  // A list of numbers of a size (`array of bytes`), which also goes where
  // C takes a pointer to its items.
  auto is_c_array() const -> bool {
    return depth == 1 && exact.starts_with("(list ");
  }
  auto element() const -> value_kind { return with_depth(depth - 1); }
  auto core() const -> string_type;
  auto english() const -> string_type;
  // The parser's kinds; `exact` only says more about the same one.
  friend auto operator==(const value_kind &p_left, const value_kind &p_right)
      -> bool {
    return p_left.base == p_right.base && p_left.depth == p_right.depth &&
           p_left.record == p_right.record;
  }
};

// A value of type `p_value` may be put where `p_wanted` is: the same type, a
// whole number for a decimal, or a type not known yet.
auto assignable(const value_kind &p_wanted, const value_kind &p_value) -> bool;

// A record the program declares (`a book has:`), as the parser needs it.
struct record_field {
  // Its name in Ovis Core (`number_of_wheels`) and as written (`number`,
  // `of`, `wheels`; one word for most fields).
  string_type name;
  std::vector<string_type> words;
  value_kind type;
  // The default's Ovis Core, put into `(new …)` when a line leaves the
  // field out.
  std::optional<string_type> default_core;

  // As a person writes it: `number of wheels`.
  auto spoken() const -> string_type;
};

// A field named by its words: `number of wheels` is `number_of_wheels`.
auto field_named(const std::vector<string_type> &p_words) -> record_field;

struct record_info {
  // Its name in Ovis Core (`order_item`) and as written (`order`, `item`).
  string_type name;
  std::vector<string_type> words;
  std::vector<record_field> fields;
  // A trait (`a shape can:`) is a type too, with no fields: any record
  // that implements it fits.
  bool is_trait = false;
  // `a header has, packed:`: its fields lie with no gaps and no hidden
  // tag (numbers.md B6).
  bool is_packed = false;

  // The field called `p_word` (or its plural): its Ovis Core name, or its
  // words with spaces (`number of wheels`).
  auto find(string_view_type p_word) const -> const record_field *;
  // The field whose words are the tokens at `p_position` (the longest), and
  // how many tokens they are; its Ovis Core name is one token.
  auto field_at(const std::vector<token> &p_tokens, std::size_t p_position,
                std::size_t p_end) const
      -> std::pair<const record_field *, std::size_t>;
  auto type() const -> value_kind { return {"record", 0, name}; }
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
  // When the hole is a field's name: the record whose fields it may be.
  string_type hole_record;
  // For a phrase: its pattern starts or ends with a slot.
  bool open_edge = false;
  // The templates it is made of, so tests can tell every one is used.
  std::vector<const phrase_template *> uses;

  auto use_all(const parse &p_part) -> void {
    uses.insert(uses.end(), p_part.uses.begin(), p_part.uses.end());
  }
  // Takes on a part's hole, if it has one.
  auto take_holes(const parse &p_part) -> void {
    if (p_part.holes == 0)
      return;
    holes += p_part.holes;
    hole_begin = p_part.hole_begin;
    hole_end = p_part.hole_end;
    hole_record = p_part.hole_record;
  }
};

struct vocabulary;

// Type names written in words, for slots of type `type`: each match's
// length in tokens and its type. Records' names count with a vocabulary.
auto type_words(const std::vector<token> &p_tokens, std::size_t p_position,
                std::size_t p_end, const vocabulary *p_vocabulary = nullptr)
    -> std::vector<std::pair<std::size_t, value_kind>>;

// What each slot of a template matched; a variadic slot holds several.
struct slot_fills {
  std::vector<std::vector<parse>> values;
  // For a slot of type `type`.
  std::vector<std::optional<value_kind>> types;
};

// The parse a template gives with these fills, if their types fit.
auto build_phrase(const phrase_template &p_template, const pattern &p_pattern,
                  const slot_fills &p_fills, const vocabulary &p_vocabulary)
    -> std::optional<parse>;

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
  // C constants (`pi`, `EOF`, a header's `Z_OK`), used where no variable of
  // that name is in scope.
  std::map<string_type, value_kind> constants;
  // The program's records, by their name in Ovis Core.
  std::map<string_type, record_info> records;
  // Records' implementations of the built-in traits: trait, then record,
  // then the Ovis Core name of the function the author wrote.
  std::map<string_type, std::map<string_type, string_type>> impls;
  // `a circle is a shape:`: (trait, record) pairs of user traits.
  std::set<std::pair<string_type, string_type>> trait_impls;
  // A phrase as a value, by its words with `_` for its slots (`compare _
  // and _`; articles left out): its Ovis Core name (references.md §7b).
  std::map<string_type, string_type> phrase_values;
  // For finding out why a line doesn't read: every record counts as
  // comparable and equatable.
  bool assume_record_traits = false;

  // The trait every one of these types is, or implements, if there is one
  // (the first by name): what a list of different records is a list of.
  auto common_trait(const std::vector<value_kind> &p_types) const
      -> std::optional<string_type>;

  // The function that implements `p_trait` for `p_type`, or nullptr.
  auto implementation(string_view_type p_trait, const value_kind &p_type) const
      -> const string_type *;
  // Whether a value of type `p_type` (not a list) has the trait: `num`,
  // `any` and the traits of `is_trait_name`.
  auto implements(string_view_type p_trait, const value_kind &p_type) const
      -> bool;

  // The record whose name is written at `p_position`, and its length in
  // tokens (the longest, if names share words).
  auto record_at(const std::vector<token> &p_tokens, std::size_t p_position,
                 std::size_t p_end) const
      -> std::pair<const record_info *, std::size_t>;
  // Whether any record has a field called `p_word`.
  auto is_field(string_view_type p_word) const -> bool;
  // The lengths of the fields of any record whose words are at
  // `p_position`, longest first: `number of wheels` may also be `number`.
  auto field_lengths(const std::vector<token> &p_tokens, std::size_t p_position,
                     std::size_t p_end) const -> std::vector<std::size_t>;
  // Whether `p_word` is a word of some field's name, other than a function
  // word (`of`): a line naming it is about that field.
  auto is_field_word(string_view_type p_word) const -> bool;
};

// The built-in traits a record implements by a phrase with these words and
// parameters, written `to (a: book) comes before (b: book):`: the trait,
// the record's Ovis Core name and the number of parameters, or nothing.
struct trait_method {
  string_type trait;
  string_type record;
};
auto trait_method_of(string_view_type p_name,
                     const std::vector<std::optional<value_kind>> &p_types)
    -> std::optional<trait_method>;

// Ovis Core's types as the parser's: `void` for nothing.
auto kind_of(const core::value_type &p_type) -> value_kind;
auto kind_of(const core::c_constant &p_constant) -> value_kind;

// A variable's name in Ovis Core: names Ovis Core gives a meaning (`text`,
// `list`, `print`, ...) get a `_`, so `text = "hi"` stays a variable.
auto core_name(const string_type &p_name) -> string_type;

// The C standard library's functions and constants, which every program
// can use.
auto add_libc(vocabulary &p_vocabulary) -> void;

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
    skip,
    // `to HEADER:`, a function; `give back [VALUE]`.
    define,
    give_back,
    // `at the end, STATEMENT` (or `defer STATEMENT`): the statement is
    // `begin`..`end`. `at the end:` opens a block of them.
    defer_line,
    defer_block,
    // `use "zlib.h"`, `use the C library "sqlite3.h" from "sqlite3"`.
    use_c,
    // `a book has:`, a record; `name` is its words.
    record,
    // `a shape can:`, a trait; `name` is its words.
    trait_decl,
    // `a circle is a shape:`: `name` is the record's words, `trait_name`
    // the trait's; `begin`..`end` span the record's.
    impl_decl
  };

  kind_type kind = kind_type::phrase;
  // The variable assigned or looped over; for `use`, the header.
  string_type name;
  // For `use`: the library to link, if named.
  string_type library;
  // For a record: `a header has, packed:`.
  bool packed = false;
  // For an impl: the trait's words, and where they start.
  string_type trait_name;
  std::size_t trait_begin = 0;
  std::size_t trait_end = 0;
  std::size_t begin = 0;
  std::size_t end = 0;
  bool takes_block = false;
  // Why the line can't be read, if it can't.
  string_type problem;

  auto is_statement() const -> bool {
    return kind == kind_type::phrase || kind == kind_type::defer_line;
  }
};

auto shape_of(const std::vector<token> &p_tokens) -> line_shape;

auto readings_of(line_parser &p_parser, const line_shape &p_shape)
    -> std::vector<parse>;

} // namespace ovis::phrase::internal

#endif // OVIS_PHRASE_PARSER_HPP
