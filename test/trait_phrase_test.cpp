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
  // Numbers and text can't have a trait: a record that holds one can.
  CHECK_THAT(problems(base + "a whole number is a shape:\n"
                             "    area of (it):\n"
                             "        give back 1\n"),
             ContainsSubstring("Only records can be a shape, not whole number. "
                               "Make a record that holds the value"));
  CHECK_THAT(problems("a text is showable:\n"
                      "    text of (it):\n"
                      "        give back it\n"),
             ContainsSubstring("Only records can be showable, not text."));
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
  // The value itself is `it`, or a parameter without a type.
  CHECK_THAT(problems("a shape can:\n"
                      "    area of (x: whole number) gives a decimal\n"),
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

TEST_CASE("Records of two traits in one list can be used as either.",
          "[traits][phrases]") {
  const std::string two = "a circle has:\n"
                          "    radius (decimal)\n"
                          "a square has:\n"
                          "    side (decimal)\n"
                          "a shape can:\n"
                          "    area of (it) gives a decimal\n"
                          "a named can:\n"
                          "    label of (it) gives text\n"
                          "a circle is a shape:\n"
                          "    area of (it): give back 3 * radius of it\n"
                          "a square is a shape:\n"
                          "    area of (it): give back side of it\n"
                          "a circle is a named:\n"
                          "    label of (it): give back \"circle\"\n"
                          "a square is a named:\n"
                          "    label of (it): give back \"square\"\n"
                          "to count the shapes in (xs: list of shape):\n"
                          "    give back length of xs\n"
                          "to count the names in (xs: list of named):\n"
                          "    give back length of xs\n"
                          "all = [a circle with radius 1, a square with side 2]\n";
  CHECK(run_phrases(two + "for each x in all:\n"
                          "    show area of x\n"
                          "    show label of x\n"
                          "show count the shapes in all\n"
                          "show count the names in all\n") ==
        "3.0\ncircle\n2.0\nsquare\n2\n2\n");
}

TEST_CASE("A trait's phrase may take the value without (it), and be a "
          "built-in trait's.",
          "[traits][phrases]") {
  const std::string base =
      "a circle has:\n"
      "    radius (decimal)\n"
      "a square has:\n"
      "    side (decimal)\n"
      "a shape can:\n"
      "    area of (it) gives a decimal\n"
      "    (a) is bigger than (b) gives a truth value:\n"
      "        give back area of a > area of b\n"
      "    (a) comes before (b) gives a truth value\n"
      "a circle is a shape:\n"
      "    area of (it):\n"
      "        give back 3 * radius of it * radius of it\n"
      "    (a) comes before (b):\n"
      "        give back radius of a < radius of b\n"
      "a square is a shape:\n"
      "    area of (it):\n"
      "        give back side of it * side of it\n"
      "    (a) comes before (b):\n"
      "        give back side of a < side of b\n";
  // On records: their impl directly. `<` and `sort` use `comes before`.
  CHECK(run_phrases(base + "c1 = a circle with radius 2\n"
                           "c2 = a circle with radius 1\n"
                           "show c1 is bigger than c2\n"
                           "show c1 comes before c2\n"
                           "show c2 < c1\n"
                           "q1 = a square with side 3\n"
                           "q2 = a square with side 1\n"
                           "squares = [q1, q2]\n"
                           "sort squares\n"
                           "show squares\n") ==
        "true\nfalse\ntrue\n[(a square with side 1.0), (a square with side 3.0)]\n");
  // On values of the trait: dispatched, and a list of the trait sorts.
  CHECK(run_phrases(base + "c1 = a circle with radius 2\n"
                           "c2 = a circle with radius 1\n"
                           "q = a square with side 1\n"
                           "mixed = [c1, q, c2]\n"
                           "show item 1 of mixed is bigger than item 3 of mixed\n"
                           "show item 3 of mixed < item 1 of mixed\n"
                           "show item 1 of mixed comes before item 3 of mixed\n") ==
        "true\ntrue\nfalse\n");
  // Two different records known when it is built.
  CHECK_THAT(problems(base + "show a circle with radius 1 is bigger than "
                             "a square with side 1\n"),
             ContainsSubstring("needs two values of the same kind"));
  // The impl must write the values as the trait does.
  CHECK_THAT(problems(base + "a dot has:\n"
                             "    size (decimal)\n"
                             "a dot is a shape:\n"
                             "    area of (it):\n"
                             "        give back 0.0\n"
                             "    (a) comes before (b: dot):\n"
                             "        give back false\n"),
             ContainsSubstring("as the trait does"));
  // `comes before` must give a truth value.
  CHECK_THAT(problems("a thing can:\n"
                      "    (a) comes before (b)\n"),
             ContainsSubstring("end it with 'gives a truth value'"));
  // A phrase with nothing for the value itself.
  CHECK_THAT(problems("a thing can:\n"
                      "    shout (n: whole number)\n"),
             ContainsSubstring("needs the value itself"));
}
