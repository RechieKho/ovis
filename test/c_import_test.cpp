#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
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

TEST_CASE("Import a C library from its header.", "[c-import]") {
  if (!ovis::core::has_c_import_support())
    SKIP("Built without libclang.");
  const auto &library = ovis::core::import_c_library("ovistest.h", "ovistest",
                                                     test_c_imports());
  const auto find = [&](const std::string &p_name) {
    return std::ranges::find(library.functions, p_name,
                             &ovis::core::c_function::name);
  };
  using enum ovis::core::c_type;
  REQUIRE(find("ovt_add") != library.functions.end());
  REQUIRE(find("ovt_wrap")->result == uint8_type);
  REQUIRE(find("ovt_wrap")->parameters[0] == uint16_type);
  REQUIRE(find("ovt_answer")->pointees[0] == int_type);
  REQUIRE(find("ovt_counter_open")->pointees[1] == pointer_type);
  REQUIRE(find("ovt_greet")->result == string_type);
  REQUIRE(find("ovt_add")->comment == "Adds two numbers.");
  // A struct by value goes through a C wrapper.
  REQUIRE(find("ovt_point_sum")->by_value[0]);
  REQUIRE(find("ovt_point_make")->result_by_value);
  const auto constant = [&](const std::string &p_name) {
    return std::ranges::find(library.constants, p_name,
                             &ovis::core::c_constant::name);
  };
  REQUIRE(constant("OVT_ERROR")->integer == -1);
  REQUIRE(constant("OVT_SCALE")->real == 2.5);
  REQUIRE(constant("OVT_BLUE")->integer == 6);
  REQUIRE(library.link_flags.back() == "-lovistest");
  REQUIRE(!library.header_hash.empty());

  REQUIRE(run_core(
              "(use-c \"ovistest.h\" \"ovistest\")\n"
              "(print (ovt_add 2 3) (ovt_scale 2.0 1.5) (ovt_wrap 300) "
              "(ovt_negate 5) (ovt_big))\n"
              "(let answer 0)\n(ovt_answer (ref answer))\n"
              "(let half 0.0)\n(ovt_half 5 (ref half))\n"
              "(print answer half (ovt_greet \"Ada\"))\n"
              "(let counter null)\n"
              "(print (ovt_counter_open \"four\" (ref counter)) "
              "(ovt_counter_get counter))\n"
              "(ovt_counter_add counter 3)\n"
              "(print (ovt_counter_get counter) "
              "(ovt_counter_open \"bad\" (ref counter)) OVT_ERROR OVT_VERSION "
              "OVT_SCALE OVT_GREEN OVT_BLUE)\n"
              "(print (ovt_visit null 7) (ovt_next_color OVT_RED))") ==
          "5 3.0 44 -5 4000000000\n42 2.5 hello, Ada\n0 4\n"
          "7 -1 -1 3 2.5 5 6\n-7 5\n");

  // (ref x) is a pointer, which can't be printed.
  REQUIRE(check_text("(let x 0) (print (ref x))")[0].message.find(
              "A pointer cannot be printed") != std::string::npos);
}

TEST_CASE("Use real C libraries.", "[c-import]") {
  const auto has_header = [](const char *p_name) {
    for (const auto &directory : ovis::core::system_include_directories())
      if (std::filesystem::exists(directory / p_name))
        return true;
    return false;
  };
  if (!ovis::core::has_c_import_support() || !has_header("zlib.h") ||
      !has_header("sqlite3.h"))
    SKIP("Needs libclang and the zlib and SQLite development files.");

  // zlib: compress and uncompress text, sizes passed by (ref ...).
  REQUIRE(run_core(
              "(use-c \"zlib.h\" \"z\")\n"
              "(let text \"hello hello hello hello hello hello\")\n"
              "(let packed_size (compressBound (strlen text)))\n"
              "(let packed (buffer packed_size))\n"
              "(print (compress packed (ref packed_size) text (strlen text)) "
              "Z_OK)\n"
              "(let unpacked_size 100)\n"
              "(let unpacked (buffer 101))\n"
              "(uncompress unpacked (ref unpacked_size) packed packed_size)\n"
              "(print (< packed_size (strlen text)) unpacked_size unpacked)") ==
          "0 0\ntrue 35 hello hello hello hello hello hello\n");

  // SQLite: open an in-memory database, insert and query.
  REQUIRE(run_core(
              "(use-c \"sqlite3.h\" \"sqlite3\")\n"
              "(let db null)\n"
              "(print (= (sqlite3_open \":memory:\" (ref db)) SQLITE_OK))\n"
              "(sqlite3_exec db \"create table t (n int); insert into t "
              "values (20), (22);\" null null null)\n"
              "(let statement null)\n"
              "(sqlite3_prepare_v2 db \"select sum(n) from t\" -1 "
              "(ref statement) null)\n"
              "(if (= (sqlite3_step statement) SQLITE_ROW) "
              "(then (print \"sum\" (sqlite3_column_int statement 0))))\n"
              "(sqlite3_finalize statement)\n(sqlite3_close db)") ==
          "true\nsum 42\n");
}


TEST_CASE("C structs are C records: fields, nested structs, char arrays.",
          "[c-import][pointers]") {
  if (!ovis::core::has_c_import_support())
    SKIP("Built without libclang.");
  const auto &library = ovis::core::import_c_library("ovistest.h", "ovistest",
                                                     test_c_imports());
  const auto record = std::ranges::find(library.records, "ovt_box",
                                        &ovis::core::record::name);
  REQUIRE(record != library.records.end());
  REQUIRE(record->size == 48);
  REQUIRE(record->find("label")->text_length == 8);
  REQUIRE(record->find("label")->offset == 12);
  REQUIRE(record->find("corner")->inline_record);
  REQUIRE(record->find("next")->type ==
          ovis::core::value_type::record_of("ovt_box"));
  REQUIRE(std::ranges::find(library.records, "ovt_point",
                            &ovis::core::record::name) != library.records.end());

  CHECK(run_core("(use-c \"ovistest.h\" \"ovistest\")\n"
                 "(let p (ovt_point_new 3 4))\n"
                 "(print (field p x) (field p y) (ovt_point_area p))\n"
                 "(set-field p x 10)\n"
                 "(print (ovt_point_area p))\n"
                 "(free p)\n"
                 "(let b (new ovt_box (width 2) (label \"a long label\")))\n"
                 "(let second (new ovt_box (width 5) (scale 0.5)))\n"
                 "(set-field b next second)\n"
                 "(ovt_box_grow b 3)\n"
                 "(print (field b width) (field b label) (field second width)"
                 " (field (field b corner) x) (field (field b next) scale))\n"
                 "(set-field (field b corner) y 9)\n"
                 "(print (field b corner))\n"
                 "(print (size-of (struct ovt_box)) (load i32 (ref (field b width))))\n") ==
        "3 4 12\n40\n5 a long  8 -3 0.5\nan ovt point with x -3, y 9\n48 5\n");
}

TEST_CASE("C arrays: made, viewed, passed to C and checked.",
          "[c-import][pointers]") {
  if (!ovis::core::has_c_import_support())
    SKIP("Built without libclang.");
  CHECK(run_core("(use-c \"ovistest.h\" \"ovistest\")\n"
                 "(let values (list 1.5 2.5 3.0))\n"
                 "(print (ovt_sum values (len values)))\n"
                 "(let bytes (array u8 4))\n"
                 "(ovt_fill bytes (len bytes))\n"
                 "(print bytes (get bytes 3))\n"
                 "(put bytes 0 300)\n"
                 "(append bytes 7)\n"
                 "(print bytes (len bytes))\n"
                 "(let squares (view i32 (ovt_squares 5) 5))\n"
                 "(let total 0)\n"
                 "(each square squares (set total (+ total square)))\n"
                 "(print squares total)\n"
                 "(free (cast ptr squares))\n"
                 "(free squares)\n") ==
        "7.0\n[0, 100, 200, 44] 44\n[44, 100, 200, 44, 7] 5\n[0, 1, 4, 9, 16] 30\n");
  // Items of the wrong C type, and an item past the end.
  ovis::core::program program;
  program.units.push_back(ovis::core::parse_unit(
      "(use-c \"ovistest.h\" \"ovistest\")\n"
      "(let values (list 1 2))\n"
      "(print (ovt_sum values 2))\n"));
  ovis::core::resolve_c_imports(program, test_c_imports());
  ovis::core::elaborate(program);
  const auto problems = ovis::core::check(program);
  REQUIRE(!problems.empty());
  REQUIRE(problems[0].message.find("takes an array of float") != std::string::npos);
}

TEST_CASE("Structs by value pass through C wrappers.", "[c-import][pointers]") {
  if (!ovis::core::has_c_import_support())
    SKIP("Built without libclang.");
  CHECK(run_core("(use-c \"ovistest.h\" \"ovistest\")\n"
                 "(let p (ovt_point_make 3 4))\n"
                 "(print p (ovt_point_sum p))\n"
                 "(set-field p y 10)\n"
                 "(print (ovt_point_sum p))\n"
                 "(free p)\n") ==
        "an ovt point with x 3, y 4 7\n13\n");
}

TEST_CASE("Phrases as values: called, and given to C as callbacks.",
          "[c-import][pointers]") {
  if (!ovis::core::has_c_import_support())
    SKIP("Built without libclang.");
  CHECK(run_core("(use-c \"ovistest.h\" \"ovistest\")\n"
                 "(use-c \"stdlib.h\")\n"
                 "(fn triple ((n int)) int (return (* n 3)))\n"
                 "(fn shout ((s str)) (print s \"!\"))\n"
                 "(fn compare ((a ptr) (b ptr)) int\n"
                 "  (return (- (load int b) (load int a))))\n"
                 "(let f (phrase-of triple))\n"
                 "(print (call f 4))\n"
                 "(call (phrase-of shout) \"hey\")\n"
                 "(print (ovt_visit (phrase-of triple) 5) (ovt_visit null 5))\n"
                 "(let numbers (list 3 9 1 7))\n"
                 "(qsort numbers (len numbers) 8 (phrase-of compare))\n"
                 "(print numbers)\n") ==
        "12\nhey !\n15 -5\n[9, 7, 3, 1]\n");
}

TEST_CASE("A function-like macro that is one call is a function.",
          "[c-import]") {
  if (!ovis::core::has_c_import_support())
    SKIP("Built without libclang.");
  CHECK(run_core("(use-c \"ovistest.h\" \"ovistest\")\n"
                 "(print (ovt_twice 21) (ovt_hello))\n") == "42 hello, macro\n");
}

TEST_CASE("C macros of one expression take the variant their arguments fit.",
          "[c-import]") {
  if (!ovis::core::has_c_import_support())
    SKIP("Built without libclang.");
  const auto &library = ovis::core::import_c_library("ovistest.h", "ovistest",
                                                     test_c_imports());
  const auto max = std::ranges::find(library.functions, "OVT_MAX",
                                     &ovis::core::c_function::name);
  REQUIRE(max != library.functions.end());
  std::vector<std::string> variants;
  for (const auto &variant : max->variants)
    variants.push_back(variant.name);
  std::ranges::sort(variants);
  // Text with text compares pointers, so it is a variant too; text with a
  // number only warns in C, and is left out.
  CHECK(variants == std::vector<std::string>{"OVT_MAX__ff", "OVT_MAX__fi",
                                             "OVT_MAX__if", "OVT_MAX__ii",
                                             "OVT_MAX__ss"});
  for (const auto *const left_out : {"OVT_SWAP", "OVT_ZERO_OF"})
    CHECK(std::ranges::find(library.functions, std::string(left_out),
                            &ovis::core::c_function::name) ==
          library.functions.end());

  CHECK(run_core("(use-c \"ovistest.h\" \"ovistest\")\n"
                 "(let n 7)\n"
                 "(print (OVT_MAX 3 n) (OVT_MAX 2.5 1) (OVT_SQUARE 1.5)"
                 " (OVT_SQUARE 4))\n"
                 "(print (OVT_FIRST_CHAR \"hey\") (OVT_IS_EMPTY \"\")"
                 " (OVT_CLAMP 15 0 10) (+ (OVT_CLAMP 0.5 0 1) 1))\n"
                 "(fn bigger (a b) (return (OVT_MAX a b)))\n"
                 "(print (bigger 4 9))\n") ==
        "7 2.5 2.25 16\n104 1 10 1.5\n9\n");
  ovis::core::program program;
  program.units.push_back(ovis::core::parse_unit(
      "(use-c \"ovistest.h\" \"ovistest\")\n(print (OVT_SQUARE \"x\"))"));
  REQUIRE(ovis::core::resolve_c_imports(program, test_c_imports()).empty());
  ovis::core::elaborate(program);
  const auto problems = ovis::core::check(program);
  REQUIRE(problems.size() == 1);
  CHECK_THAT(problems.front().message,
             Catch::Matchers::ContainsSubstring(
                 "The C macro 'OVT_SQUARE' can't be given text; it takes "
                 "(a whole number) or (a decimal number)"));
}
