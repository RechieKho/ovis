#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <ovis/core/checker.hpp>
#include <ovis/core/elaborate.hpp>
#include <ovis/phrase/front_end.hpp>

#include "support.hpp"

// `at the end` (defer.md): `(defer …)` in Ovis Core, and its phrases.

using Catch::Matchers::ContainsSubstring;

namespace {

auto check_core(const std::string &p_core, ovis::core::program &p_program)
    -> std::vector<std::string> {
  p_program.units.push_back(ovis::core::parse_unit(p_core));
  ovis::core::elaborate(p_program);
  std::vector<std::string> problems;
  for (const auto &problem : ovis::core::check(p_program))
    problems.push_back(problem.message);
  return problems;
}

auto run_core(const std::string &p_core) -> std::string {
  ovis::core::program program;
  const auto problems = check_core(p_core, program);
  for (const auto &problem : problems)
    UNSCOPED_INFO(problem);
  REQUIRE(problems.empty());
  return run_program(program);
}

auto core_problems(const std::string &p_core) -> std::string {
  ovis::core::program program;
  std::string text;
  for (const auto &problem : check_core(p_core, program))
    text += problem + "\n";
  return text;
}

auto run_phrases(const std::string &p_source) -> std::string {
  const auto result = ovis::phrase::compile_phrases(p_source, nullptr);
  std::string text;
  for (const auto &problem : result.problems)
    text += problem + "\n";
  INFO(text);
  REQUIRE(result.succeeded());
  return run_program(result.program);
}

auto phrase_problems(const std::string &p_source) -> std::string {
  const auto result = ovis::phrase::compile_phrases(p_source, nullptr);
  REQUIRE(!result.succeeded());
  std::string text;
  for (const auto &problem : result.problems)
    text += problem + "\n";
  return text;
}

} // namespace

TEST_CASE("Deferred statements run last first at the end of a block.",
          "[defer]") {
  CHECK(run_core("(print 1)\n"
                 "(defer (print 10))\n"
                 "(print 2)\n"
                 "(defer (print 11))\n"
                 "(print 3)") == "1\n2\n3\n11\n10\n");
  CHECK(run_core("(if true (then (defer (print 5)) (print 4)))\n"
                 "(print 6)") == "4\n5\n6\n");
}

TEST_CASE("A deferred statement sees the values at the end.", "[defer]") {
  CHECK(run_core("(let n 1)\n"
                 "(defer (print n))\n"
                 "(set n 7)") == "7\n");
}

TEST_CASE("A for loop can count down.", "[core]") {
  CHECK(run_core("(for-down i 3 1 (print i))") == "3\n2\n1\n");
  CHECK(run_core("(for-down i 1 3 (print i))").empty());
  CHECK(run_core("(for-down i 5 1 (if (= i 3) (then (continue))) (print i))") ==
        "5\n4\n2\n1\n");
}

TEST_CASE("A deferred statement runs on every pass of a loop.", "[defer]") {
  CHECK(run_core("(for i 1 3 (defer (print (* i 10))) (print i))") ==
        "1\n10\n2\n20\n3\n30\n");
  // stop and skip leave the pass through its deferred statements.
  CHECK(run_core("(for i 1 5\n"
                 "  (defer (print 0))\n"
                 "  (if (= i 2) (then (continue)))\n"
                 "  (if (= i 4) (then (break)))\n"
                 "  (print i))") == "1\n0\n0\n3\n0\n0\n");
  // A pass that stops before the defer line doesn't run it.
  CHECK(run_core("(for i 1 3\n"
                 "  (if (= i 2) (then (continue)))\n"
                 "  (defer (print 9))\n"
                 "  (print i))") == "1\n9\n3\n9\n");
}

TEST_CASE("Deferred statements run before a function returns.", "[defer]") {
  const std::string work =
      "(fn work ((n int)) int\n"
      "  (if (< n 0) (then (return 0)))\n"
      "  (defer (print 100))\n"
      "  (if (> n 10) (then (return 10)))\n"
      "  (return n))\n";
  CHECK(run_core(work + "(print (work -1))\n(print (work 20))\n(print (work 3))") ==
        "0\n100\n10\n100\n3\n");
  // The value is worked out first: the deferred statement can't change it.
  CHECK(run_core("(fn f () int\n"
                 "  (let x 1)\n"
                 "  (defer (set x 99))\n"
                 "  (return x))\n"
                 "(print (f))") == "1\n");
}

TEST_CASE("A trailing defer doesn't hide a function's value.", "[defer]") {
  CHECK(run_core("(fn f ((n int)) int\n"
                 "  (defer (print 0))\n"
                 "  (* n 2)\n"
                 "  (defer (print 1)))\n"
                 "(print (f 4))") == "1\n0\n8\n");
}

TEST_CASE("Defer nests, and a deferred block has its own deferred "
          "statements.",
          "[defer]") {
  CHECK(run_core("(defer (print 3) (defer (print 2)) (print 1))\n"
                 "(print 0)") == "0\n3\n1\n2\n");
  CHECK(run_core("(defer (for i 1 2 (print i)))") == "1\n2\n");
}

TEST_CASE("A deferred block can't leave early.", "[defer]") {
  CHECK_THAT(core_problems("(fn f () int (defer (return 1)) (return 2))"),
             ContainsSubstring("can't give back"));
  CHECK_THAT(core_problems("(for i 1 3 (defer (break)))"),
             ContainsSubstring("can't leave the loop"));
  CHECK_THAT(core_problems("(for i 1 3 (defer (continue)))"),
             ContainsSubstring("can't leave the loop"));
  // A loop inside the deferred block may use stop and skip.
  CHECK(core_problems("(defer (for i 1 3 (if (= i 2) (then (break)))))")
            .empty());
  CHECK(core_problems("(for i 1 3 (defer (for j 1 3 (continue))))").empty());
}

TEST_CASE("Read at the end in phrases.", "[defer][phrase]") {
  CHECK(run_phrases("show \"a\"\n"
                    "at the end, show \"z\"\n"
                    "show \"b\"\n") == "a\nb\nz\n");
  CHECK(run_phrases("defer show \"z\"\n"
                    "at the end:\n"
                    "    show \"y\"\n"
                    "    show \"x\"\n"
                    "show \"a\"\n") == "a\ny\nx\nz\n");
  CHECK(run_phrases("count = 0\n"
                    "at the end, show count\n"
                    "count = 5\n") == "5\n");
}

TEST_CASE("At the end inside phrases and loops.", "[defer][phrase]") {
  CHECK(run_phrases("to check (n):\n"
                    "    if n < 0:\n"
                    "        give back 0\n"
                    "    at the end, show \"checked\"\n"
                    "    give back n\n"
                    "show check(-1)\n"
                    "show check(4)\n") == "0\nchecked\n4\n");
  CHECK(run_phrases("to greet (name):\n"
                    "    at the end, show \"bye\", name\n"
                    "    show \"hello\", name\n"
                    "greet \"Ann\"\n") == "hello Ann\nbye Ann\n");
  CHECK(run_phrases("for each n in [1, 2]:\n"
                    "    at the end, show \"done\", n\n"
                    "    show n\n") == "1\ndone 1\n2\ndone 2\n");
}

TEST_CASE("Names first set in a deferred block stay in it.",
          "[defer][phrase]") {
  CHECK(run_phrases("at the end:\n"
                    "    total = 2 + 3\n"
                    "    show total\n"
                    "total = 1\n"
                    "show total\n") == "1\n5\n");
  // Also in a block inside it: declared at its top, not hoisted out.
  CHECK(run_phrases("at the end:\n"
                    "    if 1 < 2:\n"
                    "        count = 7\n"
                    "        show count\n"
                    "count = \"a text\"\n"
                    "show count\n") == "a text\n7\n");
}

TEST_CASE("Report mistakes in at the end.", "[defer][phrase]") {
  CHECK_THAT(phrase_problems("at the end,\nshow 1\n"),
             ContainsSubstring("Say what to do at the end"));
  CHECK_THAT(phrase_problems("at the end, show 1:\n    show 2\n"),
             ContainsSubstring("Write 'at the end:'"));
  // Names must be set above.
  CHECK_THAT(phrase_problems("at the end, show later\nlater = 3\n"),
             ContainsSubstring("move 'at the end' below"));
  CHECK_THAT(phrase_problems("to f (n):\n"
                             "    at the end:\n"
                             "        give back 1\n"
                             "    give back n\n"
                             "show f(1)\n"),
             ContainsSubstring("can't give back a value"));
}

TEST_CASE("Note where at the end runs when that isn't the whole program.",
          "[defer][phrase]") {
  const std::string source = "for each n in [1, 2]:\n"
                             "    at the end, show \"done\"\n"
                             "    show n\n"
                             "at the end, show \"bye\"\n"
                             "if true:\n"
                             "    at the end, show \"if\"\n";
  const auto first = ovis::phrase::compile_phrases(source, nullptr);
  REQUIRE(first.succeeded());
  CHECK_THAT(first.source,
             ContainsSubstring("    @[note: runs at the end of every pass "
                               "of the loop.]\n"
                               "    at the end, show \"done\"\n"));
  CHECK_THAT(first.source, ContainsSubstring("    @[note: runs at the end of "
                                             "this branch.]\n"));
  // Top-level lines get no note, and a rebuild changes nothing.
  CHECK_THAT(first.source,
             ContainsSubstring("at the end, show \"bye\"\nif true:"));
  const auto second = ovis::phrase::compile_phrases(first.source, nullptr);
  REQUIRE(second.succeeded());
  CHECK(second.source == first.source);
}
