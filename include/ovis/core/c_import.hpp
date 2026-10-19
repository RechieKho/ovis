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
  // Other flags the headers are read with (`-DNAME=1`): what CMake knows
  // about the C target the program uses.
  std::vector<string_type> compiler_flags;
  // The caller links the C library (CMake does): no pkg-config `--libs`, no
  // `-l`. Headers are read as usual.
  bool link_by_caller = false;
};

struct c_library {
  std::filesystem::path header_path;
  // Identifies the header's exact contents; recorded in the lockfile.
  string_type header_hash;
  std::vector<c_function> functions;
  std::vector<c_constant> constants;
  // The complete structs and unions the header defines, as C records.
  std::vector<record> records;
  // Flags for the C compiler that links the program: -L... -l...
  std::vector<string_type> link_flags;
  // One line per declaration that could not be imported, and why.
  std::vector<string_type> skipped;
  // The header and the files it includes outside the system's directories:
  // what a build depends on (`--depfile`).
  std::vector<std::filesystem::path> dependencies;
};

// Whether this build of Ovis can read C headers (it was built with libclang).
auto has_c_import_support() -> bool;

// Where system headers are looked for after the -I directories and
// pkg-config's: /usr/include and friends, and on macOS the SDK's
// (`xcrun --show-sdk-path`), where the system keeps zlib's and SQLite's.
auto system_include_directories() -> std::vector<std::filesystem::path>;

// The dependencies of every header imported so far, without repeats.
auto imported_dependencies() -> std::vector<std::filesystem::path>;

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

} // namespace ovis::core

#endif // OVIS_CORE_C_IMPORT_HPP
