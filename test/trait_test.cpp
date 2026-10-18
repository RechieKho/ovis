#include <catch2/catch_test_macros.hpp>

#include <ovis/core/checker.hpp>
#include <ovis/core/elaborate.hpp>

#include "support.hpp"

// User traits and impls in Ovis Core: `trait`, `impl`, and calls on a value
// of a trait, which are dispatched when the program runs.

namespace {

struct elaborated {
  ovis::core::program program;
  std::vector<std::string> problems;
};

auto elaborate_and_check(const std::string &p_core) -> elaborated {
  elaborated result;
  result.program.units.push_back(ovis::core::parse_unit(p_core));
  std::vector<ovis::core::diagnostic> unclear;
  ovis::core::elaborate(result.program, &unclear);
  for (const auto &problem : unclear)
    result.problems.push_back(problem.message);
  for (const auto &problem : ovis::core::check(result.program))
    result.problems.push_back(problem.message);
  return result;
}

auto run_traits(const std::string &p_core) -> std::string {
  auto checked = elaborate_and_check(p_core);
  for (const auto &problem : checked.problems)
    UNSCOPED_INFO(problem);
  REQUIRE(checked.problems.empty());
  return run_program(checked.program);
}

auto problems_of(const std::string &p_core) -> std::string {
  std::string all;
  for (const auto &problem : elaborate_and_check(p_core).problems)
    all += problem + "\n";
  return all;
}

const std::string shapes =
    "(record circle (radius float))\n"
    "(record square (side float))\n"
    "(trait shape\n"
    "  (area (it) float)\n"
    "  (describe (it))\n"
    "  (fn describe_twice (it) (describe it) (describe it)))\n"
    "(impl shape circle\n"
    "  (fn area (it) (* 3 (field it radius) (field it radius)))\n"
    "  (fn describe (it) (print \"circle\" (field it radius))))\n"
    "(impl shape square\n"
    "  (fn area (it) (* (field it side) (field it side)))\n"
    "  (fn describe (it) (print \"square\" (field it side)))\n"
    "  (fn describe_twice (it) (print \"a square, once\")))\n";

} // namespace

TEST_CASE("Parse traits and impls.", "[traits]") {
  const auto unit = ovis::core::parse_unit(shapes);
  REQUIRE(unit.traits.size() == 1);
  const auto &trait = unit.traits[0];
  CHECK(trait.name == "shape");
  REQUIRE(trait.methods.size() == 3);
  CHECK(!trait.methods[0].has_default);
  CHECK(trait.methods[0].return_type == ovis::core::value_type::real());
  CHECK(trait.methods[1].return_type == ovis::core::value_type::nothing());
  CHECK(trait.methods[2].has_default);
  CHECK(trait.methods[0].parameters[0].type ==
        ovis::core::value_type::record_of("shape"));
  REQUIRE(unit.impls.size() == 2);
  CHECK(unit.impls[1].methods ==
        std::vector<std::string>{"area", "describe", "describe_twice"});
  // The methods are functions, one per record.
  bool found = false;
  for (const auto &function : unit.functions)
    if (function.name == "area__circle") {
      found = true;
      CHECK(function.parameters[0].type ==
            ovis::core::value_type::record_of("circle"));
    }
  CHECK(found);

  CHECK_THROWS(ovis::core::parse_unit("(trait int (area (it)))"));
  CHECK_THROWS(ovis::core::parse_unit("(trait shape (area ()))"));
  CHECK_THROWS(ovis::core::parse_unit("(trait shape (area (it x)))"));
  CHECK_THROWS(ovis::core::parse_unit("(impl shape)"));
  CHECK_THROWS(ovis::core::parse_unit("(impl shape circle (area (it)))"));
  CHECK_THROWS(ovis::core::parse_unit("(fn f () (trait shape (area (it))))"));
}

TEST_CASE("Call a trait's phrases on a record.", "[traits][codegen]") {
  // On a record: that record's impl, or the trait's default.
  CHECK(run_traits(shapes +
                   "(let c (new circle (radius 2)))\n"
                   "(let q (new square (side 3)))\n"
                   "(print (area c) (area q))\n"
                   "(describe_twice c)\n"
                   "(describe_twice q)") ==
        "12.0 9.0\ncircle 2.0\ncircle 2.0\na square, once\n");
}

TEST_CASE("Dispatch on a value known only as a trait.",
          "[traits][codegen]") {
  // A parameter and a variable typed by the trait take either record.
  CHECK(run_traits(shapes +
                   "(fn double_area ((s (record shape))) float (* 2 (area s)))\n"
                   "(let c (new circle (radius 1)))\n"
                   "(let held (record shape) c)\n"
                   "(print (double_area c) (double_area (new square (side 2))))\n"
                   "(print (area held))\n"
                   "(describe_twice held)") ==
        "6.0 8.0\n3.0\ncircle 1.0\ncircle 1.0\n");

  // A mixed list: its items are all shapes, whatever they are.
  CHECK(run_traits(shapes +
                   "(fn total ((all (list (record shape)))) float\n"
                   "  (let sum 0.0)\n"
                   "  (each s all (set sum (+ sum (area s))))\n"
                   "  sum)\n"
                   "(let all (list (new circle (radius 1)) "
                   "(new square (side 2))))\n"
                   "(print (total all))\n"
                   "(each s all (describe_twice s))") ==
        "7.0\ncircle 1.0\ncircle 1.0\na square, once\n");

  // Appending to a list of the trait, and printing what is in it.
  CHECK(run_traits(shapes +
                   "(let all (list (record shape)))\n"
                   "(append all (new square (side 1)))\n"
                   "(append all (new circle (radius 1)))\n"
                   "(print (len all) (area (get all 0)))\n"
                   "(print all)") ==
        "2 1.0\n[(a square with side 1.0), (a circle with radius 1.0)]\n");
}

TEST_CASE("A trait's methods are inferred from the phrases used.",
          "[traits][codegen]") {
  // `s` is never typed: only the shape's phrases are used on it.
  CHECK(run_traits(shapes +
                   "(fn bigger (a b) (> (area a) (area b)))\n"
                   "(print (bigger (new circle (radius 2)) "
                   "(new square (side 3))))") == "true\n");

  // Phrases of two traits leave it open: the author must say.
  const auto two = shapes +
                   "(trait drawable (draw (it)))\n"
                   "(impl drawable circle (fn draw (it) (print \"o\")))\n"
                   "(fn both (x) (area x) (draw x))\n";
  CHECK(problems_of(two).find("shape") != std::string::npos);
  CHECK(problems_of(two).find("drawable") != std::string::npos);
  CHECK(run_traits(shapes +
                   "(trait drawable (draw (it)))\n"
                   "(impl drawable circle (fn draw (it) (print \"o\")))\n"
                   "(fn both ((x (record circle))) (print (area x)) (draw x))\n"
                   "(both (new circle (radius 1)))") == "3.0\no\n");
}

TEST_CASE("Problems with traits are explained.", "[traits]") {
  const auto has = [](const std::string &p_core, const std::string &p_text) {
    return problems_of(p_core).find(p_text) != std::string::npos;
  };
  // An impl that lacks a phrase lists it.
  CHECK(has("(record a (x int))\n(trait t (one (it) int) (two (it) int))\n"
            "(impl t a (fn one (it) 1))",
            "still needs: 'two'"));
  // A phrase the trait doesn't have.
  CHECK(has("(record a (x int))\n(trait t (one (it) int))\n"
            "(impl t a (fn one (it) 1) (fn three (it) 3))",
            "'t' has no phrase 'three'"));
  // The types must be the trait's.
  CHECK(has("(record a (x int))\n(trait t (one (it) int))\n"
            "(impl t a (fn one (it) \"no\"))",
            "must be a whole number but is text"));
  CHECK(has("(record a (x int))\n(trait t (one (it) int))\n"
            "(impl t a (fn one (it extra) 1))",
            "takes 1 argument"));
  // One impl per trait and record; both must exist.
  CHECK(has("(record a (x int))\n(trait t (one (it) int))\n"
            "(impl t a (fn one (it) 1))\n(impl t a (fn one (it) 2))",
            "implements 't' twice"));
  CHECK(has("(record a (x int))\n(impl nothing a)", "no trait called"));
  CHECK(has("(trait t (one (it) int))\n(impl t ghost (fn one (it) 1))",
            "no record called 'ghost'"));
  // A record that doesn't implement the trait can't be used as one.
  CHECK(has("(record a (x int))\n(record b (y int))\n"
            "(trait t (one (it) int))\n(impl t a (fn one (it) 1))\n"
            "(print (one (new b (y 1))))",
            "doesn't implement it"));
  CHECK(has("(record a (x int))\n(trait t (one (it) int))\n"
            "(impl t a (fn one (it) 1))\n(fn f ((v (record t))) int 1)\n"
            "(f (new a (x 1)))\n(f 3)",
            "must be a t"));
  // A trait is not a record.
  CHECK(has("(record a (x int))\n(trait t (one (it) int))\n"
            "(impl t a (fn one (it) 1))\n(print (new t (x 1)))",
            "is a trait, not a record"));
  CHECK(has("(record a (x int))\n(trait t (one (it) int))\n"
            "(impl t a (fn one (it) 1))\n"
            "(fn f ((v (record t))) int (field v x))",
            "is a trait, not a record"));
  CHECK(has("(record t (x int))\n(trait t (one (it) int))", "is a record"));
  CHECK(has("(trait t (one (it) int))\n(trait u (one (it) int))",
            "phrase of both"));
}
