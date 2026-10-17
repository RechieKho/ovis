#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <ovis/english/language_model.hpp>
#include <ovis/english/lockfile.hpp>
#include <ovis/english/paragraph.hpp>

namespace ovis::english {

namespace {

auto is_blank(string_view_type p_line) -> bool {
  for (const auto character : p_line)
    if (!std::isspace(static_cast<unsigned char>(character)))
      return false;
  return true;
}


} // namespace

// Paragraphs.

auto split_paragraphs(string_view_type p_source) -> std::vector<paragraph> {
  std::vector<paragraph> result;
  std::optional<paragraph> current;
  std::size_t line_number = 0;
  std::size_t position = 0;
  while (position <= p_source.size()) {
    auto end = p_source.find('\n', position);
    if (end == string_view_type::npos)
      end = p_source.size();
    const auto line = p_source.substr(position, end - position);
    ++line_number;
    position = end + 1;

    if (is_blank(line)) {
      if (current)
        result.push_back(std::move(*current));
      current.reset();
      continue;
    }
    if (!current)
      current = paragraph{line_number, 1, {}};
    else
      current->text.push_back('\n');
    current->line_count = line_number - current->line + 1;
    current->text += line;
    if (!current->text.empty() && current->text.back() == '\r')
      current->text.pop_back();
  }
  if (current)
    result.push_back(std::move(*current));
  return result;
}

auto normalize(string_view_type p_text) -> string_type {
  string_type result;
  bool in_space = false;
  for (const auto character : p_text) {
    if (std::isspace(static_cast<unsigned char>(character))) {
      in_space = true;
      continue;
    }
    if (in_space && !result.empty())
      result.push_back(' ');
    in_space = false;
    result.push_back(character);
  }
  return result;
}


// Detailed paragraphs.

namespace {

auto trim(string_view_type p_text) -> string_view_type {
  while (!p_text.empty() &&
         std::isspace(static_cast<unsigned char>(p_text.front())))
    p_text.remove_prefix(1);
  while (!p_text.empty() &&
         std::isspace(static_cast<unsigned char>(p_text.back())))
    p_text.remove_suffix(1);
  return p_text;
}

auto starts_indented(string_view_type p_line) -> bool {
  return !p_line.empty() && (p_line.front() == ' ' || p_line.front() == '\t');
}

constexpr string_view_type assumption_label = "Assumption:";
constexpr string_view_type compiler_label = "Compiler:";
constexpr std::size_t wrap_width = 80;

// Appends `p_text` after `p_prefix`, wrapping at word boundaries; wrapped
// lines are indented by `p_indent` spaces.
auto wrap(string_type &p_output, string_view_type p_prefix,
          string_view_type p_text, std::size_t p_indent) -> void {
  string_type line(p_prefix);
  bool line_has_word = false;
  std::size_t position = 0;
  while (position < p_text.size()) {
    while (position < p_text.size() && p_text[position] == ' ')
      ++position;
    auto end = p_text.find(' ', position);
    if (end == string_view_type::npos)
      end = p_text.size();
    const auto word = p_text.substr(position, end - position);
    position = end;
    if (word.empty())
      continue;
    if (line_has_word && line.size() + 1 + word.size() > wrap_width) {
      p_output += line + '\n';
      line = string_type(p_indent, ' ');
      line_has_word = false;
    }
    if (line_has_word)
      line.push_back(' ');
    line += word;
    line_has_word = true;
  }
  p_output += line + '\n';
}

} // namespace

auto parse_detailed(string_view_type p_text) -> detailed_paragraph {
  detailed_paragraph result;
  // Where continuation lines go.
  string_type *current = nullptr;

  bool in_detail = false;
  std::size_t position = 0;
  while (position <= p_text.size()) {
    auto end = p_text.find('\n', position);
    if (end == string_view_type::npos)
      end = p_text.size();
    const auto line = p_text.substr(position, end - position);
    position = end + 1;

    if (!in_detail && !starts_indented(line)) {
      if (!result.intent.empty())
        result.intent.push_back('\n');
      result.intent += line;
      continue;
    }
    in_detail = true;
    result.has_detail = true;
    const auto content = trim(line);
    if (content.empty())
      continue;
    std::size_t indentation = 0;
    for (const auto character : line) {
      if (character == ' ')
        ++indentation;
      else if (character == '\t')
        indentation += 4;
      else
        break;
    }
    // Items are indented by 4 and wrapped lines by more, so a wrapped line
    // that happens to start with "30." stays part of its item.
    if (indentation > 5 && current != nullptr) {
      *current += ' ';
      *current += content;
      continue;
    }

    std::size_t digits = 0;
    while (digits < content.size() &&
           std::isdigit(static_cast<unsigned char>(content[digits])))
      ++digits;
    if (digits > 0 && digits < content.size() &&
        (content[digits] == '.' || content[digits] == ')')) {
      result.steps.emplace_back(trim(content.substr(digits + 1)));
      current = &result.steps.back();
    } else if (content.starts_with(assumption_label)) {
      result.assumptions.emplace_back(
          trim(content.substr(assumption_label.size())));
      current = &result.assumptions.back();
    } else if (content.starts_with(compiler_label)) {
      result.compiler_notes.emplace_back(
          trim(content.substr(compiler_label.size())));
      current = &result.compiler_notes.back();
    } else if (current != nullptr) {
      *current += ' ';
      *current += content;
    } else {
      // Detail written without numbers: each line is a step.
      result.steps.emplace_back(content);
      current = &result.steps.back();
    }
  }
  return result;
}

auto render_detailed(string_view_type p_intent,
                     const std::vector<string_type> &p_steps,
                     const std::vector<string_type> &p_assumptions,
                     const std::vector<string_type> &p_compiler_notes)
    -> string_type {
  string_type result(p_intent);
  result.push_back('\n');
  for (std::size_t index = 0; index < p_steps.size(); ++index) {
    const auto prefix = "    " + std::to_string(index + 1) + ". ";
    wrap(result, prefix, p_steps[index], prefix.size());
  }
  for (const auto &assumption : p_assumptions)
    wrap(result, "    " + string_type(assumption_label) + " ", assumption, 8);
  for (const auto &note : p_compiler_notes)
    wrap(result, "    " + string_type(compiler_label) + " ", note, 8);
  result.pop_back();
  return result;
}

auto numbered_steps(const std::vector<string_type> &p_steps) -> string_type {
  string_type result;
  for (std::size_t index = 0; index < p_steps.size(); ++index) {
    if (index != 0)
      result.push_back('\n');
    result += std::to_string(index + 1) + ". " + normalize(p_steps[index]);
  }
  return result;
}

auto replace_paragraphs(string_view_type p_source,
                        const std::vector<paragraph> &p_paragraphs,
                        const std::vector<string_type> &p_texts,
                        std::vector<std::size_t> *p_new_lines)
    -> string_type {
  std::size_t output_line = 1;
  std::vector<string_view_type> lines;
  std::size_t position = 0;
  while (position <= p_source.size()) {
    auto end = p_source.find('\n', position);
    if (end == string_view_type::npos)
      end = p_source.size();
    lines.push_back(p_source.substr(position, end - position));
    position = end + 1;
  }

  string_type result;
  std::size_t next = 0;
  for (std::size_t line = 1; line <= lines.size(); ++line) {
    if (next < p_paragraphs.size() && p_paragraphs[next].line == line) {
      line += p_paragraphs[next].line_count - 1;
      if (p_new_lines != nullptr)
        p_new_lines->push_back(output_line);
      // An empty text removes the paragraph, with the blank line after it.
      if (p_texts[next].empty()) {
        ++next;
        if (line < lines.size() && trim(lines[line]).empty())
          ++line;
        continue;
      }
      result += p_texts[next];
      output_line += static_cast<std::size_t>(
          std::ranges::count(p_texts[next], '\n'));
      ++next;
    } else {
      result += lines[line - 1];
    }
    if (line < lines.size()) {
      result.push_back('\n');
      ++output_line;
    }
  }
  // Removing the last paragraph leaves the blank line before it behind.
  while (result.ends_with("\n\n") && !p_source.ends_with("\n\n"))
    result.pop_back();
  return result;
}

// Lockfile.

auto status_name(detail_result::status_type p_status) -> string_view_type {
  switch (p_status) {
  case detail_result::status_type::ok:
    return "ok";
  case detail_result::status_type::note:
    return "note";
  case detail_result::status_type::too_vague:
    return "too_vague";
  case detail_result::status_type::irrelevant:
    return "irrelevant";
  case detail_result::status_type::mismatch:
    return "mismatch";
  }
  return "ok";
}

namespace {

auto parse_status(string_view_type p_name) -> detail_result::status_type {
  for (const auto status :
       {detail_result::status_type::ok, detail_result::status_type::note,
        detail_result::status_type::too_vague,
        detail_result::status_type::irrelevant,
        detail_result::status_type::mismatch})
    if (status_name(status) == p_name)
      return status;
  throw error("Unknown detail status \"" + string_type(p_name) + "\".");
}

} // namespace

auto lockfile::path_for(const std::filesystem::path &p_source)
    -> std::filesystem::path {
  auto result = p_source;
  result += ".lock";
  return result;
}

auto lockfile::load(const std::filesystem::path &p_path) -> lockfile {
  lockfile result;
  std::ifstream input(p_path);
  if (!input)
    return result;
  try {
    const auto document = nlohmann::json::parse(input);
    result.model = document.value("model", "");
    result.model_fingerprint = document.value("model_fingerprint", "");
    // Version 1 lockfiles predate the detail pass; their translations no
    // longer match anything, so they are dropped.
    if (document.value("ovis_lock_version", 1) < 2)
      return result;
    for (const auto &item : document.at("details")) {
      detail_entry entry{item.at("english").get<string_type>()};
      auto &detail = entry.detail;
      detail.status = parse_status(item.at("status").get<string_type>());
      detail.steps = item.value("steps", std::vector<string_type>());
      detail.assumptions =
          item.value("assumptions", std::vector<string_type>());
      detail.reason = item.value("reason", "");
      detail.suggestion = item.value("suggestion", "");
      result.details.push_back(std::move(entry));
    }
    for (const auto &item : document.at("translations"))
      result.translations.push_back({item.at("steps").get<string_type>(),
                                     item.at("core").get<string_type>()});
  } catch (const nlohmann::json::exception &p_error) {
    throw error("Cannot read " + p_path.string() + ": " + p_error.what());
  }
  return result;
}

auto lockfile::to_text() const -> string_type {
  nlohmann::ordered_json document;
  document["ovis_lock_version"] = 2;
  document["model"] = model;
  if (!model_fingerprint.empty())
    document["model_fingerprint"] = model_fingerprint;
  document["details"] = nlohmann::ordered_json::array();
  for (const auto &entry : details) {
    const auto &detail = entry.detail;
    nlohmann::ordered_json item = {{"english", entry.english},
                                   {"status", status_name(detail.status)}};
    if (!detail.steps.empty())
      item["steps"] = detail.steps;
    if (!detail.assumptions.empty())
      item["assumptions"] = detail.assumptions;
    if (!detail.reason.empty())
      item["reason"] = detail.reason;
    if (!detail.suggestion.empty())
      item["suggestion"] = detail.suggestion;
    document["details"].push_back(std::move(item));
  }
  document["translations"] = nlohmann::ordered_json::array();
  for (const auto &entry : translations)
    document["translations"].push_back(
        {{"steps", entry.steps}, {"core", entry.core}});
  return document.dump(2) + '\n';
}

auto lockfile::save(const std::filesystem::path &p_path) const -> void {
  std::ofstream output(p_path);
  if (!output)
    throw error("Cannot write " + p_path.string() + ".");
  output << to_text();
}

auto lockfile::find_detail(string_view_type p_english) const
    -> const detail_entry * {
  const auto key = normalize(p_english);
  for (const auto &entry : details)
    if (normalize(entry.english) == key)
      return &entry;
  return nullptr;
}

auto lockfile::find_origin(const std::vector<string_type> &p_steps) const
    -> const detail_entry * {
  std::vector<string_type> steps;
  for (const auto &step : p_steps)
    steps.push_back(normalize(step));
  const detail_entry *best = nullptr;
  std::size_t best_shared = 0;
  for (const auto &entry : details) {
    if (entry.detail.status != detail_result::status_type::ok)
      continue;
    std::size_t shared = 0;
    for (const auto &step : entry.detail.steps)
      if (std::ranges::find(steps, normalize(step)) != steps.end())
        ++shared;
    const auto size = std::max(steps.size(), entry.detail.steps.size());
    if (shared > best_shared && shared * 2 >= size) {
      best = &entry;
      best_shared = shared;
    }
  }
  return best;
}

auto lockfile::find_translation(string_view_type p_steps) const
    -> std::optional<string_type> {
  const auto key = normalize(p_steps);
  for (const auto &entry : translations)
    if (normalize(entry.steps) == key)
      return entry.core;
  return std::nullopt;
}

// Endpoint model.

auto endpoint_model::chat(const std::vector<chat_message> &p_messages,
                          string_view_type p_grammar) -> string_type {
  auto base = options.endpoint;
  while (!base.empty() && base.back() == '/')
    base.pop_back();
  const auto scheme_end = base.find("://");
  const auto path_begin =
      base.find('/', scheme_end == string_type::npos ? 0 : scheme_end + 3);
  const auto host = base.substr(0, path_begin);
  const auto path =
      (path_begin == string_type::npos ? string_type() : base.substr(path_begin)) +
      "/chat/completions";

  nlohmann::json request = {
      {"model", options.model},
      {"temperature", 0},
      {"max_tokens", options.max_tokens},
      {"messages", nlohmann::json::array()},
  };
  for (const auto &message : p_messages)
    request["messages"].push_back(
        {{"role", message.role}, {"content", message.content}});
  if (options.use_grammar && !p_grammar.empty())
    request["grammar"] = p_grammar;

  httplib::Client client(host);
  client.set_connection_timeout(10);
  client.set_read_timeout(options.timeout_seconds);
  httplib::Headers headers;
  if (!options.api_key.empty())
    headers.emplace("Authorization", "Bearer " + options.api_key);

  const auto response =
      client.Post(path, headers, request.dump(), "application/json");
  if (!response)
    throw error("Cannot reach the language model at " + options.endpoint +
                " (" + httplib::to_string(response.error()) +
                "). Start a local server such as `llama-server`, pass "
                "--endpoint, or use --offline to build from the lockfile "
                "only.");
  if (response->status != 200)
    throw error("The language model at " + options.endpoint +
                " answered HTTP " + std::to_string(response->status) + ": " +
                response->body.substr(0, 500));
  try {
    const auto reply = nlohmann::json::parse(response->body);
    return reply.at("choices").at(0).at("message").at("content");
  } catch (const nlohmann::json::exception &p_error) {
    throw error("Unexpected reply from the language model: " +
                string_type(p_error.what()));
  }
}

} // namespace ovis::english
