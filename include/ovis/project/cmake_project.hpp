#ifndef OVIS_PROJECT_CMAKE_PROJECT_HPP
#define OVIS_PROJECT_CMAKE_PROJECT_HPP

#include <filesystem>
#include <map>
#include <vector>

#include <ovis/def.hpp>
#include <ovis/project/project.hpp>

// The CMake project ovis generates from ovis.toml when a C dependency needs
// CMake (git, path, cmake; proposals/projects.md §4–§5). It is written into
// build/cmake/ at every build and never edited: ovis.toml stays the source.
namespace ovis::project {

// cmake/OvisMacros.cmake, compiled in (ovis_add_executable).
auto embedded_ovis_macros() -> string_view_type;

struct cmake_options {
  // The ovis that compiles the program: this one.
  std::filesystem::path ovis_executable;
  // More options for it, from the command line (--offline, -I …).
  std::vector<string_type> ovis_flags;
  // Let the build settle unclear lines and write meanings, as `ovis run`
  // does; off with --locked.
  bool unlocked = true;
  bool fast = false;
  // The commit each git dependency's tag resolved to (ovis.lock), by name.
  std::map<string_type, string_type> commits;
  // More for the program's link: its packages' C, compiled by cc.
  std::vector<string_type> link_inputs;
};

// The CMakeLists.txt for `p_project`.
auto cmake_project_text(const project &p_project, const cmake_options &p_options)
    -> string_type;

// Writes the project into `p_build_directory`/cmake, only the files whose
// text changed, and returns that folder (`ovis cmake`).
auto write_cmake_project(const project &p_project, const cmake_options &p_options,
                         const std::filesystem::path &p_build_directory)
    -> std::filesystem::path;

// Writes the project into `p_build_directory`/cmake (only files whose text
// changed, so CMake's own checks decide what to rebuild), configures it
// into `p_build_directory`/cmake-build if needed and builds it. Returns the
// executable. CMake's output is shown when it fails, or with `p_verbose`.
auto build_with_cmake(const project &p_project, const cmake_options &p_options,
                      const std::filesystem::path &p_build_directory,
                      bool p_verbose) -> std::filesystem::path;

} // namespace ovis::project

#endif // OVIS_PROJECT_CMAKE_PROJECT_HPP
