#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <ovis/phrase/front_end.hpp>

// The strict memory rules (proposals/memory.md §2 and §4), checked when
// `strict_memory` is set.

using Catch::Matchers::ContainsSubstring;

namespace {

// The problems a program has under the strict rules, one string.
auto strict_problems(const std::string &p_source) -> std::string {
  const auto result =
      ovis::phrase::compile_phrases(p_source, nullptr, {.strict_memory = true});
  std::string text;
  for (const auto &problem : result.problems)
    text += problem + "\n";
  return text;
}

} // namespace

TEST_CASE("A value lost on every pass of a loop is an error.", "[memory][strict]") {
  const auto lost = strict_problems("count = 0\n"
                                    "while count < 5:\n"
                                    "    label = \"item \" + count\n"
                                    "    count = count + 1\n"
                                    "show count\n");
  REQUIRE_THAT(lost, ContainsSubstring("line 3:"));
  REQUIRE_THAT(lost, ContainsSubstring("`label` gets new text here, on every "
                                       "pass of the loop,"));
  REQUIRE_THAT(lost, ContainsSubstring("`free label`"));
  // Both fixes of the proposal build.
  REQUIRE(strict_problems("count = 0\n"
                          "while count < 5:\n"
                          "    with new memory:\n"
                          "        label = \"item \" + count\n"
                          "    count = count + 1\n"
                          "show count\n").empty());
  REQUIRE(strict_problems("label = \"\"\n"
                          "count = 0\n"
                          "while count < 5:\n"
                          "    free label\n"
                          "    label = \"item \" + count\n"
                          "    count = count + 1\n"
                          "show count\n").empty());
  // Without the strict rules the program builds as it always did.
  const auto lenient = ovis::phrase::compile_phrases(
      "count = 0\nwhile count < 5:\n    label = \"item \" + count\n"
      "    count = count + 1\nshow count\n",
      nullptr);
  REQUIRE(lenient.succeeded());
}

TEST_CASE("A new value needs a place to be stored.", "[memory][strict]") {
  const auto shown = strict_problems("numbers = [3, 1, 2]\n"
                                     "show \"count \" + length of numbers\n");
  REQUIRE_THAT(shown, ContainsSubstring("makes new text inside `show`"));
  REQUIRE_THAT(shown, ContainsSubstring("Store it in a variable first"));
  // One join is one value, however many parts.
  REQUIRE(strict_problems("name = \"Ann\"\nage = 7\n"
                          "line = name + \" is \" + age + \" years\"\n"
                          "show line\nfree line\n").empty());
  // A new value as an argument, or in a condition.
  REQUIRE_THAT(strict_problems("name = \"Ann\"\n"
                               "if length of (name + \"!\") > 3:\n"
                               "    show \"long\"\n"),
               ContainsSubstring("makes new text"));
  // Storing it is the way.
  REQUIRE(strict_problems("names = empty list of text\n"
                          "add \"a\" + \"b\" to names\n"
                          "show names\n").empty());
}

TEST_CASE("A value of a new memory can't outlive it.", "[memory][strict]") {
  const auto escape = strict_problems("names = empty list of text\n"
                                      "with new memory:\n"
                                      "    line = \"ab\" + \"cd\"\n"
                                      "    add line to names\n"
                                      "show line\n");
  REQUIRE_THAT(escape, ContainsSubstring("line 4:"));
  REQUIRE_THAT(escape, ContainsSubstring("new memory of line 2"));
  REQUIRE_THAT(escape, ContainsSubstring("can't be stored in `names`"));
  REQUIRE_THAT(escape, ContainsSubstring("line 5:"));
  REQUIRE_THAT(escape, ContainsSubstring("`line` was made in the new memory"));
  // `keep` makes a copy outside.
  REQUIRE(strict_problems("names = empty list of text\n"
                          "with new memory:\n"
                          "    line = \"ab\" + \"cd\"\n"
                          "    add keep line to names\n"
                          "show names\n").empty());
}

TEST_CASE("A phrase gives back new values or existing ones, not both.",
          "[memory][strict]") {
  const auto mixed = strict_problems("to pick (a: text) or (b: text):\n"
                                     "    if length of a > 2:\n"
                                     "        give back a + b\n"
                                     "    give back b\n"
                                     "x = pick \"abc\" or \"d\"\n"
                                     "show x\n");
  REQUIRE_THAT(mixed, ContainsSubstring("gives back new text in one place and "
                                        "existing text in another"));
  REQUIRE(strict_problems("to join (a: text) with (b: text):\n"
                          "    joined = a + b\n"
                          "    give back joined\n"
                          "x = join \"a\" with \"b\"\n"
                          "show x\nfree x\n").empty());
  // A phrase that gives back new text is a new value where it is called.
  REQUIRE_THAT(strict_problems("to join (a: text) with (b: text):\n"
                               "    joined = a + b\n"
                               "    give back joined\n"
                               "show join \"a\" with \"b\"\n"),
               ContainsSubstring("makes new text inside `show`"));
}
