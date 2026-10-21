#ifndef OVIS_PROJECT_LOCK_HPP
#define OVIS_PROJECT_LOCK_HPP

#include <filesystem>
#include <optional>
#include <vector>

#include <ovis/def.hpp>
#include <ovis/project/project.hpp>

// The project's ovis.lock, beside ovis.toml: the commit each git dependency
// was pinned to (proposals/projects.md §4, packages.md §8). Not the
// per-file `<file>.lock`, which holds a file's meanings. Commit it.
namespace ovis::project {

constexpr const char *project_lock_name = "ovis.lock";

struct locked_c_dependency {
  string_type name;
  string_type git;
  // What ovis.toml asked for (a tag, or a commit), and what it resolved to.
  string_type tag;
  string_type commit;

  friend auto operator==(const locked_c_dependency &,
                         const locked_c_dependency &) -> bool = default;
};

// One package of the graph (packages.md §8).
struct locked_package {
  string_type name;
  string_type git;
  // A folder instead (an override, or a dependency by path): not fetched.
  string_type path;
  string_type line;
  string_type version;
  string_type commit;
  // sha256 over the tree's sorted paths and contents.
  string_type tree;
  // `name@line` of what it depends on.
  std::vector<string_type> dependencies;

  friend auto operator==(const locked_package &, const locked_package &)
      -> bool = default;
};

class project_lock {
public:
  std::vector<locked_c_dependency> c;
  std::vector<locked_package> packages;

  static auto load(const std::filesystem::path &p_path) -> project_lock;
  // Sorted, so the same graph is the same file.
  auto to_text() const -> string_type;
  // Written only when its text changes.
  auto save(const std::filesystem::path &p_path) const -> void;

  auto find_c(string_view_type p_name) const -> const locked_c_dependency *;

  friend auto operator==(const project_lock &, const project_lock &) -> bool = default;
};

// The commit a tag of a git repository points to (`git ls-remote`).
auto resolve_git_tag(const string_type &p_url, const string_type &p_tag)
    -> string_type;

// Pins every git C dependency of `p_project` in `p_lock`: kept as locked
// while ovis.toml asks for the same tag, resolved when new or changed, and
// resolved again when named in `p_update` (or all, if it holds "*"). Each
// message says what moved. Entries for tables no longer there are dropped.
auto lock_c_dependencies(const project &p_project, project_lock &p_lock,
                         const std::vector<string_type> &p_update = {})
    -> std::vector<string_type>;

} // namespace ovis::project

#endif // OVIS_PROJECT_LOCK_HPP
