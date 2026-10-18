#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <ovis/phrase/front_end.hpp>

#include "support.hpp"

// User traits written in phrases (types.md §2): `a NAME can:`,
// `a NAME is a TRAIT:`, `it`, mixed lists and trait-typed parameters.

using Catch::Matchers::ContainsSubstring;

namespace {

auto problems_of(const ovis::phrase::front_end_result &p_result)
    -> std::string {
  std::string text;
  for (const auto &problem : p_result.problems)
    text += problem + "\n";
  return text;
}

auto run_phrases(const std::string &p_source) -> std::string {
  const auto result = ovis::phrase::compile_phrases(p_source, nullptr);
  INFO(problems_of(result));
  REQUIRE(result.succeeded());
  REQUIRE(result.timing.questions == 0);
  return run_program(result.program);
}

auto problems(const std::string &p_source) -> std::string {
  const auto result = ovis::phrase::compile_phrases(p_source, nullptr);
  REQUIRE(!result.succeeded());
  return problems_of(result);
}

const std::string shapes = "a circle has:\n"
                           "    radius (decimal)\n"
                           "\n"
                           "a square has:\n"
                           "    side (decimal)\n"
                           "\n"
                           "a shape can:\n"
                           "    area of (it) gives a decimal\n"
                           "    describe (it)\n"
                           "    describe (it) twice:\n"
                           "        describe it\n"
                           "        describe it\n"
                           "\n"
                           "a circle is a shape:\n"
                           "    area of (it):\n"
                           "        give back 3 * radius of it * radius of it\n"
                           "    describe (it):\n"
                           "        show \"circle\", radius of it\n"
                           "\n"
                           "a square is a shape:\n"
                           "    area of (it):\n"
                           "        give back side of it * side of it\n"
                           "    describe (it):\n"
                           "        show \"square\", side of it\n"
                           "    describe (it) twice:\n"
                           "        show \"a square, once\"\n"
                           "\n";

} // namespace

TEST_CASE("A trait's phrases work on each record that implements it.",
          "[traits][phrases]") {
  CHECK(run_phrases(shapes + "c = a circle with radius 2\n"
                             "q = a square with side 3\n"
                             "show area of c, area of q\n"
                             "describe c twice\n"
                             "describe q twice\n"
                             "describe q\n") ==
        "12.0 9.0\ncircle 2.0\ncircle 2.0\na square, once\nsquare 3.0\n");
}

TEST_CASE("Values of a trait: parameters, variables and mixed lists.",
          "[traits][phrases]") {
  CHECK(run_phrases(shapes +
                    "to show total area of (all: list of shape):\n"
                    "    total = 0.0\n"
                    "    for each s in all:\n"
                    "        total = total + area of s\n"
                    "    show total\n"
                    "\n"
                    "all = [a circle with radius 1, a square with side 2]\n"
                    "show total area of all\n"
                    "for each s in all:\n"
                    "    describe s twice\n") ==
        "7.0\ncircle 1.0\ncircle 1.0\na square, once\n");

  // Annotated or inferred from the phrases used on it.
  CHECK(run_phrases(shapes +
                    "to bigger (a: shape) than (b: shape):\n"
                    "    give back area of a > area of b\n"
                    "\n"
                    "show bigger (a circle with radius 2) than "
                    "(a square with side 3)\n"
                    "to twice the area of (s: shape):\n"
                    "    give back 2 * area of s\n"
                    "show twice the area of a square with side 2\n"
                    // No type written: only a shape's phrase is used on it.
                    "to thrice the area of (s):\n"
                    "    give back 3 * area of s\n"
                    "show thrice the area of a square with side 2\n") ==
        "true\n8.0\n12.0\n");
}

TEST_CASE("A trait may be declared after its impls and in another file.",
          "[traits][phrases]") {
  const auto result = ovis::phrase::compile_phrase_program(
      {{.name = "shapes.ovis", .module = "shapes",
        .text = "a circle is a shape:\n"
                "    area of (it):\n"
                "        give back 3 * radius of it * radius of it\n"
                "\n"
                "a shape can:\n"
                "    area of (it) gives a decimal\n"
                "\n"
                "a circle has:\n"
                "    radius (decimal)\n"},
       {.name = "main.ovis", .module = "main",
        .text = "show area of a circle with radius 2\n"}},
      nullptr);
  INFO(problems_of({.problems = result.problems}));
  REQUIRE(result.succeeded());
  CHECK(run_program(result.program) == "12.0\n");
}

TEST_CASE("Problems with traits are put in the program's words.",
          "[traits][phrases]") {
  const std::string base = "a circle has:\n"
                           "    radius (decimal)\n"
                           "a shape can:\n"
                           "    area of (it) gives a decimal\n"
                           "    describe (it)\n";
  // A phrase left out.
  CHECK_THAT(problems(base + "a circle is a shape:\n"
                             "    area of (it):\n"
                             "        give back 1\n"),
             ContainsSubstring("still needs: 'describe (it)'"));
  // A phrase the trait doesn't have.
  CHECK_THAT(problems(base + "a circle is a shape:\n"
                             "    area of (it):\n"
                             "        give back 1\n"
                             "    describe (it):\n"
                             "        show 1\n"
                             "    paint (it):\n"
                             "        show 2\n"),
             ContainsSubstring("has no phrase like this one"));
  // Unknown trait and record.
  CHECK_THAT(problems(base + "a circle is a polygon:\n"
                             "    area of (it):\n"
                             "        give back 1\n"),
             ContainsSubstring("No trait is called 'polygon'"));
  CHECK_THAT(problems(base + "a hexagon is a shape:\n"
                             "    area of (it):\n"
                             "        give back 1\n"),
             ContainsSubstring("No record is called 'hexagon'"));
  // A record that doesn't implement the trait.
  CHECK_THAT(problems("a rock has:\n"
                      "    weight (decimal)\n" +
                      base + "show area of a rock with weight 1\n"),
             ContainsSubstring("doesn't implement it"));
  // Phrases of two traits on one value: say the type.
  CHECK_THAT(problems(base + "a circle is a shape:\n"
                             "    area of (it):\n"
                             "        give back 1\n"
                             "    describe (it):\n"
                             "        show 1\n"
                             "a thing can:\n"
                             "    paint (it)\n"
                             "to both (x):\n"
                             "    describe x\n"
                             "    paint x\n"),
             ContainsSubstring("write its type"));
  // The value itself is `it`.
  CHECK_THAT(problems("a shape can:\n"
                      "    area of (x) gives a decimal\n"),
             ContainsSubstring("parameter called 'it'"));
}

TEST_CASE("A phrase's body may follow its header on the same line.",
          "[traits][phrases]") {
  CHECK(run_phrases("a circle has:\n"
                    "    radius (decimal)\n"
                    "a shape can:\n"
                    "    area of (it) gives a decimal\n"
                    "    describe (it)\n"
                    "a circle is a shape:\n"
                    "    area of (it): give back 3 * radius of it * radius of "
                    "it\n"
                    "    describe (it): show \"circle\", radius of it\n"
                    "to double (n: whole number): give back n * 2\n"
                    "c = a circle with radius 2\n"
                    "show area of c, double 4\n"
                    "describe c\n") == "12.0 8\ncircle 2.0\n");
}

TEST_CASE("A list of one record can be passed as a list of a trait.",
          "[traits][phrases]") {
  CHECK(run_phrases(shapes +
                    "to total area of (all: list of shape):\n"
                    "    total = 0.0\n"
                    "    for each s in all:\n"
                    "        total = total + area of s\n"
                    "    give back total\n"
                    "circles = [a circle with radius 1, a circle with radius 2]\n"
                    "show total area of circles\n") == "15.0\n");
}
