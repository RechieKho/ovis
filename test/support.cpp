#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <catch2/catch_test_macros.hpp>
#include <llvm/IR/LLVMContext.h>

#include <ovis/codegen/llvm_codegen.hpp>
#include <ovis/core/elaborate.hpp>
#include <ovis/core/modules.hpp>

#include "support.hpp"

auto run_program(const ovis::core::program &p_program,
                 const std::string &p_input) -> std::string {
  llvm::LLVMContext context;
  auto module = ovis::codegen::generate_module(p_program, context, "test");
  ovis::codegen::optimize(*module);
  const auto executable =
      std::filesystem::temp_directory_path() / "ovis-test-program";
  ovis::codegen::build_executable(*module, executable,
                                  ovis::codegen::libraries_of(p_program));

  auto command = executable.string();
  if (!p_input.empty())
    command = "printf '" + p_input + "' | " + command;
  std::string output;
  auto *const pipe = popen(command.c_str(), "r");
  REQUIRE(pipe != nullptr);
  char buffer[256];
  while (const auto count = std::fread(buffer, 1, sizeof(buffer), pipe))
    output.append(buffer, count);
  pclose(pipe);
  std::filesystem::remove(executable);
  return output;
}

namespace test_support {

auto check_text(const std::string &p_core)
    -> std::vector<ovis::core::diagnostic> {
  ovis::core::program program;
  program.units.push_back(ovis::core::parse_unit(p_core));
  ovis::core::elaborate(program);
  return ovis::core::check(program);
}

// The elaborator's notes, one string each.
auto elaborate_text(const std::string &p_core, ovis::core::program &p_program)
    -> std::vector<std::string> {
  p_program.units.push_back(ovis::core::parse_unit(p_core));
  std::vector<std::string> result;
  for (const auto &note : ovis::core::elaborate(p_program))
    result.push_back(note.message);
  return result;
}

auto mentions(const std::vector<std::string> &p_notes,
              const std::string &p_text) -> bool {
  for (const auto &note : p_notes)
    if (note.find(p_text) != std::string::npos)
      return true;
  return false;
}

auto read_file(const std::filesystem::path &p_path) -> std::string {
  std::ifstream input(p_path);
  std::stringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

// Where the tests' own C library is.
auto test_c_imports() -> ovis::core::c_import_options {
  return {{OVIS_TEST_CLIB_INCLUDE}, {OVIS_TEST_CLIB_LIBRARY}};
}

auto run_core(const std::string &p_core, const std::string &p_input)
    -> std::string {
  ovis::core::program program;
  program.units.push_back(ovis::core::parse_unit(p_core));
  for (const auto &problem :
       ovis::core::resolve_c_imports(program, test_c_imports()))
    UNSCOPED_INFO(problem.message);
  ovis::core::elaborate(program);
  const auto problems = ovis::core::check(program);
  for (const auto &problem : problems)
    UNSCOPED_INFO(problem.message);
  REQUIRE(problems.empty());
  return run_program(program, p_input);
}

} // namespace test_support
