#include <algorithm>
#include <cctype>
#include <iterator>

#include <ovis/core/sexpr.hpp>
#include <ovis/phrase/lexer.hpp>

namespace ovis::phrase {

auto token::is_word(string_view_type p_word) const -> bool {
  return kind == kind_type::word &&
         std::ranges::equal(text, p_word, [](char_type p_left, char_type p_right) {
           return std::tolower(static_cast<unsigned char>(p_left)) ==
                  std::tolower(static_cast<unsigned char>(p_right));
         });
}

namespace {

// What word processors put in place of quotes and apostrophes, in UTF-8.
constexpr string_view_type left_double_quote = "“";
constexpr string_view_type right_double_quote = "”";
constexpr string_view_type right_single_quote = "’";

constexpr string_view_type read_as_label = "Read as:";
constexpr string_view_type compiler_label = "Compiler:";

auto is_word_start(char_type p_character) -> bool {
  const auto byte = static_cast<unsigned char>(p_character);
  // Bytes from 0x80 are UTF-8 letters: words may be in any language.
  return std::isalpha(byte) || p_character == '_' || byte >= 0x80;
}

auto is_word_part(char_type p_character) -> bool {
  return is_word_start(p_character) ||
         std::isdigit(static_cast<unsigned char>(p_character));
}

auto trim(string_view_type p_text) -> string_view_type {
  while (!p_text.empty() && std::isspace(static_cast<unsigned char>(p_text.front())))
    p_text.remove_prefix(1);
  while (!p_text.empty() && std::isspace(static_cast<unsigned char>(p_text.back())))
    p_text.remove_suffix(1);
  return p_text;
}

// The label a line Ovis wrote starts with, or nothing.
auto annotation_label(string_view_type p_content) -> string_view_type {
  for (const auto label : {read_as_label, compiler_label})
    if (p_content.starts_with(label))
      return label;
  return {};
}

// The kind and text of a `@[KIND: TEXT]` line, or why it isn't one. The
// `]` that ends it is the first outside a text.
struct sigil {
  annotation::kind_type kind = annotation::kind_type::meaning;
  string_type text;
  string_type problem;
};
auto read_sigil(string_view_type p_content) -> sigil {
  const auto usage = "Write '@[meaning: LINE]' or '@[note: TEXT]'.";
  std::size_t close = 2;
  for (bool in_text = false; close < p_content.size(); ++close) {
    if (p_content[close] == '"')
      in_text = !in_text;
    else if (p_content[close] == ']' && !in_text)
      break;
  }
  const auto inside = p_content.substr(2, close - 2);
  const auto colon = inside.find(':');
  if (close >= p_content.size() || !trim(p_content.substr(close + 1)).empty() ||
      colon == string_view_type::npos)
    return {.problem = usage};
  const auto kind = trim(inside.substr(0, colon));
  sigil result{.text = string_type(trim(inside.substr(colon + 1)))};
  if (kind == "meaning")
    result.kind = annotation::kind_type::meaning;
  else if (kind == "note")
    result.kind = annotation::kind_type::compiler;
  else
    result.problem = usage;
  return result;
}

struct open_bracket {
  char_type closer;
  std::size_t line;
  std::size_t column;
};

// Tokens of one physical line, added to `p_tokens`. Brackets left open are
// kept in `p_brackets`, so the next line continues the logical line.
class scanner {
  string_view_type text;
  std::size_t line;
  // Column of text[0].
  std::size_t first_column;
  std::size_t position = 0;
  std::vector<token> &tokens;
  std::vector<open_bracket> &brackets;

  auto column() const -> std::size_t { return first_column + position; }
  auto at(std::size_t p_offset = 0) const -> char_type {
    return position + p_offset < text.size() ? text[position + p_offset] : '\0';
  }
  auto rest() const -> string_view_type { return text.substr(position); }
  auto fail(const string_type &p_message) const -> void {
    throw lex_error(p_message, line, column());
  }
  auto add(token::kind_type p_kind, string_type p_text, std::size_t p_column)
      -> void {
    tokens.push_back({p_kind, std::move(p_text), line, p_column, column()});
  }

  // `'s` (or `’s`) right after a word, as in "the book's title".
  auto possessive_length() const -> std::size_t {
    const auto length = rest().starts_with('\'')              ? 1
                        : rest().starts_with(right_single_quote) ? right_single_quote.size()
                                                                 : 0;
    if (length == 0 || (at(length) != 's' && at(length) != 'S') ||
        is_word_part(at(length + 1)))
      return 0;
    return length + 1;
  }

  auto scan_word() -> void {
    const auto start = column();
    string_type word;
    while (true) {
      // A typographic apostrophe is UTF-8 like a letter, but isn't one.
      if (is_word_part(at()) && !rest().starts_with(right_single_quote)) {
        word.push_back(at());
        ++position;
        continue;
      }
      // An apostrophe between letters belongs to the word ("don't"), unless
      // it is a possessive.
      const auto apostrophe = rest().starts_with('\'')              ? 1
                              : rest().starts_with(right_single_quote) ? right_single_quote.size()
                                                                       : 0;
      if (apostrophe > 0 && possessive_length() == 0 &&
          std::isalpha(static_cast<unsigned char>(at(apostrophe)))) {
        word.push_back('\'');
        position += apostrophe;
        continue;
      }
      break;
    }
    add(token::kind_type::word, std::move(word), start);
    if (const auto length = possessive_length(); length > 0) {
      const auto possessive = column();
      position += length;
      add(token::kind_type::symbol, "'s", possessive);
    }
  }

  auto scan_number() -> void {
    const auto start = column();
    const auto begin = position;
    // `0x1F`, `0b1010`, `0o17`: kept as written, Ovis Core reads them.
    const auto prefix = std::tolower(static_cast<unsigned char>(at(1)));
    if (at() == '0' && (prefix == 'x' || prefix == 'b' || prefix == 'o') &&
        std::isxdigit(static_cast<unsigned char>(at(2)))) {
      position += 2;
      while (std::isxdigit(static_cast<unsigned char>(at())) ||
             (at() == '_' && std::isxdigit(static_cast<unsigned char>(at(1)))))
        ++position;
      if (!is_word_part(at())) {
        const auto written = text.substr(begin, position - begin);
        if (!core::read_whole_number(written))
          fail("'" + string_type(written) + "' is not a whole number of at "
               "most 64 bits.");
        add(token::kind_type::number, string_type(written), start);
        return;
      }
      position = begin;
    }
    // `1_000_000`: `_` between digits.
    while (std::isdigit(static_cast<unsigned char>(at())) ||
           (at() == '_' && position > begin &&
            std::isdigit(static_cast<unsigned char>(at(1)))))
      ++position;
    if (at() == '.' && std::isdigit(static_cast<unsigned char>(at(1)))) {
      ++position;
      while (std::isdigit(static_cast<unsigned char>(at())))
        ++position;
    }
    // "2nd", "3D": a word that starts with digits.
    if (is_word_start(at())) {
      position = begin;
      const auto word_start = column();
      string_type word;
      while (is_word_part(at()))
        word.push_back(text[position++]);
      add(token::kind_type::word, std::move(word), word_start);
      return;
    }
    add(token::kind_type::number, string_type(text.substr(begin, position - begin)),
        start);
  }

  auto scan_text(string_view_type p_opener, string_view_type p_closer) -> void {
    const auto start = column();
    position += p_opener.size();
    string_type value;
    while (true) {
      if (position >= text.size())
        throw lex_error("This text has no closing quote.", line, start);
      if (rest().starts_with(p_closer)) {
        position += p_closer.size();
        break;
      }
      if (at() == '\\') {
        switch (at(1)) {
        case 'n':
          value.push_back('\n');
          break;
        case 't':
          value.push_back('\t');
          break;
        case '\\':
        case '"':
        case '\'':
          value.push_back(at(1));
          break;
        default:
          fail(string_type("Unknown escape \\") + at(1) +
               "; use \\n, \\t, \\\\, \\\" or \\'.");
        }
        position += 2;
        continue;
      }
      value.push_back(text[position++]);
    }
    add(token::kind_type::text, std::move(value), start);
  }

  auto scan_symbol() -> void {
    const auto start = column();
    for (const auto symbol : {"!="sv, "<="sv, ">="sv, "=="sv})
      if (rest().starts_with(symbol)) {
        position += 2;
        add(token::kind_type::symbol, string_type(symbol), start);
        return;
      }
    const auto character = at();
    if (string_view_type("+-*/%=<>(),:.[]").find(character) ==
        string_view_type::npos)
      fail(string_type("The character '") + character + "' means nothing here.");
    if (character == '(' || character == '[')
      brackets.push_back({character == '(' ? ')' : ']', line, start});
    if (character == ')' || character == ']') {
      if (brackets.empty() || brackets.back().closer != character)
        fail(string_type("This '") + character + "' closes nothing.");
      brackets.pop_back();
    }
    ++position;
    add(token::kind_type::symbol, string_type(1, character), start);
  }

public:
  scanner(string_view_type p_text, std::size_t p_line, std::size_t p_column,
          std::vector<token> &p_tokens, std::vector<open_bracket> &p_brackets)
      : text(p_text), line(p_line), first_column(p_column), tokens(p_tokens),
        brackets(p_brackets) {}

  auto scan() -> void {
    while (position < text.size()) {
      const auto character = at();
      if (std::isspace(static_cast<unsigned char>(character))) {
        ++position;
      } else if (character == '#') {
        return;
      } else if (character == '"') {
        scan_text("\"", "\"");
      } else if (character == '\'') {
        scan_text("'", "'");
      } else if (rest().starts_with(left_double_quote)) {
        scan_text(left_double_quote, right_double_quote);
      } else if (std::isdigit(static_cast<unsigned char>(character))) {
        scan_number();
      } else if (is_word_start(character) &&
                 !rest().starts_with(right_single_quote) &&
                 !rest().starts_with(right_double_quote)) {
        scan_word();
      } else {
        scan_symbol();
      }
    }
  }
};

// `area of (it): give back 3` is the header `area of (it):` with the body
// `give back 3` as its one indented line. The colon must be outside
// brackets, so `(a: book)` stays a typed parameter. The notes written under
// the line belong to the body, which is where the phrases are read.
auto split_one_line_blocks(std::vector<source_line> p_lines)
    -> std::vector<source_line> {
  std::vector<source_line> result;
  for (auto &line : p_lines) {
    std::size_t depth = 0;
    std::size_t colon = 0;
    for (std::size_t index = 0; index < line.tokens.size(); ++index) {
      const auto &item = line.tokens[index];
      if (item.is_symbol("(") || item.is_symbol("["))
        ++depth;
      else if ((item.is_symbol(")") || item.is_symbol("]")) && depth > 0)
        --depth;
      else if (depth == 0 && item.is_symbol(":") && index > 0 &&
               index + 1 < line.tokens.size()) {
        colon = index;
        break;
      }
    }
    if (colon == 0) {
      result.push_back(std::move(line));
      continue;
    }
    source_line body{.line = line.tokens[colon + 1].line,
                     .depth = line.depth + 1};
    body.tokens.assign(std::make_move_iterator(line.tokens.begin() + colon + 1),
                       std::make_move_iterator(line.tokens.end()));
    body.annotations = std::move(line.annotations);
    line.tokens.resize(colon + 1);
    line.annotations.clear();
    result.push_back(std::move(line));
    result.push_back(std::move(body));
  }
  return result;
}

} // namespace

auto tokenize(string_view_type p_text, std::size_t p_line)
    -> std::vector<token> {
  std::vector<token> tokens;
  std::vector<open_bracket> brackets;
  scanner(p_text, p_line, 1, tokens, brackets).scan();
  if (!brackets.empty())
    throw lex_error(string_type("This '") +
                        (brackets.back().closer == ')' ? "(" : "[") +
                        "' is never closed.",
                    brackets.back().line, brackets.back().column);
  return tokens;
}

auto split_lines(string_view_type p_source) -> std::vector<source_line> {
  std::vector<source_line> result;
  std::vector<open_bracket> brackets;
  // Widths of the indentation of the enclosing blocks; the top is the
  // current block's.
  std::vector<std::size_t> widths = {0};
  std::size_t last_width = 0;
  // `@[…]` lines waiting for the line under them, with their indentation.
  std::vector<std::pair<annotation, std::size_t>> pending;
  const auto stranded = [&](std::size_t p_line) {
    return lex_error("A '@[…]' line goes right above the line it is about, "
                     "with nothing between.",
                     p_line, 1);
  };

  std::size_t number = 0;
  for (std::size_t begin = 0; begin <= p_source.size(); ++number) {
    auto end = p_source.find('\n', begin);
    if (end == string_view_type::npos)
      end = p_source.size();
    auto text = p_source.substr(begin, end - begin);
    const auto at_end = end == p_source.size();
    begin = end + 1;
    if (text.ends_with('\r'))
      text.remove_suffix(1);
    const auto line = number + 1;

    // Inside brackets, indentation means nothing: the logical line goes on.
    if (!brackets.empty()) {
      scanner(text, line, 1, result.back().tokens, brackets).scan();
      if (at_end)
        break;
      continue;
    }

    std::size_t width = 0;
    while (width < text.size() && (text[width] == ' ' || text[width] == '\t')) {
      if (text[width] == '\t' && !trim(text).empty() && trim(text)[0] != '#')
        throw lex_error("Indent with spaces, not tabs.", line, width + 1);
      ++width;
    }
    const auto content = text.substr(width);
    if (trim(content).empty() || content.starts_with('#')) {
      if (!pending.empty())
        throw stranded(pending.back().first.line);
      if (at_end)
        break;
      continue;
    }

    if (content.starts_with("@[")) {
      const auto read = read_sigil(trim(content));
      if (!read.problem.empty())
        throw lex_error(read.problem, line, width + 1);
      pending.push_back({{read.kind, read.text, line, true}, width});
      if (at_end)
        throw stranded(line);
      continue;
    }

    if (const auto label = annotation_label(content); !label.empty()) {
      if (result.empty() || width <= last_width)
        throw lex_error("A '" + string_type(label) +
                            "' line must be indented under the line it is "
                            "about.",
                        line, width + 1);
      result.back().annotations.push_back(
          {label == read_as_label ? annotation::kind_type::read_as
                                  : annotation::kind_type::compiler,
           string_type(trim(content.substr(label.size()))), line});
      if (at_end)
        break;
      continue;
    }

    if (!result.empty() && result.back().opens_block()) {
      if (width <= widths.back())
        throw lex_error("Indent the block under line " +
                            std::to_string(result.back().line) +
                            ", which ends with ':'.",
                        line, width + 1);
      widths.push_back(width);
    } else if (width > widths.back()) {
      throw lex_error(result.empty()
                          ? "The first line can't be indented."
                          : "This line is indented, but line " +
                                std::to_string(result.back().line) +
                                " doesn't end with ':'.",
                      line, width + 1);
    } else {
      while (width < widths.back())
        widths.pop_back();
      if (width != widths.back())
        throw lex_error("This line doesn't line up with any line above it.",
                        line, width + 1);
    }
    last_width = width;
    result.push_back({.line = line, .depth = widths.size() - 1});
    for (auto &[note, note_width] : pending) {
      if (note_width != width)
        throw lex_error("Line this '@[…]' up with the line under it.",
                        note.line, note_width + 1);
      result.back().annotations.push_back(std::move(note));
    }
    pending.clear();
    scanner(content, line, width + 1, result.back().tokens, brackets).scan();
    if (at_end)
      break;
  }

  if (!brackets.empty())
    throw lex_error(string_type("This '") +
                        (brackets.back().closer == ')' ? "(" : "[") +
                        "' is never closed.",
                    brackets.back().line, brackets.back().column);
  if (!pending.empty())
    throw stranded(pending.back().first.line);
  if (!result.empty() && result.back().opens_block())
    throw lex_error("Line " + std::to_string(result.back().line) +
                        " ends with ':', but nothing is indented under it.",
                    result.back().line, result.back().tokens.back().column);
  return split_one_line_blocks(std::move(result));
}

} // namespace ovis::phrase
