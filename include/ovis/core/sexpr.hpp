#ifndef OVIS_CORE_SEXPR_HPP
#define OVIS_CORE_SEXPR_HPP

#include <optional>
#include <vector>

#include <ovis/def.hpp>

// Ovis Core is written as S-expressions. This is the reader for them: it only
// knows about lists and atoms, the meaning of a form is decided by the parser.
namespace ovis::core {

struct source_location {
  std::size_t line = 1;
  std::size_t column = 1;
};

struct sexpr {
  enum class kind_type { list, symbol, integer, real, string };

  kind_type kind = kind_type::list;
  // Symbol name, string contents (unescaped) or the number as written.
  string_type text;
  max_int_type integer = 0;
  max_float_type real = 0;
  // A whole number above the largest signed 64-bit one (`0xFFFF…`), kept as
  // its bits in `integer`: only a uint64 can hold it.
  bool is_big = false;
  std::vector<sexpr> items;
  source_location location;

  auto is_list() const -> bool { return kind == kind_type::list; }
  auto is_symbol() const -> bool { return kind == kind_type::symbol; }
  auto is_symbol(string_view_type p_name) const -> bool {
    return is_symbol() && text == p_name;
  }
  // Name of the leading symbol of a list, or empty.
  auto head() const -> string_view_type;
  auto to_string() const -> string_type;
};

class syntax_error : public error {
public:
  source_location location;

  syntax_error(const string_type &p_message, source_location p_location)
      : error(p_message), location(p_location) {}
};

// A whole number as written: `42`, `-7`, `1_000_000`, `0x1F`, `0b1010`,
// `0o17` (`_` between digits). Its bits, and whether it is above the
// largest signed 64-bit number; nothing if it isn't one or doesn't fit in
// 64 bits.
struct whole_literal {
  max_uint_type bits = 0;
  bool is_big = false;
};
auto read_whole_number(string_view_type p_text) -> std::optional<whole_literal>;

// Reads every top-level form in the text. `;` starts a comment until the end
// of the line. Throws `syntax_error`.
auto read_sexprs(string_view_type p_text) -> std::vector<sexpr>;

} // namespace ovis::core

#endif // OVIS_CORE_SEXPR_HPP
