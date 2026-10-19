#include <filesystem>
#include <istream>
#include <map>
#include <optional>
#include <ostream>
#include <set>
#include <string>

#include <nlohmann/json.hpp>

#include <ovis/phrase/front_end.hpp>
#include <ovis/phrase/language_server.hpp>

namespace ovis::phrase {

namespace {

using json = nlohmann::json;

// One message: headers, a blank line, then `Content-Length` bytes of JSON.
auto read_message(std::istream &p_input) -> std::optional<json> {
  std::size_t length = 0;
  for (std::string header; std::getline(p_input, header);) {
    if (!header.empty() && header.back() == '\r')
      header.pop_back();
    if (header.empty()) {
      std::string body(length, '\0');
      if (!p_input.read(body.data(), static_cast<std::streamsize>(length)))
        return std::nullopt;
      return json::parse(body, nullptr, false);
    }
    constexpr std::string_view name = "Content-Length:";
    if (header.starts_with(name))
      length = std::stoul(header.substr(name.size()));
  }
  return std::nullopt;
}

auto write_message(std::ostream &p_output, const json &p_message) -> void {
  const auto body = p_message.dump();
  p_output << "Content-Length: " << body.size() << "\r\n\r\n" << body;
  p_output.flush();
}

auto is_hex(char_type p_character) -> bool {
  return std::isxdigit(static_cast<unsigned char>(p_character)) != 0;
}

// `file:///a/b%20c.ovis` is `/a/b c.ovis`.
auto path_of(const std::string &p_uri) -> std::filesystem::path {
  auto text = p_uri.starts_with("file://") ? p_uri.substr(7) : p_uri;
  std::string decoded;
  for (std::size_t index = 0; index < text.size(); ++index) {
    if (text[index] == '%' && index + 2 < text.size() && is_hex(text[index + 1]) &&
        is_hex(text[index + 2])) {
      decoded.push_back(
          static_cast<char_type>(std::stoi(text.substr(index + 1, 2), nullptr, 16)));
      index += 2;
    } else {
      decoded.push_back(text[index]);
    }
  }
  return std::filesystem::path(decoded).lexically_normal();
}

auto uri_of(const std::filesystem::path &p_path) -> std::string {
  static constexpr char digits[] = "0123456789ABCDEF";
  std::string result = "file://";
  for (const auto character : std::filesystem::absolute(p_path).lexically_normal().string()) {
    const auto byte = static_cast<unsigned char>(character);
    if (std::isalnum(byte) != 0 || std::string_view("/-._~").find(character) !=
                                       std::string_view::npos) {
      result.push_back(character);
    } else {
      result.push_back('%');
      result.push_back(digits[byte >> 4]);
      result.push_back(digits[byte & 15]);
    }
  }
  return result;
}

auto lines_of(std::string_view p_text) -> std::vector<std::string> {
  std::vector<std::string> result;
  for (std::size_t begin = 0; begin <= p_text.size();) {
    auto end = p_text.find('\n', begin);
    if (end == std::string_view::npos)
      end = p_text.size();
    auto line = std::string(p_text.substr(begin, end - begin));
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    result.push_back(std::move(line));
    begin = end + 1;
  }
  return result;
}

// The meaning a `@[meaning: …]` line holds, or nothing.
auto meaning_in(const std::string &p_line) -> std::optional<std::string> {
  const auto first = p_line.find_first_not_of(' ');
  if (first == std::string::npos ||
      !std::string_view(p_line).substr(first).starts_with("@[meaning:"))
    return std::nullopt;
  const auto close = p_line.rfind(']');
  if (close == std::string::npos || close < first + 10)
    return std::nullopt;
  auto meaning = p_line.substr(first + 10, close - first - 10);
  meaning.erase(0, meaning.find_first_not_of(' '));
  return meaning;
}

class server {
  std::ostream &output;
  core::c_import_options imports;
  // Open documents by URI: their text in the editor, saved or not.
  std::map<std::string, std::string> open;

public:
  server(std::ostream &p_output, core::c_import_options p_imports)
      : output(p_output), imports(std::move(p_imports)) {}

  auto respond(const json &p_id, json p_result) -> void {
    write_message(output, {{"jsonrpc", "2.0"}, {"id", p_id}, {"result", std::move(p_result)}});
  }

  auto notify(const std::string &p_method, json p_params) -> void {
    write_message(output,
                  {{"jsonrpc", "2.0"}, {"method", p_method}, {"params", std::move(p_params)}});
  }

  // The text of a document: the editor's if open, else the file's.
  auto text_of(const std::string &p_uri) -> std::string {
    if (const auto found = open.find(p_uri); found != open.end())
      return found->second;
    return {};
  }

  // Checks the program the document belongs to and publishes the problems
  // of each of its files.
  auto check(const std::string &p_uri) -> void {
    auto path = path_of(p_uri);
    // A file of a folder program is read with the whole program.
    if (path.filename() != "main.ovis" &&
        std::filesystem::exists(path.parent_path() / "main.ovis"))
      path = path.parent_path();
    std::vector<phrase_file> files;
    try {
      files = load_phrase_program(path);
    } catch (const error &p_error) {
      publish(p_uri, json::array({problem_json(0, 0, 0, "error", p_error.what(), {})}));
      return;
    }
    for (auto &file : files)
      if (const auto found = open.find(uri_of(file.path)); found != open.end())
        file.text = found->second;
    const auto result = compile_phrase_program(
        files, nullptr,
        {.c_imports = imports, .rewrite = false, .sources_written = false, .strict_memory = true});
    std::vector<json> problems(files.size(), json::array());
    for (const auto &problem : result.diagnostics) {
      json fixes = json::array();
      for (const auto &fix : problem.fixes)
        fixes.push_back({{"reading", fix.reading}, {"meaning", fix.meaning}});
      problems[problem.file].push_back(problem_json(problem.line, problem.column,
                                                    problem.end_column, problem.kind,
                                                    problem.message, fixes));
    }
    for (std::size_t index = 0; index < files.size(); ++index)
      publish(uri_of(files[index].path), problems[index]);
  }

  static auto problem_json(std::size_t p_line, std::size_t p_column,
                           std::size_t p_end_column, const std::string &p_kind,
                           const std::string &p_message, json p_fixes) -> json {
    const auto line = p_line > 0 ? p_line - 1 : 0;
    // A whole line when the words aren't known.
    const auto begin = p_column > 0 ? p_column - 1 : 0;
    const auto end = p_end_column > 0 ? p_end_column - 1 : 1000;
    return {{"range",
             {{"start", {{"line", line}, {"character", begin}}},
              {"end", {{"line", line}, {"character", end}}}}},
            {"severity", 1},
            {"source", "ovis"},
            {"code", p_kind},
            {"message", p_message},
            {"data", {{"fixes", std::move(p_fixes)}}}};
  }

  auto publish(const std::string &p_uri, json p_problems) -> void {
    notify("textDocument/publishDiagnostics",
           {{"uri", p_uri}, {"diagnostics", std::move(p_problems)}});
  }

  // Each fix of the problems at the range: `@[meaning: …]` written above
  // the line, at its indentation, in place of a meaning already there.
  auto code_actions(const json &p_params) -> json {
    const auto uri = p_params["textDocument"]["uri"].get<std::string>();
    const auto lines = lines_of(text_of(uri));
    json actions = json::array();
    for (const auto &problem : p_params["context"]["diagnostics"]) {
      if (!problem.contains("data") || !problem["data"].contains("fixes"))
        continue;
      const auto line = problem["range"]["start"]["line"].get<std::size_t>();
      if (line >= lines.size())
        continue;
      const auto &written = lines[line];
      const auto indent = written.substr(0, written.find_first_not_of(' '));
      const bool replaces = line > 0 && meaning_in(lines[line - 1]).has_value();
      for (const auto &fix : problem["data"]["fixes"]) {
        const auto sigil = indent + "@[meaning: " + fix["meaning"].get<std::string>() + "]";
        const json edit =
            replaces ? json{{"range",
                             {{"start", {{"line", line - 1}, {"character", 0}}},
                              {"end", {{"line", line - 1}, {"character", lines[line - 1].size()}}}}},
                            {"newText", sigil}}
                     : json{{"range",
                             {{"start", {{"line", line}, {"character", 0}}},
                              {"end", {{"line", line}, {"character", 0}}}}},
                            {"newText", sigil + "\n"}};
        actions.push_back({{"title", "Read as: " + fix["reading"].get<std::string>()},
                           {"kind", "quickfix"},
                           {"diagnostics", json::array({problem})},
                           {"edit", {{"changes", {{uri, json::array({edit})}}}}}});
      }
    }
    return actions;
  }

  // On a line under a meaning: how it is read. On the meaning: who wrote it,
  // by the lockfile (a meaning it doesn't know is the author's).
  auto hover(const json &p_params) -> json {
    const auto uri = p_params["textDocument"]["uri"].get<std::string>();
    const auto lines = lines_of(text_of(uri));
    const auto line = p_params["position"]["line"].get<std::size_t>();
    if (line >= lines.size())
      return nullptr;
    std::string text;
    if (const auto meaning = meaning_in(lines[line])) {
      const auto lock = lockfile::load(lockfile::path_for(path_of(uri)));
      const auto *const entry = lock.find_meaning(*meaning);
      const auto below = line + 1 < lines.size() ? lines[line + 1] : std::string();
      const auto first = below.find_first_not_of(' ');
      const auto written = first == std::string::npos ? std::string() : below.substr(first);
      text = entry == nullptr
                 ? "Written by you: Ovis reads the line below as this."
                 : entry->line == written
                       ? "Written by Ovis when it settled the line below."
                       : "Written by Ovis for an earlier version of the line "
                         "below: the next build reads it again.";
    } else if (line > 0) {
      if (const auto meaning = meaning_in(lines[line - 1]))
        text = "Ovis reads this line as:\n\n```ovis\n" + *meaning + "\n```";
    }
    if (text.empty())
      return nullptr;
    return {{"contents", {{"kind", "markdown"}, {"value", text}}}};
  }

  // Handles a message; false once the editor says `exit`.
  auto handle(const json &p_message) -> bool {
    const auto method = p_message.value("method", std::string());
    const auto &params = p_message.contains("params") ? p_message["params"] : json::object();
    const auto has_id = p_message.contains("id");
    if (method == "initialize") {
      respond(p_message["id"],
              {{"capabilities",
                {{"textDocumentSync",
                  {{"openClose", true}, {"change", 1}, {"save", {{"includeText", false}}}}},
                 {"codeActionProvider", {{"codeActionKinds", {"quickfix"}}}},
                 {"hoverProvider", true}}},
               {"serverInfo", {{"name", "ovis"}}}});
    } else if (method == "textDocument/didOpen") {
      const auto uri = params["textDocument"]["uri"].get<std::string>();
      open[uri] = params["textDocument"]["text"].get<std::string>();
      check(uri);
    } else if (method == "textDocument/didChange") {
      // Kept up to date, checked when saved: a build of every keystroke
      // would lag.
      const auto uri = params["textDocument"]["uri"].get<std::string>();
      for (const auto &change : params["contentChanges"])
        if (change.contains("text") && !change.contains("range"))
          open[uri] = change["text"].get<std::string>();
    } else if (method == "textDocument/didSave") {
      check(params["textDocument"]["uri"].get<std::string>());
    } else if (method == "textDocument/didClose") {
      const auto uri = params["textDocument"]["uri"].get<std::string>();
      open.erase(uri);
      publish(uri, json::array());
    } else if (method == "textDocument/codeAction") {
      respond(p_message["id"], code_actions(params));
    } else if (method == "textDocument/hover") {
      respond(p_message["id"], hover(params));
    } else if (method == "shutdown") {
      respond(p_message["id"], nullptr);
    } else if (method == "exit") {
      return false;
    } else if (has_id) {
      write_message(output, {{"jsonrpc", "2.0"},
                             {"id", p_message["id"]},
                             {"error", {{"code", -32601}, {"message", "Not supported: " + method}}}});
    }
    return true;
  }
};

} // namespace

auto serve_language(std::istream &p_input, std::ostream &p_output,
                    const core::c_import_options &p_imports) -> int {
  server instance(p_output, p_imports);
  while (const auto message = read_message(p_input)) {
    if (message->is_discarded())
      continue;
    try {
      if (!instance.handle(*message))
        return 0;
    } catch (const std::exception &p_error) {
      // A request the server can't answer is the editor's to show, not a
      // reason to stop serving.
      if (message->contains("id"))
        write_message(p_output, {{"jsonrpc", "2.0"},
                                 {"id", (*message)["id"]},
                                 {"error", {{"code", -32603}, {"message", p_error.what()}}}});
    }
  }
  return 0;
}

} // namespace ovis::phrase
