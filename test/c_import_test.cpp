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
  // A struct by value can't be passed yet: skipped, with the reason.
  REQUIRE(find("ovt_point_sum") == library.functions.end());
  REQUIRE(std::ranges::any_of(library.skipped, [](const auto &p_line) {
    return p_line.starts_with("ovt_point_sum");
  }));
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
  // On macOS the SDK's headers are found, but libclang there can't yet find
  // the compiler's own headers (stddef.h) to read them.
#ifdef __APPLE__
  SKIP("The SDK's headers need libclang's resource directory.");
#endif
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

