#include <cctype>
#include <optional>
#include <vector>

#include <ovis/phrase/front_end.hpp>

namespace ovis::phrase {

namespace {

auto indentation(const string_type &p_line) -> std::size_t {
  const auto first = p_line.find_first_not_of(' ');
  return first == string_type::npos ? p_line.size() : first;
}

auto trimmed(const string_type &p_line) -> string_type {
  const auto first = p_line.find_first_not_of(" \t");
  if (first == string_type::npos)
    return {};
  const auto last = p_line.find_last_not_of(" \t\r");
  return p_line.substr(first, last - first + 1);
}

// `@[KIND: TEXT]`: its kind and text, the `]` the first outside a text.
auto sigil_of(const string_type &p_content)
    -> std::optional<std::pair<string_type, string_type>> {
  if (!p_content.starts_with("@["))
    return std::nullopt;
  std::size_t close = 2;
  for (bool in_text = false; close < p_content.size(); ++close) {
    if (p_content[close] == '"')
      in_text = !in_text;
    else if (p_content[close] == ']' && !in_text)
      break;
  }
  if (close + 1 != p_content.size())
    return std::nullopt;
  const auto inside = p_content.substr(2, close - 2);
  const auto colon = inside.find(':');
  if (colon == string_type::npos)
    return std::nullopt;
  const auto kind = trimmed(inside.substr(0, colon));
  if (kind != "meaning" && kind != "note")
    return std::nullopt;
  return std::pair(kind, trimmed(inside.substr(colon + 1)));
}

// `"SPAN" is READING` put into the line, the span as whole words, as
// builds do; nothing if the span isn't there.
auto with_decision(const string_type &p_line, const string_type &p_decision)
    -> std::optional<string_type> {
  if (!p_decision.starts_with('"'))
    return std::nullopt;
  const auto separator = p_decision.find("\" is ", 1);
  if (separator == string_type::npos)
    return std::nullopt;
  const auto span = p_decision.substr(1, separator - 1);
  const auto reading = p_decision.substr(separator + 5);
  const auto is_word = [](char_type p_character) {
    return std::isalnum(static_cast<unsigned char>(p_character)) != 0 ||
           p_character == '_';
  };
  for (auto at = p_line.find(span); !span.empty() && at != string_type::npos;
       at = p_line.find(span, at + 1)) {
    const auto starts = at == 0 || !is_word(span.front()) || !is_word(p_line[at - 1]);
    const auto ends = at + span.size() == p_line.size() || !is_word(span.back()) ||
                      !is_word(p_line[at + span.size()]);
    if (starts && ends)
      return p_line.substr(0, at) + reading + p_line.substr(at + span.size());
  }
  return std::nullopt;
}

} // namespace

auto formatted_source(string_view_type p_source) -> string_type {
  std::vector<string_type> lines;
  for (std::size_t begin = 0; begin <= p_source.size();) {
    auto end = p_source.find('\n', begin);
    if (end == string_view_type::npos)
      end = p_source.size();
    lines.emplace_back(p_source.substr(begin, end - begin));
    begin = end + 1;
  }
  const auto is_old_note = [](const string_type &p_content) {
    return p_content.starts_with("Read as:") || p_content.starts_with("Compiler:");
  };

  std::vector<string_type> output;
  // Sigils waiting for the line under them, as written.
  std::vector<string_type> pending;
  for (std::size_t index = 0; index < lines.size(); ++index) {
    const auto &line = lines[index];
    const auto content = trimmed(line);
    if (sigil_of(content)) {
      pending.push_back(line);
      continue;
    }
    // A blank line, a comment or an annotation no line owns: as written,
    // after any sigils left stranded (the build reports them).
    if (content.empty() || content.starts_with('#') || is_old_note(content)) {
      std::ranges::move(pending, std::back_inserter(output));
      pending.clear();
      output.push_back(line);
      continue;
    }

    // The line's meaning and notes: its sigils, then old lines under it.
    std::optional<string_type> meaning;
    std::vector<string_type> notes;
    for (const auto &written : pending) {
      const auto [kind, text] = *sigil_of(trimmed(written));
      if (kind == "meaning")
        meaning = text;
      else
        notes.push_back(text);
    }
    pending.clear();
    const auto indent = indentation(line);
    auto under = index + 1;
    for (; under < lines.size() && indentation(lines[under]) > indent &&
           is_old_note(trimmed(lines[under]));
         ++under) {
      const auto old = trimmed(lines[under]);
      if (old.starts_with("Compiler:")) {
        notes.push_back(trimmed(old.substr(9)));
      } else if (const auto read =
                     with_decision(meaning.value_or(content), trimmed(old.substr(8)))) {
        meaning = *read;
      }
    }
    index = under - 1;

    const string_type prefix(indent, ' ');
    // A meaning the same as the line says nothing.
    if (meaning && *meaning != content)
      output.push_back(prefix + "@[meaning: " + *meaning + "]");
    for (const auto &note : notes)
      output.push_back(prefix + "@[note: " + note + "]");
    output.push_back(line);
  }
  std::ranges::move(pending, std::back_inserter(output));

  string_type result;
  for (std::size_t index = 0; index < output.size(); ++index)
    result += output[index] + (index + 1 < output.size() ? "\n" : "");
  return result;
}

} // namespace ovis::phrase
