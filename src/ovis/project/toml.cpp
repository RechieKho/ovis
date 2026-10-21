#include <algorithm>
#include <cctype>

#include <ovis/project/toml.hpp>

namespace ovis::project {

auto toml_value::find(string_view_type p_key) const -> const toml_value * {
  for (const auto &entry : entries)
    if (entry.key == p_key)
      return &entry.value;
  return nullptr;
}

auto toml_value::kind_name() const -> string_view_type {
  switch (kind) {
  case kind_type::string:
    return "a text";
  case kind_type::integer:
    return "a whole number";
  case kind_type::boolean:
    return "true or false";
  case kind_type::array:
    return "a list";
  case kind_type::table:
    return "a table";
  }
  return "a value";
}

namespace {

auto is_bare_key_character(char p_character) -> bool {
  return std::isalnum(static_cast<unsigned char>(p_character)) ||
         p_character == '_' || p_character == '-';
}

class reader {
  string_view_type text;
  string_type file_name;
  std::size_t position = 0;
  std::size_t line = 1;

public:
  reader(string_view_type p_text, string_view_type p_file_name)
      : text(p_text), file_name(p_file_name) {}

  [[noreturn]] auto fail(const string_type &p_message) const -> void {
    throw error(file_name + ", line " + std::to_string(line) + ": " + p_message);
  }

  auto at_end() const -> bool { return position >= text.size(); }
  auto peek() const -> char { return at_end() ? '\0' : text[position]; }

  auto advance() -> char {
    const auto character = text[position++];
    if (character == '\n')
      ++line;
    return character;
  }

  // Spaces and tabs, not newlines.
  auto skip_blanks() -> void {
    while (peek() == ' ' || peek() == '\t')
      advance();
  }

  auto skip_comment() -> void {
    if (peek() == '#')
      while (!at_end() && peek() != '\n')
        advance();
  }

  // Blanks, comments and newlines: inside arrays.
  auto skip_space() -> void {
    for (;;) {
      skip_blanks();
      skip_comment();
      if (peek() == '\n' || peek() == '\r')
        advance();
      else
        return;
    }
  }

  // The rest of a line must be empty or a comment.
  auto end_line() -> void {
    skip_blanks();
    skip_comment();
    if (peek() == '\r')
      advance();
    if (at_end())
      return;
    if (peek() != '\n')
      fail("expected the end of the line, found `" + string_type(1, peek()) + "`");
    advance();
  }

  auto read_basic_string() -> string_type {
    advance(); // "
    string_type result;
    for (;;) {
      if (at_end() || peek() == '\n')
        fail("a text is not closed with \"");
      const auto character = advance();
      if (character == '"')
        return result;
      if (character != '\\') {
        result.push_back(character);
        continue;
      }
      if (at_end())
        fail("a text is not closed with \"");
      switch (const auto escaped = advance()) {
      case 'n':
        result.push_back('\n');
        break;
      case 't':
        result.push_back('\t');
        break;
      case 'r':
        result.push_back('\r');
        break;
      case '"':
      case '\\':
        result.push_back(escaped);
        break;
      default:
        fail("unknown escape \\" + string_type(1, escaped) + " in a text");
      }
    }
  }

  auto read_literal_string() -> string_type {
    advance(); // '
    string_type result;
    for (;;) {
      if (at_end() || peek() == '\n')
        fail("a text is not closed with '");
      const auto character = advance();
      if (character == '\'')
        return result;
      result.push_back(character);
    }
  }

  auto read_key_part() -> string_type {
    skip_blanks();
    if (peek() == '"')
      return read_basic_string();
    if (peek() == '\'')
      return read_literal_string();
    string_type key;
    while (is_bare_key_character(peek()))
      key.push_back(advance());
    if (key.empty())
      fail(at_end() || peek() == '\n'
               ? string_type("expected a key")
               : "expected a key, found `" + string_type(1, peek()) + "`");
    return key;
  }

  // `a.b."c d"`
  auto read_key() -> std::vector<string_type> {
    std::vector<string_type> parts{read_key_part()};
    skip_blanks();
    while (peek() == '.') {
      advance();
      parts.push_back(read_key_part());
      skip_blanks();
    }
    return parts;
  }

  auto read_value() -> toml_value {
    skip_blanks();
    toml_value value;
    value.line = line;
    const auto character = peek();
    if (character == '"' || character == '\'') {
      if (text.substr(position).starts_with(character == '"' ? "\"\"\"" : "'''"))
        fail("texts over several lines are not read in ovis.toml");
      value.kind = toml_value::kind_type::string;
      value.text = character == '"' ? read_basic_string() : read_literal_string();
      return value;
    }
    if (character == '[') {
      advance();
      value.kind = toml_value::kind_type::array;
      for (;;) {
        skip_space();
        if (peek() == ']') {
          advance();
          return value;
        }
        value.items.push_back(read_value());
        skip_space();
        if (peek() == ',') {
          advance();
          continue;
        }
        if (peek() == ']') {
          advance();
          return value;
        }
        fail("expected , or ] in a list");
      }
    }
    if (character == '{') {
      advance();
      value.kind = toml_value::kind_type::table;
      skip_blanks();
      if (peek() == '}') {
        advance();
        return value;
      }
      for (;;) {
        const auto key = read_key();
        skip_blanks();
        if (peek() != '=')
          fail("expected = after a key");
        advance();
        set(value, key, read_value());
        skip_blanks();
        if (peek() == ',') {
          advance();
          continue;
        }
        if (peek() == '}') {
          advance();
          return value;
        }
        fail("expected , or } in an inline table");
      }
    }
    string_type word;
    while (!at_end() && (is_bare_key_character(peek()) || peek() == '+' ||
                         peek() == '.' || peek() == ':'))
      word.push_back(advance());
    if (word == "true" || word == "false") {
      value.kind = toml_value::kind_type::boolean;
      value.boolean = word == "true";
      return value;
    }
    auto digits = word;
    if (!digits.empty() && (digits.front() == '+' || digits.front() == '-'))
      digits.erase(0, 1);
    std::erase(digits, '_');
    if (!digits.empty() && std::ranges::all_of(digits, [](char p_digit) {
          return std::isdigit(static_cast<unsigned char>(p_digit));
        })) {
      value.kind = toml_value::kind_type::integer;
      try {
        value.integer = std::stoll((word.front() == '-' ? "-" : "") + digits);
      } catch (const std::out_of_range &) {
        fail("the number " + word + " is too large");
      }
      return value;
    }
    if (word.empty())
      fail(at_end() || peek() == '\n' ? string_type("expected a value after =")
                                      : "expected a value, found `" +
                                            string_type(1, peek()) + "`");
    fail("`" + word + "` is not a value ovis.toml reads: write texts in \"quotes\"");
  }

  // Puts `p_value` at the dotted key under `p_table`, making tables on the way.
  auto set(toml_value &p_table, const std::vector<string_type> &p_key,
           toml_value p_value) -> void {
    auto *table = &p_table;
    for (std::size_t index = 0; index + 1 < p_key.size(); ++index)
      table = &child_table(*table, p_key[index], false);
    for (const auto &entry : table->entries)
      if (entry.key == p_key.back())
        fail("`" + p_key.back() + "` is given twice");
    table->entries.push_back({p_key.back(), std::move(p_value)});
  }

  // The table named `p_key` under `p_table`, made if missing. A header
  // (`p_header`) may reopen a table only made implicitly by a dotted header.
  auto child_table(toml_value &p_table, const string_type &p_key, bool p_header)
      -> toml_value & {
    for (auto &entry : p_table.entries)
      if (entry.key == p_key) {
        if (entry.value.kind != toml_value::kind_type::table)
          fail("`" + p_key + "` is " + string_type(entry.value.kind_name()) +
               ", not a table");
        if (p_header && entry.value.line != 0)
          fail("[" + p_key + "] is given twice");
        return entry.value;
      }
    toml_value table;
    table.line = 0; // made implicitly; a header sets the line
    p_table.entries.push_back({p_key, std::move(table)});
    return p_table.entries.back().value;
  }

  auto run() -> toml_value {
    toml_value root;
    root.line = 1;
    auto *current = &root;
    for (;;) {
      skip_space();
      if (at_end())
        return root;
      if (peek() == '[') {
        advance();
        if (peek() == '[')
          fail("arrays of tables ([[…]]) are not read in ovis.toml");
        const auto header_line = line;
        const auto key = read_key();
        skip_blanks();
        if (peek() != ']')
          fail("expected ] after a table's name");
        advance();
        current = &root;
        for (std::size_t index = 0; index < key.size(); ++index)
          current = &child_table(*current, key[index], index + 1 == key.size());
        current->line = header_line;
        end_line();
        continue;
      }
      const auto key = read_key();
      skip_blanks();
      if (peek() != '=')
        fail("expected = after `" + key.back() + "`");
      advance();
      set(*current, key, read_value());
      end_line();
    }
  }
};

} // namespace

auto parse_toml(string_view_type p_text, string_view_type p_file_name)
    -> toml_value {
  return reader(p_text, p_file_name).run();
}

auto toml_key(string_view_type p_key) -> string_type {
  if (!p_key.empty() && std::ranges::all_of(p_key, is_bare_key_character))
    return string_type(p_key);
  return toml_string(p_key);
}

auto toml_string(string_view_type p_text) -> string_type {
  string_type result = "\"";
  for (const auto character : p_text) {
    switch (character) {
    case '"':
      result += "\\\"";
      break;
    case '\\':
      result += "\\\\";
      break;
    case '\n':
      result += "\\n";
      break;
    case '\t':
      result += "\\t";
      break;
    default:
      result.push_back(character);
    }
  }
  return result + "\"";
}

} // namespace ovis::project
