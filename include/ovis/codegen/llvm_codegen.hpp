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
// A checked program (the default) knows which source line is running and
// finds values used after they were freed; `p_checked` false is `--fast`.
auto generate_module(const core::program &p_program,
                     llvm::LLVMContext &p_context,
                     string_view_type p_module_name, bool p_checked = true)
    -> std::unique_ptr<llvm::Module>;

// Sets the host target on the module and runs the default -O2 pipeline.
auto optimize(llvm::Module &p_module) -> void;

auto to_ir_text(const llvm::Module &p_module) -> string_type;

// Linker flags a program needs: -lm, every `(link ...)`, and the flags of
// the C libraries it imports.
auto libraries_of(const core::program &p_program) -> std::vector<string_type>;

// Writes the module as an object file for the host and stops there: the
// runtime and the C libraries are linked by whoever builds the program (CMake).
auto emit_object(llvm::Module &p_module, const std::filesystem::path &p_output)
    -> void;

// Emits an object file for the host and links it with the runtime using the
// system C compiler (`cc`, `clang` or `gcc`) and `p_link_flags`.
// The runtime is built checked, to match `generate_module`, and lists at exit
// the values never freed with `p_report_leaks`.
auto build_executable(llvm::Module &p_module,
                      const std::filesystem::path &p_output,
                      const std::vector<string_type> &p_link_flags = {"-lm"},
                      bool p_checked = true, bool p_report_leaks = false)
    -> void;

} // namespace ovis::codegen

#endif // OVIS_CODEGEN_LLVM_CODEGEN_HPP
