#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>

// Checked programs (proposals/memory.md §6): a value used after it was freed
// is an error naming the lines, runtime errors name their line, and
// `--report-leaks` lists what was never freed. `--fast` turns the checks off.

using Catch::Matchers::ContainsSubstring;

namespace {

// Runs a program with `ovis run`; its output with standard error mixed in.
auto run_ovis(const std::string &p_name, const std::string &p_source,
              const std::string &p_flags = "") -> std::string {
  const auto folder = std::filesystem::temp_directory_path() / ("ovis-checked-" + p_name);
  std::filesystem::remove_all(folder);
  std::filesystem::create_directories(folder);
  const auto path = folder / "p.ovis";
  std::ofstream(path) << p_source;
  const std::string command = std::string(OVIS_EXECUTABLE) + " run " +
                              path.string() + " --offline --quiet --keep-source " +
                              p_flags + " 2>&1";
  std::string output;
  auto *const pipe = popen(command.c_str(), "r");
  REQUIRE(pipe != nullptr);
  char buffer[256];
  while (const auto count = std::fread(buffer, 1, sizeof(buffer), pipe))
    output.append(buffer, count);
  pclose(pipe);
  std::filesystem::remove_all(folder);
  return output;
}

} // namespace

TEST_CASE("Text used after it was freed is reported with its lines.",
          "[checked]") {
  const auto output = run_ovis("text", "line = \"ab\" + \"cd\"\n"
                                       "show line\n"
                                       "free line\n"
                                       "show line\n");
  REQUIRE_THAT(output, ContainsSubstring("abcd"));
  REQUIRE_THAT(output, ContainsSubstring("this text was freed on line 3"));
  REQUIRE_THAT(output, ContainsSubstring("it was made on line 1"));
  REQUIRE_THAT(output, ContainsSubstring("(line 4)"));
}

TEST_CASE("A double free and a list used after free are reported.",
          "[checked]") {
  REQUIRE_THAT(run_ovis("double", "line = \"ab\" + \"cd\"\nfree line\nfree line\n"),
               ContainsSubstring("already freed on line 2"));
  const auto list = run_ovis("list", "xs = [1, 2]\nfree xs\nshow length of xs\n");
  REQUIRE_THAT(list, ContainsSubstring("this list was freed on line 2"));
  REQUIRE_THAT(list, ContainsSubstring("(line 3)"));
  // Text written in the program is never freed.
  REQUIRE(run_ovis("constant", "line = \"abc\"\nfree line\nshow line\n") ==
          "abc\n");
}

TEST_CASE("A value of a new memory is gone once the block ends.", "[checked]") {
  const auto output = run_ovis("arena", "with new memory:\n"
                                        "    word = \"ab\" + \"cd\"\n"
                                        "    show word\n"
                                        "show word\n");
  REQUIRE_THAT(output, ContainsSubstring("abcd\n"));
  REQUIRE_THAT(output, ContainsSubstring(
                           "made on line 2 in a memory that was freed or "
                           "cleared on line 1"));
  const auto cleared = run_ovis("clear", "scratch = new memory\n"
                                         "with scratch:\n"
                                         "    word = \"ab\" + \"cd\"\n"
                                         "clear scratch\n"
                                         "show word\n");
  REQUIRE_THAT(cleared, ContainsSubstring("in a memory that was freed or cleared"));
}

TEST_CASE("Runtime errors name the line; --fast leaves it out.", "[checked]") {
  const std::string source = "xs = [1]\nshow 1\nshow item 5 of xs\n";
  REQUIRE_THAT(run_ovis("index", source), ContainsSubstring("first one is at index 0) (line 3)"));
  const auto fast = run_ovis("fast", source, "--fast");
  REQUIRE_THAT(fast, ContainsSubstring("first one is at index 0)"));
  REQUIRE_THAT(fast, !ContainsSubstring("(line 3)"));
  // Without checks a freed text is not recognised: the program carries on.
  REQUIRE_THAT(run_ovis("fast-free", "line = \"ab\" + \"cd\"\nfree line\n"
                                     "show \"done\"\n", "--fast"),
               ContainsSubstring("done"));
}

TEST_CASE("Report the values a program never freed.", "[checked]") {
  const auto output = run_ovis("leaks", "count = 0\n"
                                        "while count < 3:\n"
                                        "    label = \"item \" + count\n"
                                        "    count = count + 1\n"
                                        "show count\n",
                               "--report-leaks");
  REQUIRE_THAT(output, ContainsSubstring("3\n"));
  REQUIRE_THAT(output, ContainsSubstring("line 3 made 3 values that were never freed"));
  // What was freed, or lives in a freed memory, is not reported.
  const auto clean = run_ovis("clean", "with new memory:\n"
                                       "    label = \"item \" + 1\n"
                                       "    show label\n"
                                       "other = \"a\" + \"b\"\n"
                                       "free other\n",
                              "--report-leaks");
  REQUIRE_THAT(clean, !ContainsSubstring("never freed"));
}
