#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <ovis/phrase/front_end.hpp>

#include "support.hpp"

// Bare blocks, as Zig's: `(block [:LABEL] BODY...)` is a scope of its own
// whose `defer`s run when it is left; `(let x (block …))` gives it a value
// through `(leave [:LABEL] VALUE)`. In English, `do:` and `leave with`.

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

} // namespace

TEST_CASE("A bare block is a scope whose defers run when it ends.",
          "[block]") {
  CHECK(run_core("(let x 1)\n"
                 "(block\n"
                 "  (let x 2)\n"
                 "  (defer (print \"inner ends\"))\n"
                 "  (print x))\n"
                 "(print x)\n"
                 "(do (let y 3) (print y))\n") ==
        "2\ninner ends\n1\n3\n");
  // A name declared in it is gone after it.
  CHECK_THAT(problems("(block (let y 3))\n(print y)"),
             ContainsSubstring("y"));
}

TEST_CASE("A block gives a value by leaving with it.", "[block]") {
  CHECK(run_core("(let x 5)\n"
                 "(let size (block :sizing\n"
                 "  (defer (print \"sizing ends\"))\n"
                 "  (if (> x 0) (then (leave :sizing \"big\")))\n"
                 "  (leave :sizing \"small\")))\n"
                 "(print size)\n"
                 "(fn classify (n) str\n"
                 "  (let word (do\n"
                 "    (each i (list 1 2 3)\n"
                 "      (if (= i n) (then (leave (concat \"found \" (to_str i))))))\n"
                 "    (leave \"none\")))\n"
                 "  (return word))\n"
                 "(print (classify 2) (classify 9))\n"
                 "(set x (block (leave 7)))\n"
                 "(print x)\n") ==
        "sizing ends\nbig\nfound 2 none\n7\n");
  // Leaving an outer block from an inner one runs both blocks' defers.
  CHECK(run_core("(let r (block :outer\n"
                 "  (defer (print \"outer\"))\n"
                 "  (block\n"
                 "    (defer (print \"inner\"))\n"
                 "    (leave :outer 1))\n"
                 "  (leave :outer 2)))\n"
                 "(print r)\n") == "inner\nouter\n1\n");
}

TEST_CASE("A block's leaves are checked.", "[block]") {
  CHECK_THAT(problems("(let v (block (print 1)))"),
             ContainsSubstring("must end by leaving with one"));
  CHECK_THAT(problems("(let v (block (leave)))"),
             ContainsSubstring("leave it with one"));
  CHECK_THAT(problems("(block (leave 3))"),
             ContainsSubstring("gives no value"));
  CHECK_THAT(problems("(leave)"), ContainsSubstring("only works inside a block"));
  CHECK_THAT(problems("(block (leave :nowhere))"),
             ContainsSubstring("no block labelled ':nowhere'"));
  CHECK_THAT(problems("(let v (block (leave 1) ))\n(set v (block (leave \"a\")))"),
             ContainsSubstring("The block's value"));
  CHECK_THAT(problems("(block (defer (leave)) (print 1))"),
             ContainsSubstring("can't leave the block around it"));
}

TEST_CASE("`do:` blocks in English.", "[block]") {
  CHECK(run_phrases("scores = [72, 95, 88]\n"
                    "do:\n"
                    "    total = 0\n"
                    "    at the end, show \"done adding\"\n"
                    "    for each score in scores:\n"
                    "        total = total + score\n"
                    "    show total\n"
                    "grade = do:\n"
                    "    for each score in scores:\n"
                    "        if score > 90:\n"
                    "            leave with \"an A\"\n"
                    "    leave with \"no A\"\n"
                    "show grade\n"
                    "to first even in (numbers):\n"
                    "    found = do:\n"
                    "        for each n in numbers:\n"
                    "            if n % 2 = 0:\n"
                    "                leave with n\n"
                    "        leave with 0\n"
                    "    give back found\n"
                    "show first even in [3, 8, 5]\n"
                    "count = 1\n"
                    "do:\n"
                    "    if count > 0:\n"
                    "        leave\n"
                    "    show \"not shown\"\n"
                    "show \"after\"\n") ==
        "255\ndone adding\nan A\n8\nafter\n");
  // Names set inside are the block's own.
  CHECK_THAT(phrase_problems("do:\n    inner = 1\nshow inner\n"),
             ContainsSubstring("inner"));
  CHECK_THAT(phrase_problems("leave\n"),
             ContainsSubstring("only works inside a 'do:' block"));
  CHECK_THAT(phrase_problems("do:\n    leave with 3\n"),
             ContainsSubstring("gives no value"));
  CHECK_THAT(phrase_problems("x = do:\n    show 1\n"),
             ContainsSubstring("must end by leaving with one"));
}
