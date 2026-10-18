#ifndef OVIS_PHRASE_TEMPLATES_HPP
#define OVIS_PHRASE_TEMPLATES_HPP

#include <optional>
#include <utility>
#include <vector>

#include <ovis/def.hpp>

// Phrase templates: the phrases the front end knows, each with typed slots
// and the Ovis Core it stands for. Built-in ones are in `phrases/*.phr`.
namespace ovis::phrase {

// A slot's or a result's type, as written in a template.
struct type_name {
  enum class kind_type {
    // int, float, num, str, bool, any, or type (the slot takes a type,
    // written in words: "whole number").
    builtin,
    // An uppercase name such as T: the same type wherever it appears.
    variable,
    // The name of a slot of type `type`: the type the author wrote there.
    type_slot
  };

  kind_type kind = kind_type::builtin;
  string_type name;
  // How many `[]`: 1 is a list of `name`.
  std::size_t list_depth = 0;

  auto to_string() const -> string_type;
  friend auto operator==(const type_name &, const type_name &) -> bool = default;
};

struct slot {
  string_type name;
  type_name type;
  // `{items: any, ...}`: one or more values, separated by commas.
  bool is_variadic = false;
};

// One part of a pattern. Words are stored in lower case.
struct pattern_element {
  enum class kind_type {
    word,
    slot,
    // `[the]`: the words in `branches[0]` may be left out.
    optional,
    // `(a|b c)`, or `decimal[s]` (decimal | decimals): one of `branches`.
    choice
  };

  kind_type kind = kind_type::word;
  string_type word;
  // Index into `phrase_template::slots`.
  std::size_t slot = 0;
  std::vector<std::vector<pattern_element>> branches;

  friend auto operator==(const pattern_element &, const pattern_element &)
      -> bool = default;
};

using pattern = std::vector<pattern_element>;

struct phrase_template {
  enum class kind_type { expression, statement };

  kind_type kind = kind_type::expression;
  // The first is the canonical pattern; the others are its `alias:` lines.
  std::vector<pattern> patterns;
  std::vector<slot> slots;
  // What an expression gives back.
  std::optional<type_name> result;
  // A statement ending in `: BLOCK` takes the lines indented under it.
  bool takes_block = false;
  // One Ovis Core form, with `$name` for slots, `$name...` for a variadic
  // slot spliced in, and `BLOCK` for the block.
  string_type core;
  // Where it was written, for errors.
  string_type file;
  std::size_t line = 0;
};

// Reads templates in the `.phr` format:
//
//   # comment
//   expr  average of {xs: num[]}        : float => (/ (float (sum $xs)) ...)
//         alias: mean of {xs}
//   stmt  add {x: T} to {xs: T[]}               => (append $xs $x)
//   stmt  keep doing {n: int} times: BLOCK      => (for _ 1 $n BLOCK)
//
// Throws `error` naming `p_file` and the line.
auto read_templates(string_view_type p_text, string_view_type p_file)
    -> std::vector<phrase_template>;

// A pattern written back in the `.phr` format, slots with their types:
// "add {x: T} to {xs: T[]}".
auto to_string(const phrase_template &p_template, const pattern &p_pattern)
    -> string_type;

// The `phrases/*.phr` files compiled into Ovis: (file name, contents).
auto builtin_phrase_files()
    -> const std::vector<std::pair<string_view_type, string_view_type>> &;

// Every template of `builtin_phrase_files()`, read once.
auto builtin_templates() -> const std::vector<phrase_template> &;

} // namespace ovis::phrase

#endif // OVIS_PHRASE_TEMPLATES_HPP
