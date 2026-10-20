#ifndef OVIS_CORE_C_IMPORT_HPP
#define OVIS_CORE_C_IMPORT_HPP

#include <filesystem>
#include <functional>
#include <map>
#include <set>
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

// A call of a generic C macro (generics.md §9) with its arguments as C has
// them.
struct macro_argument {
  c_function::macro_role role = c_function::macro_role::value;
  // A value's C type (`uint32_t`, `struct item *`; for an address, what it
  // points at), a type given as an argument, or a field's name.
  string_type spelling;
  // How Ovis passes the value (or what an address points at).
  c_type type = c_type::long_type;
  // A C record's name, when the value is one (a pointer to its struct).
  string_type record;
};
struct macro_request {
  const c_function *macro;
  std::vector<macro_argument> arguments;
  // Every header the program uses, and their flags: an argument may be a
  // struct another header defines.
  std::vector<string_type> headers;
  std::vector<string_type> flags;
};
// The headers (and their flags) a program's C functions come from.
auto headers_of(const program &p_program)
    -> std::pair<std::vector<string_type>, std::vector<string_type>>;
// The copy the call calls (`OVT_MAX__i64_f64`), or why clang refused it.
struct macro_answer {
  std::optional<c_function> copy;
  string_type problem;
};

// Asks clang, in one file, what each call's macro gives back for its
// arguments' types, and makes each call's copy. Answers are kept by the
// header's hash and the call (`c_macro_answers`), so asking again (or a
// build whose lockfile has them) asks clang nothing.
auto copy_c_macros(const std::vector<macro_request> &p_requests)
    -> std::vector<macro_answer>;
// A call's arguments as a generic C macro takes them: each value's C type
// (`p_type_of`, the type of the argument at an index), a type or field
// given (`(type T)`, `(member NAME)`), an address for a value it assigns to
// (`(ref x)`). Nothing while a type isn't known (unless `p_guess`: a whole
// number); `p_problem` says why the call can't be made.
auto macro_arguments_of(const program &p_program, const c_function &p_macro,
                        const std::vector<expression> &p_arguments,
                        const std::function<value_type(std::size_t)> &p_type_of,
                        bool p_guess, string_type &p_problem)
    -> std::optional<std::vector<macro_argument>>;
// The C record a copy gives back a pointer to (its result's spelling).
auto macro_result_record(const program &p_program, const c_function &p_copy)
    -> string_type;

// The answers known, by key: what lockfiles had, and what clang said.
auto c_macro_answers() -> std::map<string_type, string_type> &;
// The keys a build used, for its lockfile (the front end clears it).
auto c_macro_answers_used() -> std::set<string_type> &;
// How many times clang was asked, for tests.
auto c_macro_probes() -> std::size_t &;

} // namespace ovis::core

#endif // OVIS_CORE_C_IMPORT_HPP
