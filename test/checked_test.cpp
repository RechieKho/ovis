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
                              path.string() + " --offline --quiet --keep-source --lax-memory " +
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
  REQUIRE_THAT(output, ContainsSubstring("the text in 'line' was freed on line 3"));
  REQUIRE_THAT(output, ContainsSubstring("it was made on line 1"));
  REQUIRE_THAT(output, ContainsSubstring("(line 4)"));
}

TEST_CASE("Freed text given to C is reported, named by its variable.",
          "[checked]") {
  // `length of` is C's strlen, which would read freed memory silently.
  const auto output = run_ovis("strlen", "fan speed = \"ab\" + \"cd\"\n"
                                         "free fan speed\n"
                                         "show length of fan speed\n");
  REQUIRE_THAT(output, ContainsSubstring(
                           "the text in 'fan speed' was freed on line 2; it was "
                           "made on line 1 (line 3)"));
}

TEST_CASE("A double free and a list used after free are reported.",
          "[checked]") {
  REQUIRE_THAT(run_ovis("double", "line = \"ab\" + \"cd\"\nfree line\nfree line\n"),
               ContainsSubstring("already freed on line 2"));
  const auto list = run_ovis("list", "xs = [1, 2]\nfree xs\nshow length of xs\n");
  REQUIRE_THAT(list, ContainsSubstring("the list in 'xs' was freed on line 2"));
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
  // A global list is given its value by its line, not an empty list first
  // that nothing frees; a memory cleared and freed holds nothing.
  const auto global = run_ovis("global", "xs = [1, 2]\n"
                                         "at the end, free xs\n"
                                         "scratch = new memory\n"
                                         "with scratch:\n"
                                         "    label = \"a\" + 1\n"
                                         "clear scratch\n"
                                         "free scratch\n"
                                         "show xs\n",
                               "--report-leaks");
  REQUIRE(global == "[1, 2]\n");
}

TEST_CASE("The strict memory rules are on unless --lax-memory.", "[checked]") {
  const std::string loop = "count = 0\nwhile count < 3:\n"
                           "    label = \"item \" + count\n"
                           "    count = count + 1\nshow count\n";
  // run_ovis passes --lax-memory; ask the binary directly here.
  const auto folder = std::filesystem::temp_directory_path() / "ovis-strict-default";
  std::filesystem::remove_all(folder);
  std::filesystem::create_directories(folder);
  std::ofstream(folder / "p.ovis") << loop;
  const auto run = [&](const std::string &p_flags) {
    std::string output;
    auto *const pipe = popen((std::string(OVIS_EXECUTABLE) + " run " +
                              (folder / "p.ovis").string() +
                              " --offline --quiet --keep-source " + p_flags +
                              " 2>&1").c_str(), "r");
    REQUIRE(pipe != nullptr);
    char buffer[256];
    while (const auto count = std::fread(buffer, 1, sizeof(buffer), pipe))
      output.append(buffer, count);
    pclose(pipe);
    return output;
  };
  REQUIRE_THAT(run(""), ContainsSubstring("gets new text here"));
  REQUIRE(run("--lax-memory") == "3\n");
  std::filesystem::remove_all(folder);
}

TEST_CASE("A trait's phrase on two values checks they are the same record.",
          "[checked][traits]") {
  const auto output = run_ovis(
      "same", "a circle has:\n"
              "    radius (decimal)\n"
              "a square has:\n"
              "    side (decimal)\n"
              "a shape can:\n"
              "    (a) comes before (b) gives a truth value\n"
              "a circle is a shape:\n"
              "    (a) comes before (b):\n"
              "        give back radius of a < radius of b\n"
              "a square is a shape:\n"
              "    (a) comes before (b):\n"
              "        give back side of a < side of b\n"
              "mixed = [a circle with radius 1, a square with side 2]\n"
              "show item 1 of mixed < item 1 of mixed\n"
              "show item 1 of mixed < item 2 of mixed\n");
  REQUIRE_THAT(output, ContainsSubstring("false"));
  REQUIRE_THAT(output, ContainsSubstring("two different kinds of record"));
}

TEST_CASE("A trait phrase one record answers is called directly, and checked.",
          "[checked][traits]") {
  const auto output = run_ovis(
      "direct", "a circle has:\n"
                "    radius (decimal)\n"
                "a square has:\n"
                "    side (decimal)\n"
                "a named can:\n"
                "    label of (it) gives text\n"
                "a shape can:\n"
                "    area of (it) gives a decimal\n"
                "a circle is a named:\n"
                "    label of (it): give back \"circle\"\n"
                "a square is a named:\n"
                "    label of (it): give back \"square\"\n"
                "a circle is a shape:\n"
                "    area of (it): give back 3 * radius of it\n"
                "to count (xs: list of named):\n"
                "    show length of xs\n"
                "all = [a circle with radius 1, a square with side 2]\n"
                "count all\n"
                "for each x in all:\n"
                "    show area of x\n");
  REQUIRE_THAT(output, ContainsSubstring("2\n3.0\n"));
  REQUIRE_THAT(output, ContainsSubstring("does not implement the trait"));
}
