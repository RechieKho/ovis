#ifndef OVIS_PHRASE_LEXER_HPP
#define OVIS_PHRASE_LEXER_HPP

#include <vector>

#include <ovis/def.hpp>

// The first step of the phrase front end: source text to logical lines of
// tokens, with their block depth and the notes Ovis wrote under them.
namespace ovis::phrase {

struct token {
  enum class kind_type { word, number, text, symbol };

  kind_type kind = kind_type::word;
  // A word or number as written, a text's contents (unescaped) or a symbol.
  string_type text;
  std::size_t line = 1;
  std::size_t column = 1;

  auto is_symbol(string_view_type p_symbol) const -> bool {
    return kind == kind_type::symbol && text == p_symbol;
  }
  // Words match ignoring case: "Show" is "show".
  auto is_word(string_view_type p_word) const -> bool;
};

// A line Ovis writes under a source line. It is never code.
struct annotation {
  enum class kind_type {
    // `Read as: …`: how an unclear phrase was read. Authoritative.
    read_as,
    // `Compiler: …`: something the compiler filled in.
    compiler
  };

  kind_type kind = kind_type::read_as;
  // The text after the label, trimmed.
  string_type text;
  std::size_t line = 1;
};

struct source_line {
  // Where the line starts. A line continues onto the next ones while a
  // bracket is open.
  std::size_t line = 1;
  // How many blocks it is inside: 0 at the top level.
  std::size_t depth = 0;
  std::vector<token> tokens;
  std::vector<annotation> annotations;

  // A line ending in `:` opens a block: the lines indented under it.
  auto opens_block() const -> bool {
    return !tokens.empty() && tokens.back().is_symbol(":");
  }
};

class lex_error : public error {
public:
  std::size_t line;
  std::size_t column;

  lex_error(const string_type &p_message, std::size_t p_line,
            std::size_t p_column)
      : error("line " + std::to_string(p_line) + ", column " +
              std::to_string(p_column) + ": " + p_message),
        line(p_line), column(p_column) {}
};

// Splits a program into logical lines. Throws `lex_error` for:
// - tabs in indentation, and a dedent to a width no outer line has;
// - a deeper line under one that doesn't end with `:`, and a line ending
//   with `:` with nothing indented under it;
// - unclosed text and brackets, and characters that mean nothing;
// - a `Read as:` or `Compiler:` line that isn't indented under a line.
auto split_lines(string_view_type p_source) -> std::vector<source_line>;

} // namespace ovis::phrase

#endif // OVIS_PHRASE_LEXER_HPP
