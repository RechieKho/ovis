#ifndef OVIS_CORE_C_IMPORT_HPP
#define OVIS_CORE_C_IMPORT_HPP

#include <filesystem>
#include <vector>

#include <ovis/core/ast.hpp>
#include <ovis/core/checker.hpp>

// Reads C headers so Ovis programs can use any C library: `(use-c "zlib.h"
// "z")` makes the header's functions and constants callable. Done with
// libclang, deterministically: the same header always gives the same result.
namespace ovis::core {

struct c_import_options {
  // Extra -I and -L directories (the CLI's -I and -L options).
  std::vector<std::filesystem::path> include_directories;
  std::vector<std::filesystem::path> library_directories;
};

struct c_library {
  std::filesystem::path header_path;
  // Identifies the header's exact contents; recorded in the lockfile.
  string_type header_hash;
  std::vector<c_function> functions;
  std::vector<c_constant> constants;
  // Flags for the C compiler that links the program: -L... -l...
  std::vector<string_type> link_flags;
  // One line per declaration that could not be imported, and why.
  std::vector<string_type> skipped;
};

// Whether this build of Ovis can read C headers (it was built with libclang).
auto has_c_import_support() -> bool;

// Imports a header. `p_library` is the library to link: a pkg-config
// package or a name for -l. Results are cached for the life of the process.
// Throws `error` when the header can't be found or read.
auto import_c_library(string_view_type p_header, string_view_type p_library,
                      const c_import_options &p_options) -> const c_library &;

// Imports every `(use-c ...)` of the program into its units (as externs,
// constants and link flags). Problems are reported against the unit that
// asked for the header.
auto resolve_c_imports(program &p_program, const c_import_options &p_options)
    -> std::vector<diagnostic>;

// The imported functions and constants most related to `p_text` (words
// shared with their names and comments), at most `p_limit`, described in
// Ovis Core for the model. Deterministic.
auto relevant_c_declarations(const program &p_program, string_view_type p_text,
                             std::size_t p_limit) -> std::vector<string_type>;

} // namespace ovis::core

#endif // OVIS_CORE_C_IMPORT_HPP
