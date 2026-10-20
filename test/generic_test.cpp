#include <catch2/catch_test_macros.hpp>

#include <ovis/core/checker.hpp>
#include <ovis/core/elaborate.hpp>

#include "support.hpp"

// Generics in Ovis Core (docs/proposals/generics.md): `forall`, `(var T)`,
// a copy per use with concrete types, bounds checked once.

namespace {

auto problems_of(const std::string &p_core) -> std::string {
  ovis::core::program program;
  program.units.push_back(ovis::core::parse_unit(p_core));
  std::vector<ovis::core::diagnostic> unclear;
  ovis::core::elaborate(program, &unclear);
  std::string all;
  for (const auto &problem : unclear)
    all += problem.message + "\n";
  for (const auto &problem : ovis::core::check(program))
    all += problem.message + "\n";
  return all;
}

const std::string book =
    "(record book (title str) (pages int))\n"
    "(fn comes_before__book ((a (record book)) (b (record book))) bool\n"
    "  (< (field a pages) (field b pages)))\n"
    "(fn is_same_as__book ((a (record book)) (b (record book))) bool\n"
    "  (= (field a title) (field b title)))\n";

const std::string largest =
    "(fn largest (forall (item comparable)) ((xs (list (var item))))\n"
    "  (let best (get xs 0))\n"
    "  (each x xs (if (< best x) (then (set best x))))\n"
    "  best)\n";

} // namespace

TEST_CASE("A generic function is copied for each type it is used with",
          "[generic]") {
  CHECK(test_support::run_core(
            "(fn identity (forall item) ((x (var item))) x)\n"
            "(print (identity 5))\n"
            "(print (identity \"five\"))\n"
            "(print (+ (identity 1.5) 1))\n") == "5\nfive\n2.5\n");
}

TEST_CASE("A generic function can swap and give back lists of its type",
          "[generic]") {
  CHECK(test_support::run_core(
            "(fn swap (forall item) ((xs (list (var item))))\n"
            "  (let first (get xs 0))\n"
            "  (put xs 0 (get xs 1))\n"
            "  (put xs 1 first))\n"
            "(fn twice (forall item) ((x (var item))) (list x x))\n"
            "(let a (list 1 2))\n"
            "(swap a)\n"
            "(print a)\n"
            "(let b (list \"x\" \"y\"))\n"
            "(swap b)\n"
            "(print b)\n"
            "(print (twice true))\n") == "[2, 1]\n[\"y\", \"x\"]\n[true, true]\n");
}

TEST_CASE("A bound lets a generic compare, over numbers, text and records",
          "[generic]") {
  CHECK(test_support::run_core(
            book + largest +
            "(print (largest (list 3 9 4)))\n"
            "(print (largest (list \"pear\" \"apple\")))\n"
            "(print (largest (list 1.5 0.5)))\n"
            "(print (field (largest (list (new book (title \"A\") (pages 3))\n"
            "                             (new book (title \"B\") (pages 7)))) title))\n") ==
        "9\npear\n1.5\nB\n");
}

TEST_CASE("Generics call generics, and copies are made once", "[generic]") {
  CHECK(test_support::run_core(
            largest +
            "(fn both (forall (item comparable)) ((a (list (var item))) (b (list (var item))))\n"
            "  (let x (largest a))\n"
            "  (let y (largest b))\n"
            "  (if (< x y) (then (return y)))\n"
            "  (return x))\n"
            "(print (both (list 1 5) (list 2 3)))\n"
            "(print (both (list \"b\") (list \"c\")))\n"
            "(print (largest (list 8 2)))\n") == "5\nc\n8\n");
}

TEST_CASE("An unmet bound is an error at the call, naming what to write",
          "[generic]") {
  const auto problems = problems_of(
      "(record book (title str))\n" + largest +
      "(print (largest (list (new book (title \"A\")))))\n");
  CHECK(problems.find("'largest' needs comparable values, but a book isn't "
                      "one. Say how two compare: `to (a: book) comes before "
                      "(b: book):`.") != std::string::npos);
  // Not inside the generic's body.
  CHECK(problems.find("needs numbers") == std::string::npos);
}

TEST_CASE("A generic body may only do what its bounds allow", "[generic]") {
  CHECK(problems_of("(fn bigger (forall item) ((a (var item)) (b (var item)))\n"
                    "  (< a b))\n") ==
        "'<' needs an item to be comparable: write `any comparable item` "
        "where it is first named.\n");
  CHECK(problems_of("(fn total (forall item) ((a (var item)) (b (var item)))\n"
                    "  (+ a b))\n")
            .find("needs an item to be number") != std::string::npos);
  CHECK(problems_of("(fn total (forall (item number)) ((a (var item)) (b (var item)))\n"
                    "  (+ a b))\n"
                    "(print (total 2 3))\n") == "");
  CHECK(problems_of("(fn show_it (forall item) ((a (var item))) (print a))\n")
            .find("to be showable") != std::string::npos);
}

TEST_CASE("A type variable nothing decides is an error at the call",
          "[generic]") {
  CHECK(problems_of("(fn nothing_yet (forall item) () (array (var item) 0))\n"
                    "(nothing_yet)\n")
            .find("Ovis can't tell what `item` is in this use of "
                  "'nothing_yet'") != std::string::npos);
  CHECK(test_support::run_core(
            "(fn empty (forall item) () (array (var item) 0))\n"
            "(let xs (list int) (empty))\n"
            "(append xs 4)\n"
            "(print xs)\n") == "[4]\n");
}

TEST_CASE("Bounds name traits there are, and a user trait's phrases",
          "[generic]") {
  CHECK(problems_of("(fn f (forall (item sortable)) ((a (var item))) a)\n")
            .find("There is no trait called 'sortable'") != std::string::npos);
  CHECK(test_support::run_core(
            "(record circle (radius float))\n"
            "(trait shape (area (it) float))\n"
            "(impl shape circle (fn area (it) (* 3 (field it radius) (field it radius))))\n"
            "(fn total_area (forall (s shape)) ((xs (list (var s))))\n"
            "  (let total 0.0)\n"
            "  (each x xs (set total (+ total (area x))))\n"
            "  total)\n"
            "(print (total_area (list (new circle (radius 1.0)) (new circle (radius 2.0)))))\n") ==
        "15.0\n");
}

namespace {

const std::string stack =
    "(record stack (forall item) (items (list (var item))) (size int))\n"
    "(fn push_on (forall item) ((x (var item)) (s (record stack (var item))))\n"
    "  (append (field s items) x)\n"
    "  (set-field s size (+ (field s size) 1)))\n"
    "(fn top (forall item) ((s (record stack (var item))))\n"
    "  (get (field s items) (- (field s size) 1)))\n";

} // namespace

TEST_CASE("A generic record is a record of its own for each use",
          "[generic]") {
  CHECK(test_support::run_core(
            "(record pair (forall first second) (left (var first)) (right (var second)))\n"
            "(let p (new pair (left 1) (right \"one\")))\n"
            "(let q (new (pair str float) (left \"pi\") (right 3.14)))\n"
            "(print (field p right) (field q left))\n"
            "(print p)\n") == "one pi\na pair of whole numbers and text with left 1, right \"one\"\n");
  CHECK(test_support::run_core(
            stack +
            "(let s (new stack (items (array int 0)) (size 0)))\n"
            "(push_on 4 s)\n"
            "(push_on 7 s)\n"
            "(let t (new stack (items (array str 0)) (size 0)))\n"
            "(push_on \"hi\" t)\n"
            "(print (top s) (top t) (field s size))\n") == "7 hi 2\n");
}

TEST_CASE("A generic record may hold itself and other uses of itself",
          "[generic]") {
  CHECK(test_support::run_core(
            "(record node (forall item) (value (var item))\n"
            "  (next (maybe (record node (var item)))))\n"
            "(fn total ((n (maybe (record node int)))) int\n"
            "  (let sum 0)\n"
            "  (let at n)\n"
            "  (while (!= at null)\n"
            "    (set sum (+ sum (field at value)))\n"
            "    (set at (field at next)))\n"
            "  sum)\n"
            "(let last (new node (value 2) (next (cast (maybe (record node int)) null))))\n"
            "(let first (new node (value 1) (next last)))\n"
            "(print (total first))\n") == "3\n");
  CHECK(test_support::run_core(
            stack +
            "(let inner (new stack (items (list \"a\")) (size 1)))\n"
            "(let outer (new (stack (record stack str)) (items (array (record stack str) 0)) (size 0)))\n"
            "(push_on inner outer)\n"
            "(print (top (top outer)))\n") == "a\n");
}

TEST_CASE("A generic record's types are said or decided", "[generic]") {
  CHECK(problems_of(stack + "(let s (new stack (items (list)) (size 0)))\n")
            .find("Say what a stack holds: `a new stack of …`") !=
        std::string::npos);
}

TEST_CASE("An impl for every stack, with what its items need", "[generic]") {
  const std::string shown =
      stack +
      "(trait describable (describe (it) str))\n"
      "(impl describable stack (forall (item showable))\n"
      "  (fn describe (it) (concat \"a stack of \" (to_str (field it size)))))\n"
      "(record crate (label str))\n"
      "(impl describable crate (fn describe (it) (concat \"crate \" (field it label))))\n";
  CHECK(test_support::run_core(
            shown +
            "(let s (new stack (items (list 1 2)) (size 2)))\n"
            "(let t (new stack (items (list \"x\")) (size 1)))\n"
            "(print (describe s))\n"
            "(let all (list (record describable)))\n"
            "(append all s)\n"
            "(append all t)\n"
            "(append all (new crate (label \"tea\")))\n"
            "(each d all (print (describe d)))\n") ==
        "a stack of 2\na stack of 2\na stack of 1\ncrate tea\n");
}

TEST_CASE("Numbers and text implement traits statically", "[generic]") {
  const std::string hashable =
      "(trait hashable (hash (it) int)\n"
      "  (fn hash_twice (it) (* 2 (hash it))))\n"
      "(impl hashable int (fn hash (it) (% it 7)))\n"
      "(impl hashable str (fn hash (it) (strlen it)))\n"
      "(fn bucket (forall (key hashable)) ((k (var key)) (n int)) (% (hash k) n))\n";
  CHECK(test_support::run_core(hashable +
                               "(print (hash 30) (hash \"abc\") (hash_twice 30))\n"
                               "(print (bucket 10 4) (bucket \"hello\" 3))\n") ==
        "2 3 4\n3 2\n");
  // Not a value of the trait: that would need the number boxed.
  CHECK(problems_of(hashable + "(let xs (list (record hashable)))\n"
                               "(append xs 5)\n")
            .find("The appended value must be a hashable but is a whole number") !=
        std::string::npos);
  CHECK(problems_of(hashable + "(print (bucket 1.5 4))\n")
            .find("'bucket' needs hashable values, but a decimal number isn't one") !=
        std::string::npos);
}
