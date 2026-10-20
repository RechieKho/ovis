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

namespace {

// A program whose units are (module, Ovis Core) pairs, resolved like the
// compiler does. Returns the problems, or runs it when there are none.
auto run_modules(const std::vector<std::pair<std::string, std::string>> &p_units,
                 std::vector<std::string> *p_problems = nullptr)
    -> std::string {
  ovis::core::program program;
  for (const auto &[module, core] : p_units) {
    program.units.push_back(ovis::core::parse_unit(core));
    program.units.back().module = module;
  }
  std::vector<std::string> problems;
  for (const auto &problem : ovis::core::resolve_modules(program))
    problems.push_back(problem.message);
  ovis::core::elaborate(program);
  for (const auto &problem : ovis::core::check(program))
    problems.push_back(problem.message);
  if (p_problems != nullptr) {
    *p_problems = problems;
    return "";
  }
  for (const auto &problem : problems)
    UNSCOPED_INFO(problem);
  REQUIRE(problems.empty());
  return run_program(program);
}

} // namespace

TEST_CASE("Resolve names across modules.", "[modules]") {
  const std::pair<std::string, std::string> square = {
      "geometry.square", "(fn area (side) (* side side))"};
  const std::pair<std::string, std::string> circle = {
      "geometry.circle", "(fn area (radius) (* 3 radius radius))"};
  const std::pair<std::string, std::string> rates = {
      "rates", "(let rate 2)\n(fn scaled (value) (* value rate))"};

  // A name only one module defines needs no qualification; types flow
  // across modules (`value` is a float because main passes one).
  REQUIRE(run_modules({rates, {"main", "(print (scaled 1.5) rate)"}}) ==
          "3.0 2\n");

  // Two modules define `area`: main must say which, by any tail of the
  // module's name.
  std::vector<std::string> problems;
  run_modules({square, circle, {"main", "(print (area 2))"}}, &problems);
  REQUIRE(!problems.empty());
  REQUIRE(problems[0].find("geometry.circle.area or geometry.square.area") !=
          std::string::npos);
  REQUIRE(run_modules({square, circle,
                       {"main", "(print (square.area 2) "
                                "(geometry.circle.area 1))"}}) == "4 3\n");

  // A module's own definition comes first, and locals shadow globals.
  REQUIRE(run_modules({rates,
                       {"main", "(let rate 10)\n"
                                "(fn twice (rate) (* rate 2))\n"
                                "(print rate (twice 4) (rates.rate))"}},
                      &problems) == "");
  REQUIRE(problems.empty() == false); // (rates.rate) is not a function
  REQUIRE(run_modules({rates, {"main", "(let rate 10)\n"
                                       "(fn twice (rate) (* rate 2))\n"
                                       "(print rate (twice 4) rates.rate)"}}) ==
          "10 8 2\n");
}

TEST_CASE("Helper modules only define things.", "[modules]") {
  std::vector<std::string> problems;
  run_modules({{"helpers", "(print \"hi\")"}, {"main", "(pass)"}}, &problems);
  REQUIRE(problems.size() == 1);
  REQUIRE(problems[0].find("helper file") != std::string::npos);

  run_modules({{"helpers", "(let n (read_int))"}, {"main", "(pass)"}},
              &problems);
  REQUIRE(problems.size() == 1);

  // Data computed from other data is a definition, and is ready before main
  // runs.
  REQUIRE(run_modules({{"data", "(let base 4)\n(let sizes (list base (* base "
                                "2)))\n(let label (+ \"size \" base))"},
                       {"main", "(print sizes label)"}}) ==
          "[4, 8] size 4\n");
}


