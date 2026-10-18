#include <cstdio>
#include <filesystem>

#include <catch2/catch_test_macros.hpp>
#include <llvm/IR/LLVMContext.h>

#include <ovis/codegen/llvm_codegen.hpp>

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
