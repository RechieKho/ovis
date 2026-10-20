#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#include <catch2/catch_test_macros.hpp>

#include <ovis/phrase/front_end.hpp>
#include <ovis/phrase/templates.hpp>

#include "support.hpp"

namespace {

// A program of test/phrase_library.txt:
//
//   == name
//   program lines
//   -- input
//   lines given to it
//   -- output
//   what it prints
struct library_case {
  std::string name;
  std::string program;
  std::string input;
  std::string output;
};

// Without trailing blank lines.
auto trimmed(std::string p_text) -> std::string {
  while (p_text.ends_with("\n\n"))
    p_text.pop_back();
  return p_text;
}

auto read_library_cases() -> std::vector<library_case> {
  std::ifstream file(std::filesystem::path(OVIS_SOURCE_DIR) / "test" /
                     "phrase_library.txt");
  REQUIRE(file);
  std::vector<library_case> result;
  std::string *section = nullptr;
  for (std::string line; std::getline(file, line);) {
    if (line.starts_with("== ")) {
      result.push_back({line.substr(3)});
      section = &result.back().program;
    } else if (line == "-- input" && !result.empty()) {
      section = &result.back().input;
    } else if (line == "-- output" && !result.empty()) {
      section = &result.back().output;
    } else if (section != nullptr) {
      *section += line + "\n";
    }
  }
  // Blank lines between cases belong to none.
  for (auto &item : result)
    for (auto *text : {&item.program, &item.input, &item.output})
      *text = trimmed(*text);
  return result;
}

} // namespace

TEST_CASE("Every built-in phrase works as its test program shows.",
          "[phrase][library]") {
  std::set<std::string> used;
  for (const auto &item : read_library_cases()) {
    INFO(item.name);
    const auto result = ovis::phrase::compile_phrases(item.program, nullptr);
    std::string problems;
    for (const auto &problem : result.problems)
      problems += problem + "\n";
    INFO(problems);
    REQUIRE(result.succeeded());
    // A question settled offline would hide which phrase was meant.
    REQUIRE(result.timing.questions == 0);
    used.insert(result.templates_used.begin(), result.templates_used.end());
    REQUIRE(trimmed(run_program(result.program, item.input)) == item.output);
  }
  for (const auto &item : ovis::phrase::builtin_templates()) {
    const auto name = item.file + ":" + std::to_string(item.line);
    INFO(name + " " + ovis::phrase::to_string(item, item.patterns[0]));
    CHECK(used.contains(name));
  }
}
