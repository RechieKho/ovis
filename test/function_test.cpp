#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <ovis/phrase/front_end.hpp>

#include "support.hpp"

using Catch::Matchers::ContainsSubstring;

namespace {

auto problems_of(const ovis::phrase::front_end_result &p_result)
    -> std::string {
  std::string text;
  for (const auto &problem : p_result.problems)
    text += problem + "\n";
  return text;
}

// Reads a program in phrases with no model, runs it and returns what it
// prints.
auto run_phrases(const std::string &p_source, const std::string &p_input = "")
    -> std::string {
  const auto result = ovis::phrase::compile_phrases(p_source, nullptr);
  INFO(problems_of(result));
  REQUIRE(result.succeeded());
  return run_program(result.program, p_input);
}

auto failure(const std::string &p_source) -> std::string {
  return problems_of(ovis::phrase::compile_phrases(p_source, nullptr));
}

} // namespace

TEST_CASE("Define phrases with 'to', in any order.", "[phrase][function]") {
  // Used before it is defined, as an expression and in a condition, and
  // recursively.
  REQUIRE(run_phrases("show factorial of 5\n"
                      "if 120 = factorial of 5:\n"
                      "    show \"right\"\n"
                      "to factorial of (n):\n"
                      "    if n < 2:\n"
                      "        give back 1\n"
                      "    give back n * factorial of (n - 1)\n") ==
          "120\nright\n");
  // A phrase that gives nothing back is a statement; types can be stated.
  REQUIRE(run_phrases("to greet (name: text) for (times: whole number) times:\n"
                      "    repeat times times:\n"
                      "        show \"Hello,\", name\n"
                      "greet \"Ann\" for 2 times\n") ==
          "Hello, Ann\nHello, Ann\n");
}

TEST_CASE("Call phrases and C functions by name.", "[phrase][function]") {
  // The name is the phrase's words (articles left out), or `as NAME`.
  REQUIRE(run_phrases("to pad (text) to (width):\n"
                      "    result = text\n"
                      "    while strlen(result) < width:\n"
                      "        result = result + \".\"\n"
                      "    give back result\n"
                      "to the larger of (a) and (b) as bigger:\n"
                      "    if a > b:\n"
                      "        give back a\n"
                      "    give back b\n"
                      "show pad_to(\"ab\", 4), bigger(3, 8)\n"
                      "printf(\"%d %.1f\\n\", abs(-3), pi)\n") ==
          "ab.. 8\n3 3.1\n");
  REQUIRE_THAT(failure("to twice (x):\n    give back x * 2\nshow twice(1, 2)\n"),
               ContainsSubstring("line 3:"));
}

TEST_CASE("Report problems in the program's own words.", "[phrase][function]") {
  // Types in English, and the line quoted.
  REQUIRE_THAT(failure("to halve (x):\n"
                       "    y = x / 2\n"
                       "    give back strlen(y)\n"
                       "show halve 4\n"),
               ContainsSubstring("line 3: Argument 1 of 'strlen' must be text "
                                 "but is a decimal number.\n"
                                 "    give back strlen(y)"));
  // A phrase by its header, not its Ovis Core name.
  REQUIRE_THAT(failure("to greet (who):\n"
                       "    show \"hi\", who\n"
                       "x = greet \"Ann\"\n"),
               ContainsSubstring("line 3: 'greet (who)' gives back nothing"));
  // A variable by its own name, though Ovis Core calls it `list_`.
  REQUIRE_THAT(failure("list = [1, 2]\nlist = \"a\"\n"),
               ContainsSubstring("The new value of 'list' must be a list of "
                                 "whole numbers but is text."));
}

TEST_CASE("A phrase's variables are its own.", "[phrase][function]") {
  // Assigning in a phrase makes a local, as in Python: the global keeps its
  // value. Variables first set in a block live on to the end of the phrase.
  REQUIRE(run_phrases("total = 100\n"
                      "to sum up to (n):\n"
                      "    total = 0\n"
                      "    for each i in [1, 2, 3]:\n"
                      "        if i <= n:\n"
                      "            last = i\n"
                      "            total = total + i\n"
                      "    show \"last\", last\n"
                      "    give back total\n"
                      "show sum up to 2, total\n") ==
          "last 2\n3 100\n");
  // A variable first set in a block, by a phrase whose result type isn't
  // known yet, gets it once the program is elaborated.
  REQUIRE(run_phrases("to label of (n):\n"
                      "    give back \"n\" + n\n"
                      "if true:\n"
                      "    name = label of 3\n"
                      "show name\n") == "n3\n");
  // Names Ovis Core uses itself are still the author's to use.
  REQUIRE(run_phrases("text = \"hi\"\nlist = [1, 2]\nprint = 3\n"
                      "show text, list, print\n") == "hi [1, 2] 3\n");
}

TEST_CASE("Problems in a phrase's body cite their own line.",
          "[phrase][function]") {
  // The checker's problem is on line 3, not on the phrase's line 1.
  REQUIRE_THAT(failure("to halve (x):\n"
                       "    y = x / 2\n"
                       "    give back strlen(y)\n"
                       "show halve 4\n"),
               ContainsSubstring("line 3:"));
}

TEST_CASE("Report phrases defined wrongly.", "[phrase][function]") {
  REQUIRE_THAT(failure("to (a) (b):\n    show a\n"),
               ContainsSubstring("line 1: A phrase needs at least one word"));
  REQUIRE_THAT(failure("to add (a) (b):\n    show a\n"),
               ContainsSubstring("line 1:"));
  REQUIRE_THAT(failure("to show 2 (a):\n    show a\n"),
               ContainsSubstring("words and (parameters)"));
  REQUIRE_THAT(failure("to scale (x: colour):\n    show x\n"),
               ContainsSubstring("'x' needs a type"));
  REQUIRE_THAT(failure("to go (a):\n    show a\nto go (b):\n    show b\n"),
               ContainsSubstring("line 3: A phrase called 'go' is already "
                                 "defined on line 1"));
  REQUIRE_THAT(failure("give back 1\n"),
               ContainsSubstring("line 1: 'give back' only works inside"));
  REQUIRE_THAT(failure("if true:\n    to go (a):\n        show a\n"),
               ContainsSubstring("line 2: Define phrases"));
}

TEST_CASE("Say why dividing a whole number cannot be done in place.",
          "[phrase][function]") {
  REQUIRE_THAT(failure("x = 7\ndivide x by 2\nshow x\n"),
               ContainsSubstring("would make x a decimal"));
  REQUIRE(run_phrases("x = 7\nx = (x divided by 2) rounded down\nshow x\n") ==
          "3\n");
}

TEST_CASE("Warn when a line drops a result and changes nothing.",
          "[phrase][function]") {
  const std::string phrase = "to add (n: whole number) and (k: whole number):\n"
                             "    give back n + k\n"
                             "score = 1\n";
  auto result = ovis::phrase::compile_phrases(
      phrase + "add score and 5\nshow score\n", nullptr);
  REQUIRE(result.succeeded());
  REQUIRE(result.warnings.size() == 1);
  REQUIRE_THAT(result.warnings.front(), ContainsSubstring("line 4:"));
  REQUIRE_THAT(result.warnings.front(),
               ContainsSubstring("gives back a value and changes nothing"));
  // Using the result is the way; a phrase that changes a list is fine.
  result = ovis::phrase::compile_phrases(
      phrase + "score = add score and 5\nshow score\n", nullptr);
  REQUIRE(result.warnings.empty());
  result = ovis::phrase::compile_phrases(
      "to put (n: whole number) in (xs: list of whole numbers):\n"
      "    add n to xs\n"
      "    give back length of xs\n"
      "numbers = [1]\nput 2 in numbers\nshow numbers\n",
      nullptr);
  REQUIRE(result.succeeded());
  REQUIRE(result.warnings.empty());
}
