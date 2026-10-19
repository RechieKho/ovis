#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include <ovis/phrase/front_end.hpp>
#include <ovis/phrase/language_server.hpp>

// Problems as tools see them (proposals/quick-fix.md): diagnostics with
// their fixes, `ovis check --json` and `ovis fix`.

using Catch::Matchers::ContainsSubstring;

namespace {

// What a shell command prints on standard output and standard error.
auto output_of(const std::string &p_command) -> std::string {
  std::string output;
  auto *const pipe = popen((p_command + " 2>&1").c_str(), "r");
  REQUIRE(pipe != nullptr);
  char buffer[256];
  while (const auto count = std::fread(buffer, 1, sizeof(buffer), pipe))
    output.append(buffer, count);
  pclose(pipe);
  return output;
}

auto read_file(const std::filesystem::path &p_path) -> std::string {
  std::ifstream input(p_path);
  std::stringstream text;
  text << input.rdbuf();
  return text.str();
}

// A program in a fresh folder of its own.
auto program_file(const std::string &p_name, const std::string &p_text)
    -> std::filesystem::path {
  const auto folder = std::filesystem::temp_directory_path() / ("ovis-fix-" + p_name);
  std::filesystem::remove_all(folder);
  std::filesystem::create_directories(folder);
  const auto path = folder / "p.ovis";
  std::ofstream(path) << p_text;
  return path;
}

const std::string unclear = "total = 1\nshow totl\nnumbers = [3, 1, 2]\n"
                            "show mean of numbers\n";

} // namespace

TEST_CASE("Describe problems for tools.", "[phrase][fix]") {
  const auto result = ovis::phrase::compile_phrases(unclear, nullptr);
  REQUIRE(result.diagnostics.size() == 1);
  const auto &problem = result.diagnostics.front();
  // Cited where it is once `totl`'s meaning is written above line 2.
  CHECK(problem.line == 5);
  CHECK(problem.kind == "unclear");
  CHECK(problem.span == "mean of numbers");
  CHECK(problem.column == 6);
  CHECK(problem.end_column == 21);
  // A fix's meaning is the whole line with the reading put in.
  REQUIRE(problem.fixes.size() == 9);
  CHECK(std::ranges::find(problem.fixes,
                          ovis::phrase::quick_fix{"average of numbers",
                                                  "show average of numbers"}) !=
        problem.fixes.end());
  CHECK_THAT(result.problems.front(), ContainsSubstring("line 5: \"mean of"));

  // Without writing the source back, the lines are as read.
  const auto as_read = ovis::phrase::compile_phrases(
      unclear, nullptr, {.sources_written = false});
  CHECK(as_read.diagnostics.front().line == 4);

  // Other kinds: no phrase at all, and the lexer's, with its column.
  const auto none =
      ovis::phrase::compile_phrases("x = 1\nshow x not 3 at all ever\n", nullptr);
  REQUIRE(!none.diagnostics.empty());
  const auto syntax = ovis::phrase::compile_phrases("show (1\n", nullptr);
  REQUIRE(syntax.diagnostics.size() == 1);
  CHECK(syntax.diagnostics.front().kind == "syntax");
  CHECK(syntax.diagnostics.front().column == 6);
  CHECK_THAT(syntax.problems.front(), ContainsSubstring("line 1, column 6:"));
}

TEST_CASE("Check, and take a fix, from the command line.", "[phrase][fix]") {
  const std::string ovis = OVIS_EXECUTABLE;
  const auto path = program_file("check", unclear);

  // `check --json` changes nothing and cites the file as it is.
  const auto json = nlohmann::json::parse(
      output_of(ovis + " check " + path.string() + " --json 2>/dev/null"));
  REQUIRE(json["diagnostics"].size() == 1);
  CHECK(json["diagnostics"][0]["line"] == 4);
  CHECK(json["diagnostics"][0]["kind"] == "unclear");
  CHECK(read_file(path) == unclear);

  // `fix --line N --apply K` writes the reading's meaning above that line.
  const auto fixes = json["diagnostics"][0]["fixes"];
  std::size_t average = 0;
  for (std::size_t index = 0; index < fixes.size(); ++index)
    if (fixes[index]["reading"] == "average of numbers")
      average = index + 1;
  REQUIRE(average > 0);
  output_of(ovis + " fix " + path.string() + " --line 4 --apply " +
            std::to_string(average));
  CHECK(read_file(path) == "total = 1\nshow totl\nnumbers = [3, 1, 2]\n"
                           "@[meaning: show average of numbers]\n"
                           "show mean of numbers\n");
  CHECK(output_of(ovis + " run " + path.string() + " --offline --quiet") ==
        "1\n2.0\n");

  // `fix` asks, and a meaning may be written by hand; an answer that isn't
  // an option is asked again.
  const auto asked = program_file("ask", "show zorp blat\n");
  const auto answers = output_of("printf 'x\\n4\\nshow \"zorp\"\\n' | " + ovis +
                                 " fix " + asked.string() + " --offline");
  CHECK_THAT(answers, ContainsSubstring("What does \"zorp blat\" mean here?"));
  CHECK_THAT(answers, ContainsSubstring("every line reads"));
  CHECK(read_file(asked) == "@[meaning: show \"zorp\"]\nshow zorp blat\n");

  // A build with no terminal never asks.
  const auto piped = program_file("piped", "show zorp blat\n");
  CHECK_THAT(output_of("echo 1 | " + ovis + " run " + piped.string() +
                       " --offline"),
             !ContainsSubstring("What does"));
}

namespace {

auto framed(const nlohmann::json &p_message) -> std::string {
  const auto body = p_message.dump();
  return "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
}

// The messages a server wrote, in order.
auto messages_of(const std::string &p_output) -> std::vector<nlohmann::json> {
  std::vector<nlohmann::json> result;
  for (std::size_t at = 0; (at = p_output.find("Content-Length: ", at)) !=
                           std::string::npos;) {
    const auto length = std::stoul(p_output.substr(at + 16));
    const auto body = p_output.find("\r\n\r\n", at) + 4;
    result.push_back(nlohmann::json::parse(p_output.substr(body, length)));
    at = body + length;
  }
  return result;
}

} // namespace

TEST_CASE("Serve an editor: problems, quick fixes, hover.", "[phrase][fix][lsp]") {
  const auto path = program_file("lsp", unclear);
  const auto uri = "file://" + path.string();
  const auto with_meaning = "x = 1\n@[meaning: show x]\nshow it\n";
  const auto meant = program_file("lsp-meant", with_meaning);
  const auto meant_uri = "file://" + meant.string();
  std::stringstream input;
  input << framed({{"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"}, {"params", nlohmann::json::object()}})
        << framed({{"jsonrpc", "2.0"},
                   {"method", "textDocument/didOpen"},
                   {"params", {{"textDocument", {{"uri", uri}, {"text", unclear}}}}}});
  std::stringstream first;
  ovis::phrase::serve_language(input, first, {});
  const auto opened = messages_of(first.str());
  REQUIRE(opened.size() == 2);
  CHECK(opened[0]["result"]["capabilities"]["hoverProvider"] == true);
  // The problem, where it is in the editor (from 0), with its fixes.
  const auto problems = opened[1]["params"]["diagnostics"];
  REQUIRE(problems.size() == 1);
  CHECK(problems[0]["range"]["start"] == nlohmann::json{{"line", 3}, {"character", 5}});
  CHECK(problems[0]["code"] == "unclear");
  REQUIRE(problems[0]["data"]["fixes"].size() == 9);

  // A quick fix writes the meaning above the line; hover shows a meaning.
  std::stringstream again;
  again << framed({{"jsonrpc", "2.0"},
                   {"method", "textDocument/didOpen"},
                   {"params", {{"textDocument", {{"uri", uri}, {"text", unclear}}}}}})
        << framed({{"jsonrpc", "2.0"},
                   {"method", "textDocument/didOpen"},
                   {"params", {{"textDocument", {{"uri", meant_uri}, {"text", with_meaning}}}}}})
        << framed({{"jsonrpc", "2.0"},
                   {"id", 2},
                   {"method", "textDocument/codeAction"},
                   {"params",
                    {{"textDocument", {{"uri", uri}}},
                     {"range", problems[0]["range"]},
                     {"context", {{"diagnostics", problems}}}}}})
        << framed({{"jsonrpc", "2.0"},
                   {"id", 3},
                   {"method", "textDocument/hover"},
                   {"params", {{"textDocument", {{"uri", meant_uri}}}, {"position", {{"line", 2}, {"character", 0}}}}}})
        << framed({{"jsonrpc", "2.0"}, {"id", 4}, {"method", "shutdown"}})
        << framed({{"jsonrpc", "2.0"}, {"method", "exit"}});
  std::stringstream second;
  CHECK(ovis::phrase::serve_language(again, second, {}) == 0);
  nlohmann::json actions, hover;
  for (const auto &message : messages_of(second.str())) {
    if (message.value("id", 0) == 2)
      actions = message["result"];
    if (message.value("id", 0) == 3)
      hover = message["result"];
  }
  REQUIRE(actions.size() == 9);
  nlohmann::json average;
  for (const auto &action : actions)
    if (action["title"] == "Read as: average of numbers")
      average = action;
  REQUIRE(!average.is_null());
  const auto edit = average["edit"]["changes"][uri][0];
  CHECK(edit["newText"] == "@[meaning: show average of numbers]\n");
  CHECK(edit["range"]["start"] == nlohmann::json{{"line", 3}, {"character", 0}});
  CHECK_THAT(hover["contents"]["value"].get<std::string>(),
             ContainsSubstring("show x"));
}
