#ifndef OVIS_PROJECT_PROJECT_HPP
#define OVIS_PROJECT_PROJECT_HPP

#include <compare>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <vector>

#include <ovis/def.hpp>

// A project: a folder with `ovis.toml` saying what its program needs
// (proposals/projects.md), and, with a `[package]` table, a package other
// projects depend on through git (proposals/packages.md).
namespace ovis::project {

constexpr const char *project_file_name = "ovis.toml";

// A semantic version, `1.4.0`.
struct version {
  int64_type major = 0;
  int64_type minor = 0;
  int64_type patch = 0;

  auto operator<=>(const version &) const = default;
  auto text() const -> string_type;
  // The line it belongs to (packages.md §2): the major, or before 1.0 the
  // minor, so 0.3 and 0.4 are as different as 1 and 2.
  auto line() const -> string_type;
};

// `1.4.0`, `1.4`, `1` (missing parts are 0), with an optional leading `v`.
auto parse_version(string_view_type p_text) -> std::optional<version>;

struct c_dependency {
  enum class kind_type { pkg_config, git, path, cmake };

  string_type name;
  kind_type kind = kind_type::pkg_config;
  // pkg-config's module (and minimum, `zlib >= 1.2.11`), a git URL, a
  // folder or a CMake file, by kind.
  string_type source;
  string_type tag;
  string_type commit;
  std::vector<string_type> targets;
  std::size_t line = 0;
};

// A dependency on an Ovis package (packages.md §2, §5).
struct package_dependency {
  // Its name in this project's lines.
  string_type name;
  string_type git;
  // A folder instead of git (an override, or a package beside this one).
  std::filesystem::path path;
  // `version = "1.2"`: 1.2.0 or later on its line.
  std::optional<version> minimum;
  // `version = "=1.2.3"` or `commit = "…"`: only in a root project.
  bool exact = false;
  string_type commit;
  // A tag as written: an override of a C dependency (`"c.cjson"`) keeps
  // any tag, a package's must be a version.
  string_type tag;
  std::size_t line = 0;
};

struct package_info {
  string_type name;
  version package_version;
  // The oldest toolchain that builds it.
  std::optional<version> ovis;
};

class project {
public:
  // The folder holding ovis.toml, and the file itself.
  std::filesystem::path root;
  std::filesystem::path file;

  // [project]
  string_type name;
  string_type toolchain;
  // [build], as written (relative to root).
  std::vector<string_type> include;
  std::vector<string_type> defines;
  std::vector<string_type> phrases;
  std::vector<string_type> c_sources;
  // [c.NAME], in the file's order.
  std::vector<c_dependency> c_dependencies;
  // [package], [dependencies], [overrides]
  std::optional<package_info> package;
  std::vector<package_dependency> dependencies;
  // Keyed by a dependency's name or URL; replaces it in the whole graph.
  std::vector<package_dependency> overrides;

  // The executable's name: [project] name, else the folder's.
  auto program_name() const -> string_type;
};

// Reads and checks `p_file`; errors are `ovis.toml, line N: …`.
auto load_project(const std::filesystem::path &p_file) -> project;
auto parse_project(string_view_type p_text, const std::filesystem::path &p_file)
    -> project;

// The nearest ovis.toml in `p_start` or a folder above it.
auto find_project(const std::filesystem::path &p_start)
    -> std::optional<std::filesystem::path>;

} // namespace ovis::project

#endif // OVIS_PROJECT_PROJECT_HPP
