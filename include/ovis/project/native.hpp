#ifndef OVIS_PROJECT_NATIVE_HPP
#define OVIS_PROJECT_NATIVE_HPP

#include <filesystem>
#include <vector>

#include <ovis/def.hpp>
#include <ovis/project/project.hpp>

// A project's C built without CMake (proposals/projects.md §3): its
// `c = [...]` files compiled by cc into build/c/, and its pkg-config
// dependencies' flags.
namespace ovis::project {

struct command_result {
  // -1 when the program couldn't be started (`errors` says why).
  int status = -1;
  string_type output;
  string_type errors;
};

// Runs a program found on the PATH, its output and errors captured.
auto run_command(const string_type &p_program,
                 const std::vector<string_type> &p_arguments) -> command_result;

// Whether `p_path` (relative, with `/`) matches `p_pattern`: `*` and `?`
// within one folder, `**` across folders.
auto path_matches(string_view_type p_pattern, string_view_type p_path) -> bool;

// The files `c = [...]` names, sorted; a pattern matching nothing is an
// error (a typo would otherwise build without the file).
auto c_source_files(const project &p_project) -> std::vector<std::filesystem::path>;

// pkg-config's --cflags or --libs for a dependency, split into words.
auto pkg_config_flags(const c_dependency &p_dependency, bool p_libraries)
    -> std::vector<string_type>;

// Whether a dependency needs the generated CMake project (git, path, cmake)
// rather than cc alone (§3).
auto needs_cmake(const project &p_project) -> bool;

struct native_parts {
  // For reading headers: -I and -D from [build] and pkg-config.
  std::vector<std::filesystem::path> include_directories;
  std::vector<string_type> compiler_flags;
  // For linking, in order: the project's objects, then the libraries.
  std::vector<string_type> link_inputs;
};

// Gathers the flags the program is read and linked with and, when
// `p_compile`, compiles the project's C files that changed (or whose
// headers did) into `p_build_directory`/c. Errors are cc's own, with the
// file. Without `p_compile` (check, fmt) nothing is written.
auto build_native_parts(const project &p_project,
                        const std::filesystem::path &p_build_directory,
                        bool p_compile = true) -> native_parts;

} // namespace ovis::project

#endif // OVIS_PROJECT_NATIVE_HPP
