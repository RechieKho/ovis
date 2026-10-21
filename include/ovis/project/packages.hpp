#ifndef OVIS_PROJECT_PACKAGES_HPP
#define OVIS_PROJECT_PACKAGES_HPP

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <vector>

#include <ovis/def.hpp>
#include <ovis/project/lock.hpp>
#include <ovis/project/project.hpp>

// Ovis packages hosted anywhere git is (proposals/packages.md): a package
// is a repository, its versions are `vX.Y.Z` tags, its URL its name.
// Versions are chosen by minimal version selection, one per line (§3).
namespace ovis::project {

// Where fetched packages are kept: $OVIS_PACKAGE_CACHE, else
// $XDG_CACHE_HOME/ovis/packages, else ~/.cache/ovis/packages.
auto package_cache_directory() -> std::filesystem::path;

// A URL as a package's identity: `https://github.com/a/b.git/` and
// `https://github.com/a/b` are the same package (§4).
auto normalized_url(string_view_type p_url) -> string_type;

// The `vX.Y.Z` tags of a repository and the commits they name.
auto list_version_tags(const string_type &p_url) -> std::map<version, string_type>;

// A sha256 over a folder's files: their sorted paths and contents, `.git`
// left out. What ovis.lock records, so a rewritten repository is noticed.
auto tree_hash(const std::filesystem::path &p_folder) -> string_type;

// The files of `p_url` at `p_commit`, fetched once into the cache and
// kept read-only there. Checks the tree against `p_tree` when given.
auto fetch_package(const string_type &p_url, const string_type &p_commit,
                   const string_type &p_tree = {}) -> std::filesystem::path;

// One package of a program's graph.
struct resolved_package {
  // `name@line`, unique in the graph: what modules, messages and ovis.lock
  // call it (`log@1`, `log@2`, or `log@1~2` for a second URL named log).
  string_type id;
  string_type name;
  string_type git;
  // A package by path (a dependency or an override): not fetched.
  std::filesystem::path path;
  string_type line;
  version package_version;
  string_type commit;
  string_type tree;
  // Where its files are: the cache, or the path.
  std::filesystem::path root;
  // Its own ovis.toml.
  project manifest;
  // Its direct dependencies: the name its lines use → the package's id.
  std::map<string_type, string_type> dependencies;
  // Who asked for which minimum, for messages ("csv asks 1.2").
  std::vector<string_type> asked_by;
};

struct package_graph {
  std::vector<resolved_package> packages;
  // The root project's direct dependencies: name → id.
  std::map<string_type, string_type> roots;

  auto find(string_view_type p_id) const -> const resolved_package *;
};

struct resolve_options {
  // Use ovis.lock's versions when they still satisfy ovis.toml, without
  // asking any repository (the usual build).
  const project_lock *lock = nullptr;
  // Names (or "*") whose versions may move to the newest tag on their line
  // (`ovis update`).
  std::vector<string_type> update;
  // No network: only the lock and the cache.
  bool offline = false;
  // Progress, for the command line.
  std::function<void(const string_type &)> log;
};

// The graph `p_project` needs (packages.md §3–§4). Throws an `error` that
// names who asked for what when it can't be built.
auto resolve_packages(const project &p_project, const resolve_options &p_options)
    -> package_graph;

// The graph as ovis.lock records it.
auto lock_packages(const package_graph &p_graph) -> std::vector<locked_package>;

// Every C dependency of the program and its packages (packages.md §4: one
// C library per program): the same pkg-config module, git URL or folder
// once, its targets joined; `path` and `cmake` sources made absolute. Two
// tags of one git library are an error naming who wants which, unless the
// root's [overrides] has `"c.NAME" = { tag = … }`.
auto combined_c_dependencies(const project &p_root, const package_graph &p_graph)
    -> std::vector<c_dependency>;

// `ovis tree`: the graph, one package a line, indented under who uses it.
auto describe_graph(const package_graph &p_graph) -> string_type;

} // namespace ovis::project

#endif // OVIS_PROJECT_PACKAGES_HPP
