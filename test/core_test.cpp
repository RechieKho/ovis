#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>

#include <catch2/catch_test_macros.hpp>
#include <llvm/IR/LLVMContext.h>

#include <ovis/codegen/llvm_codegen.hpp>
#include <ovis/core/checker.hpp>
#include <ovis/core/elaborate.hpp>
#include <ovis/core/modules.hpp>
#include <ovis/core/c_import.hpp>
#include <ovis/english/language_model.hpp>

#include "support.hpp"

using namespace std::literals::string_literals;
using namespace test_support;

TEST_CASE("Read S-expressions.", "[sexpr]") {
  using kind = ovis::core::sexpr::kind_type;
  const auto forms = ovis::core::read_sexprs(
      "(print \"a \\\"b\\\"\" 12 -3.5) ; comment\n(x (y))");
  REQUIRE(forms.size() == 2);
  REQUIRE(forms[0].head() == "print");
  REQUIRE(forms[0].items[1].kind == kind::string);
  REQUIRE(forms[0].items[1].text == "a \"b\"");
  REQUIRE(forms[0].items[2].kind == kind::integer);
  REQUIRE(forms[0].items[2].integer == 12);
  REQUIRE(forms[0].items[3].kind == kind::real);
  REQUIRE(forms[0].items[3].real == -3.5);
  REQUIRE(forms[1].items[1].items[0].is_symbol("y"));
  REQUIRE(forms[1].location.line == 2);

  // Reals are read with strtod, which accepts more spellings than a number.
  const auto reals =
      ovis::core::read_sexprs("(.5 1e3 2.5E-1 inf nan +1.5 0x1p3 1e 1e999)");
  REQUIRE(reals[0].items[0].real == 0.5);
  REQUIRE(reals[0].items[1].real == 1000.0);
  REQUIRE(reals[0].items[2].real == 0.25);
  for (std::size_t index = 3; index < reals[0].items.size(); ++index)
    REQUIRE(reals[0].items[index].kind == kind::symbol);

  REQUIRE_THROWS_AS(ovis::core::read_sexprs("(a (b)"),
                    ovis::core::syntax_error);
  REQUIRE_THROWS_AS(ovis::core::read_sexprs("a)"), ovis::core::syntax_error);
}

TEST_CASE("Parse lenient Ovis Core.", "[parser]") {
  using kind = ovis::core::statement::kind_type;
  const auto unit = ovis::core::parse_unit(
      "(fn twice ((x float)) float (return (* x 2)))\n"
      "(let names str[] (list \"a\"))\n"
      "(let count int)\n"
      "(if (== count 0) (print \"zero\") (print \"other\"))\n"
      "(set (get names 0) \"b\")");
  REQUIRE(unit.functions.size() == 1);
  REQUIRE(unit.functions[0].return_type == ovis::core::value_type::real());
  REQUIRE(unit.statements.size() == 4);
  REQUIRE(*unit.statements[0].declared_type ==
          ovis::core::value_type::list_of(ovis::core::base_type::string));
  REQUIRE(unit.statements[1].values.empty());
  REQUIRE(unit.statements[2].values[0].text == "=");
  REQUIRE(unit.statements[2].body.size() == 1);
  REQUIRE(unit.statements[2].else_body.size() == 1);
  REQUIRE(unit.statements[3].kind == kind::put);

  // A copied declaration is skipped; statements nested in a let follow it.
  const auto copied = ovis::core::parse_unit(
      "(fn square ((n int)) int)\n"
      "(let i 1 (while (< i 6) (print i) (set i (+ i 1))))");
  REQUIRE(copied.functions.empty());
  REQUIRE(copied.statements.size() == 2);
  REQUIRE(copied.statements[1].kind == kind::while_);

  REQUIRE_THROWS_AS(ovis::core::parse_unit("(let)"), ovis::error);
  REQUIRE_THROWS_AS(ovis::core::parse_unit("(if (f (fn g () void)))"),
                    ovis::error);
}

TEST_CASE("Check types and names.", "[checker]") {
  REQUIRE(check_text("(let x int 1) (set x (+ x 2.5))").size() == 1);
  REQUIRE(check_text("(let x 1.0) (set x (+ x 2))").empty());

  const auto unknown = check_text("(let total 0) (print amount)");
  REQUIRE(unknown.size() == 1);
  REQUIRE(unknown[0].message.find("Unknown variable 'amount'") !=
          std::string::npos);
  REQUIRE(unknown[0].message.find("total (a whole number)") != std::string::npos);

  REQUIRE(check_text("(print (* \"a\" 1))")[0].message.find(
              "needs numbers") != std::string::npos);
  REQUIRE(check_text("(print (strlen 5))").size() == 1);
  REQUIRE(check_text("(printf \"%d %d\" 1)")[0].message.find(
              "needs 2 value(s)") != std::string::npos);
  REQUIRE(check_text("(printf \"%s\" 1)").size() == 1);
  REQUIRE(check_text("(fn f () int (return true))").size() == 1);
  REQUIRE(check_text("(break)").size() == 1);
  REQUIRE(check_text("(print (missing 1))").size() == 1);
  REQUIRE(check_text("(fn f ((a int)) int (return a)) (print (f 1 2))")
              .size() == 1);

  // Pseudo-code habits that are accepted.
  REQUIRE(check_text("(set fresh 3) (let fresh 4) (print fresh)").empty());
  REQUIRE(check_text("(fn uses_global () int (return limit)) (let limit 5)")
              .empty());
}

TEST_CASE("Generate working native code.", "[codegen]") {
  REQUIRE(run_core("(print 7 (/ 7 2) (div 7 2) (% -7 3) (- 4))") ==
          "7 3.5 3 -1 -4\n");
  REQUIRE(run_core("(print (and true false) (or false true) (not false) "
                   "(= \"a\" \"a\") (!= 1 1.0) (< 1 2.5))") ==
          "false true true true false true\n");
  REQUIRE(run_core("(print (sqrt 16) (pow 2 10) (floor 2.7) (abs -3) "
                   "(min 4 2 8) (max 1.5 3) (int 3.9) (float 2))") ==
          "4.0 1024.0 2.0 3 2 3.0 3 2.0\n");
  // `and` must not evaluate its second operand when the first is false.
  REQUIRE(run_core("(let xs (list 1))\n"
                   "(if (and (< 5 (len xs)) (= (get xs 5) 0)) "
                   "(then (print \"bad\")) (else (print \"safe\")))") ==
          "safe\n");
  REQUIRE(run_core("(let flags (array bool 2)) (put flags 1 true)\n"
                   "(let words (array str 1)) (append words \"x\")\n"
                   "(print flags words (len words))") ==
          "[false, true] [\"\", \"x\"] 2\n");
  REQUIRE(run_core("(let i 0)\n"
                   "(while true (set i (+ i 1)) (if (> i 3) (then (break))))\n"
                   "(for j 1 5 (if (= (% j 2) 0) (then (continue))) "
                   "(print j))\n(print i)") == "1\n3\n5\n4\n");
  REQUIRE(run_core("(fn fact ((n int)) int\n"
                   "  (if (<= n 1) (then (return 1)))\n"
                   "  (return (* n (fact (- n 1)))))\n"
                   "(fn greet ((name str)) void (print \"hi\" name))\n"
                   "(greet \"bob\") (print (fact 10))") ==
          "hi bob\n3628800\n");
  REQUIRE(run_core("(let a (read_int)) (let b (read_float)) (print (+ a b))",
                   "2 0.5") == "2.5\n");
}

TEST_CASE("Infer what Ovis Core leaves out.", "[elaborate]") {
  ovis::core::program program;
  const auto notes = elaborate_text(
      "(fn average (xs)\n"
      "  (let total 0)\n"
      "  (each x xs (set total (+ total x)))\n"
      "  (/ total (len xs)))\n"
      "(let readings (list))\n"
      "(append readings 2.5)\n"
      "(print (average readings))",
      program);
  const auto &average = program.units[0].functions[0];
  REQUIRE(average.parameters[0].type ==
          ovis::core::value_type::list_of(ovis::core::base_type::real));
  REQUIRE(average.return_type == ovis::core::value_type::real());
  // `total` starts as 0 but has floats added to it.
  REQUIRE(*average.body[0].declared_type == ovis::core::value_type::real());
  REQUIRE(mentions(notes, "gives back the value it works out last"));
  REQUIRE(mentions(notes, "`total` holds a decimal number"));
  REQUIRE(ovis::core::check(program).empty());
}

TEST_CASE("Fill in details deterministically.", "[elaborate]") {
  ovis::core::program program;
  const auto notes = elaborate_text(
      "(fn is_even (n) (= (% n 2) 0))\n"
      "(let counter 3)\n"
      "(print countr (isEven counter))\n"
      "(while counter (set counter (- counter 1)))\n"
      "(let seconds (time))\n"
      "(let things (list))",
      program);
  REQUIRE(mentions(notes, "Read `countr` as `counter`"));
  REQUIRE(mentions(notes, "Read `isEven` as `is_even`"));
  REQUIRE(mentions(notes, "is not zero"));
  REQUIRE(mentions(notes, "gets null"));
  REQUIRE(mentions(notes, "Nothing says what this list holds"));
  REQUIRE(mentions(notes, "`is_even` takes `n` as a whole number"));
  REQUIRE(ovis::core::check(program).empty());

  // The same input always gives the same result.
  ovis::core::program again;
  REQUIRE(elaborate_text(
              "(fn is_even (n) (= (% n 2) 0))\n(let counter 3)\n"
              "(print countr (isEven counter))\n"
              "(while counter (set counter (- counter 1)))\n"
              "(let seconds (time))\n(let things (list))",
              again) == notes);

  // Ambiguous names are left for the model to fix.
  REQUIRE(ovis::core::closest_name("cat", {"bat", "hat"}) == std::nullopt);
  REQUIRE(ovis::core::closest_name("totl", {"total", "count"}) == "total");
}

TEST_CASE("Work with text.", "[codegen]") {
  REQUIRE(run_core("(let name \"Ada\")\n"
                   "(print (+ \"Hi \" name \", you are \" 36 \"!\"))\n"
                   "(print (substring \"hello\" 1 3) (char_at \"A\" 0) "
                   "(chr 66) (to_str 2.5) (concat \"a\" \"b\"))") ==
          "Hi Ada, you are 36!\nell 65 B 2.5 ab\n");
  REQUIRE(run_core("(let count 0)\n"
                   "(let line (read_line))\n"
                   "(while (!= line null)\n"
                   "  (set count (+ count 1))\n"
                   "  (set line (read_line)))\n"
                   "(print count \"lines\")",
                   "one\\ntwo\\nthree\\n") == "3 lines\n");
  REQUIRE(run_core("(let xs (list 3 1 2)) (sort xs) (print xs) (reverse xs) "
                   "(print xs)\n(let words (list \"b\" \"a\")) (sort words) "
                   "(print words)") ==
          "[1, 2, 3]\n[3, 2, 1]\n[\"a\", \"b\"]\n");
}

TEST_CASE("Call the C standard library.", "[libc]") {
  REQUIRE(run_core("(printf \"%d|%5.2f|%s|%c|%x\\n\" 42 pi \"ok\" 65 255)") ==
          "42| 3.14|ok|A|ff\n");
  // Values are converted to what the format shows.
  REQUIRE(run_core("(printf \"%d %f\\n\" 2.9 3)") == "2 3.000000\n");
  REQUIRE(run_core("(print (round (sin 0)) (strlen \"four\") "
                   "(toupper (char_at \"q\" 0)) (atoi \"12\") "
                   "(strcmp \"a\" \"b\") (fmax 1 2))") ==
          "0.0 4 81 12 -1 2.0\n");

  const auto path = (std::filesystem::temp_directory_path() / "ovis-test.txt")
                        .string();
  REQUIRE(run_core("(let file (fopen \"" + path + "\" \"w\"))\n"
                   "(fprintf file \"%s %d\\n\" \"line\" 1)\n"
                   "(fclose file)\n"
                   "(let input (fopen \"" + path + "\" \"r\"))\n"
                   "(let text (buffer 100))\n"
                   "(if (!= (fgets text 100 input) null) "
                   "(then (printf \"read: %s\" text)))\n"
                   "(fclose input)") == "read: line 1\n");
  std::filesystem::remove(path);

  REQUIRE(run_core("(extern atoll (str) int)\n(extern llabs (int) int)\n"
                   "(link \"m\")\n(print (atoll \"9000000000\") (llabs -3))") ==
          "9000000000 3\n");
}

