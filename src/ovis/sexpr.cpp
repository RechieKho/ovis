#include <cctype>
#include <cerrno>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <string>

#include <ovis/core/sexpr.hpp>

namespace ovis::core {

namespace {

class reader {
  string_view_type text;
  std::size_t position = 0;
  source_location location;

  auto at_end() const -> bool { return position >= text.size(); }
  auto peek() const -> char_type { return at_end() ? '\0' : text[position]; }

  auto advance() -> char_type {
    const auto character = text[position++];
    if (character == '\n') {
      ++location.line;
      location.column = 1;
    } else {
      ++location.column;
    }
    return character;
  }

  auto fail(const string_type &p_message) const -> syntax_error {
    return syntax_error(p_message, location);
  }

  static auto is_delimiter(char_type p_character) -> bool {
    return std::isspace(static_cast<unsigned char>(p_character)) ||
           p_character == '(' || p_character == ')' || p_character == '"' ||
           p_character == ';';
  }

  auto read_string() -> sexpr {
    sexpr result{.kind = sexpr::kind_type::string, .location = location};
    advance();
    while (true) {
      if (at_end())
        throw fail("Unterminated string.");
      const auto character = advance();
      if (character == '"')
        break;
      if (character != '\\') {
        result.text.push_back(character);
        continue;
      }
      if (at_end())
        throw fail("Unterminated string.");
      switch (const auto escaped = advance()) {
      case 'n':
        result.text.push_back('\n');
        break;
      case 't':
        result.text.push_back('\t');
        break;
      default:
        result.text.push_back(escaped);
      }
    }
    return result;
  }

  // Apple's libc++ has no floating-point std::from_chars, so reals are read
  // with strtod; this check keeps the spellings from_chars refused (a leading
  // '+', hex, inf, nan) symbols, since strtod would accept them.
  static auto is_decimal_real(string_view_type p_text) -> bool {
    if (p_text.empty() || p_text.front() == '+' ||
        p_text.find_first_not_of("0123456789+-.eE") != string_view_type::npos)
      return false;
    const auto text = std::string(p_text);
    char *real_end = nullptr;
    errno = 0;
    std::strtod(text.c_str(), &real_end);
    return errno == 0 && real_end == text.c_str() + text.size();
  }

  auto read_atom() -> sexpr {
    sexpr result{.location = location};
    while (!at_end() && !is_delimiter(peek()))
      result.text.push_back(advance());

    const auto *const begin = result.text.data();
    if (const auto number = read_whole_number(result.text)) {
      result.kind = sexpr::kind_type::integer;
      result.integer = static_cast<max_int_type>(number->bits);
      result.is_big = number->is_big;
    } else if (is_decimal_real(result.text)) {
      result.real = std::strtod(begin, nullptr);
      result.kind = sexpr::kind_type::real;
    } else {
      result.kind = sexpr::kind_type::symbol;
    }
    return result;
  }

public:
  explicit reader(string_view_type p_text) : text(p_text) {}

  auto skip_space() -> void {
    while (!at_end()) {
      if (std::isspace(static_cast<unsigned char>(peek()))) {
        advance();
      } else if (peek() == ';') {
        while (!at_end() && peek() != '\n')
          advance();
      } else {
        break;
      }
    }
  }

  auto has_more() -> bool {
    skip_space();
    return !at_end();
  }

  auto read() -> sexpr {
    skip_space();
    if (at_end())
      throw fail("Unexpected end of input.");

    switch (peek()) {
    case ')':
      throw fail("Unexpected ')'.");
    case '"':
      return read_string();
    case '(':
      break;
    default:
      return read_atom();
    }

    sexpr result{.kind = sexpr::kind_type::list, .location = location};
    advance();
    while (true) {
      skip_space();
      if (at_end())
        throw syntax_error("Missing ')' for the list opened here.",
                           result.location);
      if (peek() == ')') {
        advance();
        return result;
      }
      result.items.push_back(read());
    }
  }
};

auto escape(string_view_type p_text) -> string_type {
  string_type result = "\"";
  for (const auto character : p_text) {
    switch (character) {
    case '\n':
      result += "\\n";
      break;
    case '\t':
      result += "\\t";
      break;
    case '"':
    case '\\':
      result.push_back('\\');
      [[fallthrough]];
    default:
      result.push_back(character);
    }
  }
  return result + '"';
}

} // namespace

auto read_whole_number(string_view_type p_text) -> std::optional<whole_literal> {
  bool negative = false;
  if (p_text.starts_with('-')) {
    negative = true;
    p_text.remove_prefix(1);
  }
  unsigned base = 10;
  if (p_text.size() > 2 && p_text[0] == '0') {
    const auto letter = static_cast<char_type>(std::tolower(
        static_cast<unsigned char>(p_text[1])));
    base = letter == 'x' ? 16 : letter == 'b' ? 2 : letter == 'o' ? 8 : 10;
    if (base != 10)
      p_text.remove_prefix(2);
  }
  if (p_text.empty())
    return std::nullopt;
  max_uint_type value = 0;
  bool after_digit = false;
  for (std::size_t index = 0; index < p_text.size(); ++index) {
    const auto character = p_text[index];
    // `_` only between two digits.
    if (character == '_') {
      if (!after_digit || index + 1 == p_text.size() || p_text[index + 1] == '_')
        return std::nullopt;
      after_digit = false;
      continue;
    }
    const auto lower =
        std::tolower(static_cast<unsigned char>(character));
    unsigned digit = 0;
    if (lower >= '0' && lower <= '9')
      digit = static_cast<unsigned>(lower - '0');
    else if (lower >= 'a' && lower <= 'f')
      digit = static_cast<unsigned>(lower - 'a' + 10);
    else
      return std::nullopt;
    if (digit >= base)
      return std::nullopt;
    if (value > (~max_uint_type{0} - digit) / base)
      return std::nullopt; // more than 64 bits
    value = value * base + digit;
    after_digit = true;
  }
  constexpr auto largest = static_cast<max_uint_type>(INT64_MAX);
  if (negative) {
    // Down to the smallest signed 64-bit number.
    if (value > largest + 1)
      return std::nullopt;
    return whole_literal{~value + 1, false};
  }
  return whole_literal{value, value > largest};
}

auto sexpr::head() const -> string_view_type {
  if (!is_list() || items.empty() || !items.front().is_symbol())
    return {};
  return items.front().text;
}

auto sexpr::to_string() const -> string_type {
  switch (kind) {
  case kind_type::string:
    return escape(text);
  case kind_type::list: {
    string_type result = "(";
    for (std::size_t index = 0; index < items.size(); ++index) {
      if (index != 0)
        result.push_back(' ');
      result += items[index].to_string();
    }
    return result + ')';
  }
  default:
    return text;
  }
}

auto read_sexprs(string_view_type p_text) -> std::vector<sexpr> {
  reader input(p_text);
  std::vector<sexpr> result;
  while (input.has_more())
    result.push_back(input.read());
  return result;
}

} // namespace ovis::core
