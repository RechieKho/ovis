#ifndef OVIS_CODEGEN_LLVM_CODEGEN_HPP
#define OVIS_CODEGEN_LLVM_CODEGEN_HPP

#include <filesystem>
#include <memory>

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <ovis/core/ast.hpp>

namespace ovis::codegen {

// Lowers a checked program to LLVM IR. The program's top-level statements
// become `main`.
auto generate_module(const core::program &p_program,
                     llvm::LLVMContext &p_context,
                     string_view_type p_module_name)
    -> std::unique_ptr<llvm::Module>;

// Sets the host target on the module and runs the default -O2 pipeline.
auto optimize(llvm::Module &p_module) -> void;

auto to_ir_text(const llvm::Module &p_module) -> string_type;

// Linker flags a program needs: -lm, every `(link ...)`, and the flags of
// the C libraries it imports.
auto libraries_of(const core::program &p_program) -> std::vector<string_type>;

// Emits an object file for the host and links it with the runtime using the
// system C compiler (`cc`, `clang` or `gcc`) and `p_link_flags`.
auto build_executable(llvm::Module &p_module,
                      const std::filesystem::path &p_output,
                      const std::vector<string_type> &p_link_flags = {"-lm"})
    -> void;

} // namespace ovis::codegen

#endif // OVIS_CODEGEN_LLVM_CODEGEN_HPP
