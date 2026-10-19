#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <ovis/phrase/front_end.hpp>
#include "support.hpp"

// Records that may be nothing (proposals/references.md §3, step R1b): only
// `(maybe (record T))`, written `T or nothing`, holds `null`, and its
// fields are read once a check shows it isn't nothing.

using Catch::Matchers::ContainsSubstring;
using test_support::run_core;

namespace {

auto problems(const std::string &p_core) -> std::string {
  std::string text;
  for (const auto &problem : test_support::check_text(p_core))
    text += problem.message + "\n";
  return text;
}

auto phrase_problems(const std::string &p_source) -> std::string {
  const auto result = ovis::phrase::compile_phrases(p_source, nullptr);
  std::string text;
  for (const auto &problem : result.problems)
    text += problem + "\n";
  return text;
}

auto run_phrases(const std::string &p_source) -> std::string {
  const auto result = ovis::phrase::compile_phrases(p_source, nullptr);
  INFO(phrase_problems(p_source));
  REQUIRE(result.succeeded());
  return run_program(result.program);
}

const std::string node = "(record node (value int) (next (maybe (record node))))\n";

} // namespace

TEST_CASE("Only a record that may be nothing holds nothing.", "[nothing]") {
  CHECK_THAT(problems("(record box (size int))\n"
                      "(let b (new box (size 1)))\n"
                      "(set b null)"),
             ContainsSubstring("or nothing"));
  CHECK_THAT(problems("(record item (next (record item)))\n"
                      "(let i (new item (next null)))"),
             ContainsSubstring("or nothing"));
  CHECK(problems(node + "(let n (new node (value 1) (next null)))").empty());
}

TEST_CASE("A field of a record that may be nothing needs a check first.",
          "[nothing]") {
  CHECK_THAT(problems(node + "(let n (new node (value 1) (next null)))\n"
                             "(print (field (field n next) value))"),
             ContainsSubstring("may be nothing"));
  CHECK_THAT(problems(node + "(let n (new node (value 1) (next null)))\n"
                             "(let m (field n next))\n"
                             "(print (field m value))"),
             ContainsSubstring("if m is not nothing"));
  // Checked by `if`, by `while`, and by leaving early.
  CHECK(run_core(node +
                 "(let n (new node (value 1) (next null)))\n"
                 "(let m (field n next))\n"
                 "(if (!= m null) (then (print (field m value))) "
                 "(else (print \"none\")))\n"
                 "(if (= m null) (then (print \"still none\")) "
                 "(else (print (field m value))))\n") ==
        "none\nstill none\n");
  CHECK(run_core(node +
                 "(fn total ((first (maybe (record node)))) int\n"
                 "  (let sum 0)\n"
                 "  (let at first)\n"
                 "  (while (!= at null)\n"
                 "    (set sum (+ sum (field at value)))\n"
                 "    (set at (field at next)))\n"
                 "  (return sum))\n"
                 "(fn first_value ((n (maybe (record node)))) int\n"
                 "  (if (= n null) (then (return 0)))\n"
                 "  (return (field n value)))\n"
                 "(let c (new node (value 3) (next null)))\n"
                 "(let b (new node (value 2) (next c)))\n"
                 "(print (total b) (first_value null) (first_value c))\n") ==
        "5 0 3\n");
  // Setting it again undoes the check.
  CHECK_THAT(problems(node + "(let n (new node (value 1) (next null)))\n"
                             "(let m (field n next))\n"
                             "(if (!= m null)\n"
                             "  (then (set m (field m next)) (print (field m value))))"),
             ContainsSubstring("may be nothing"));
}

TEST_CASE("Records that may be nothing in English.", "[nothing]") {
  CHECK(run_phrases("a node has:\n"
                    "    value (whole number)\n"
                    "    next (node or nothing)\n"
                    "last = a node with value 2, next nothing\n"
                    "first = a node with value 1, next last\n"
                    "current = first\n"
                    "while current is not nothing:\n"
                    "    show current's value\n"
                    "    current = next of current\n") == "1\n2\n");
  CHECK_THAT(phrase_problems("a node has:\n"
                             "    value (whole number)\n"
                             "    next (node)\n"
                             "only = a node with value 2, next nothing\n"),
             ContainsSubstring("or nothing"));
  CHECK_THAT(phrase_problems("a node has:\n"
                             "    value (whole number)\n"
                             "    next (node or nothing)\n"
                             "only = a node with value 2, next nothing\n"
                             "show next of next of only\n"),
             ContainsSubstring("may be nothing"));
}

TEST_CASE("A variable that may be nothing starts as nothing.", "[nothing]") {
  CHECK(run_phrases("a book has:\n"
                    "    title (text)\n"
                    "    pages (whole number)\n"
                    "best = a book or nothing\n"
                    "show best is nothing\n"
                    "for each pages in [100, 300, 200]:\n"
                    "    if best is nothing or pages > pages of best:\n"
                    "        best = a book with title \"T\", pages pages\n"
                    "if best is not nothing:\n"
                    "    show pages of best\n") == "true\n300\n");
}
