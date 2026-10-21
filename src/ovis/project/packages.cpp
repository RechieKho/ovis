#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/SHA256.h>

#include <ovis/project/native.hpp>
#include <ovis/project/packages.hpp>

namespace ovis::project {

auto package_graph::find(string_view_type p_id) const -> const resolved_package * {
  for (const auto &package : packages)
    if (package.id == p_id)
      return &package;
  return nullptr;
}

auto package_cache_directory() -> std::filesystem::path {
  const auto variable = [](const char *p_name) -> std::optional<std::filesystem::path> {
    const char *const value = std::getenv(p_name);
    if (value == nullptr || *value == '\0')
      return std::nullopt;
    return std::filesystem::path(value);
  };
  if (const auto cache = variable("OVIS_PACKAGE_CACHE"))
    return *cache;
  if (const auto cache = variable("XDG_CACHE_HOME"))
    return *cache / "ovis" / "packages";
  if (const auto home = variable("HOME"))
    return *home / ".cache" / "ovis" / "packages";
  return std::filesystem::temp_directory_path() / "ovis" / "packages";
}

auto normalized_url(string_view_type p_url) -> string_type {
  string_type url(p_url);
  while (url.ends_with('/'))
    url.pop_back();
  if (url.ends_with(".git"))
    url.resize(url.size() - 4);
  return url;
}

namespace {

auto read_text(const std::filesystem::path &p_path) -> string_type {
  std::ifstream input(p_path, std::ios::binary);
  std::stringstream text;
  text << input.rdbuf();
  return text.str();
}

auto trimmed(string_type p_text) -> string_type {
  while (!p_text.empty() && (p_text.back() == '\n' || p_text.back() == ' '))
    p_text.pop_back();
  return p_text;
}

auto git(const std::vector<string_type> &p_arguments, const string_type &p_what)
    -> string_type {
  const auto result = run_command("git", p_arguments);
  if (result.status != 0)
    throw error(p_what + ": " +
                (result.status == -1 ? string_type("git is needed for packages")
                                     : trimmed(result.errors)));
  return result.output;
}

// A folder name for a URL in the cache.
auto cache_name(string_view_type p_url) -> string_type {
  string_type name;
  for (const auto character : normalized_url(p_url))
    name.push_back(std::isalnum(static_cast<unsigned char>(character)) ||
                           character == '.' || character == '-'
                       ? character
                       : '_');
  return name;
}

// Whether a tag is a whole version, `v1.4.0`.
auto version_tag(string_view_type p_tag) -> std::optional<version> {
  if (!p_tag.starts_with('v') || std::ranges::count(p_tag, '.') != 2)
    return std::nullopt;
  return parse_version(p_tag);
}

} // namespace

auto list_version_tags(const string_type &p_url) -> std::map<version, string_type> {
  const auto output =
      git({"ls-remote", "--tags", p_url}, "Can't list the versions of " + p_url);
  std::map<version, string_type> direct;
  std::map<version, string_type> peeled;
  std::istringstream lines(output);
  for (string_type line; std::getline(lines, line);) {
    const auto tab = line.find('\t');
    if (tab == string_type::npos || !line.substr(tab + 1).starts_with("refs/tags/"))
      continue;
    auto tag = line.substr(tab + 1 + 10);
    const bool is_peeled = tag.ends_with("^{}");
    if (is_peeled)
      tag.resize(tag.size() - 3);
    if (const auto parsed = version_tag(tag))
      (is_peeled ? peeled : direct)[*parsed] = line.substr(0, tab);
  }
  // An annotated tag names its own object; ^{} is the commit.
  for (const auto &[parsed, commit] : peeled)
    direct[parsed] = commit;
  return direct;
}

auto tree_hash(const std::filesystem::path &p_folder) -> string_type {
  std::vector<std::filesystem::path> files;
  for (auto entry = std::filesystem::recursive_directory_iterator(p_folder);
       entry != std::filesystem::recursive_directory_iterator(); ++entry) {
    if (entry->is_directory() && entry->path().filename() == ".git") {
      entry.disable_recursion_pending();
      continue;
    }
    if (entry->is_regular_file())
      files.push_back(entry->path().lexically_relative(p_folder));
  }
  std::ranges::sort(files, {}, [](const auto &p_path) { return p_path.generic_string(); });
  llvm::SHA256 hash;
  for (const auto &file : files) {
    const auto name = file.generic_string();
    const auto contents = read_text(p_folder / file);
    // NAME \0 SIZE \0 CONTENTS: no two trees give the same stream.
    const auto size = std::to_string(contents.size());
    for (const string_view_type part : {string_view_type(name), string_view_type("\0", 1),
                                        string_view_type(size), string_view_type("\0", 1),
                                        string_view_type(contents)})
      hash.update(llvm::StringRef(part.data(), part.size()));
  }
  const auto digest = hash.final();
  static constexpr char hex[] = "0123456789abcdef";
  string_type text = "sha256:";
  for (const auto byte : digest) {
    text.push_back(hex[byte >> 4]);
    text.push_back(hex[byte & 15]);
  }
  return text;
}

auto fetch_package(const string_type &p_url, const string_type &p_commit,
                   const string_type &p_tree) -> std::filesystem::path {
  const auto cache = package_cache_directory();
  const auto known = cache / "commits" / (cache_name(p_url) + "@" + p_commit);
  std::error_code ignored;
  if (std::filesystem::exists(known, ignored)) {
    const auto tree = trimmed(read_text(known));
    const auto folder = cache / "trees" / tree.substr(tree.find(':') + 1);
    if ((p_tree.empty() || p_tree == tree) &&
        std::filesystem::is_directory(folder, ignored))
      return folder;
  }
  const auto repository = cache / "repos" / (cache_name(p_url) + ".git");
  if (!std::filesystem::exists(repository / "HEAD", ignored)) {
    std::filesystem::create_directories(repository);
    git({"init", "-q", "--bare", repository.string()}, "Can't make " + repository.string());
  }
  const auto git_dir = "--git-dir=" + repository.string();
  // The commit itself where the host allows it (GitHub does), else every
  // tag and branch.
  const auto has_commit = [&] {
    return run_command("git", {git_dir, "cat-file", "-e", p_commit + "^{commit}"}).status == 0;
  };
  if (!has_commit())
    run_command("git", {git_dir, "fetch", "-q", "--depth", "1", p_url, p_commit});
  if (!has_commit())
    git({git_dir, "fetch", "-q", "--tags", p_url, "+refs/heads/*:refs/remotes/origin/*"},
        "Can't fetch " + p_url);
  if (!has_commit())
    throw error(p_url + " has no commit " + p_commit + " (was the repository rewritten?).");

  // The tree, unpacked beside its final place and moved in whole.
  const auto unpacked = cache / "trees" / ("partial-" + p_commit);
  std::filesystem::remove_all(unpacked);
  std::filesystem::create_directories(unpacked);
  const auto archive = cache / "trees" / ("partial-" + p_commit + ".tar");
  git({git_dir, "archive", "--format=tar", "-o", archive.string(), p_commit},
      "Can't read " + p_url + " at " + p_commit);
  const auto unpacking = run_command("tar", {"-xf", archive.string(), "-C", unpacked.string()});
  std::filesystem::remove(archive);
  if (unpacking.status != 0)
    throw error("Can't unpack " + p_url + ": " + trimmed(unpacking.errors));
  const auto tree = tree_hash(unpacked);
  if (!p_tree.empty() && tree != p_tree) {
    std::filesystem::remove_all(unpacked);
    throw error(p_url + " at " + p_commit.substr(0, 12) + " is not the tree ovis.lock " +
                "recorded (" + p_tree + ", now " + tree +
                "): the repository was rewritten. Check it, then `ovis update`.");
  }
  const auto folder = cache / "trees" / tree.substr(tree.find(':') + 1);
  if (std::filesystem::exists(folder, ignored))
    std::filesystem::remove_all(unpacked);
  else
    std::filesystem::rename(unpacked, folder);
  // Read-only: a dependency's files are never written by a build.
  for (const auto &entry : std::filesystem::recursive_directory_iterator(folder))
    if (entry.is_regular_file())
      std::filesystem::permissions(entry.path(),
                                   std::filesystem::perms::owner_write |
                                       std::filesystem::perms::group_write |
                                       std::filesystem::perms::others_write,
                                   std::filesystem::perm_options::remove, ignored);
  std::filesystem::create_directories(known.parent_path());
  std::ofstream(known) << tree << "\n";
  return folder;
}

namespace {

// What one dependency line asks for, after overrides.
struct requirement {
  string_type name;
  // `git:URL` or `path:FOLDER`: the package's identity.
  string_type identity;
  string_type git;
  std::filesystem::path path;
  std::optional<version> minimum;
  bool exact = false;
  string_type commit;
  // Who asked, for messages: "csv 1.4.0" or "ovis.toml".
  string_type asked_by;
};

struct node {
  string_type identity;
  string_type git;
  std::filesystem::path path;
  string_type line;
  version minimum;
  // `=1.2.3` in the root: the version, whatever else asks.
  std::optional<version> pinned;
  string_type commit_pin;
  std::vector<string_type> asked_by;
  // The names lines use for it, for `ovis update NAME`.
  std::set<string_type> names;
  // What was chosen and read.
  std::optional<version> chosen;
  string_type commit;
  string_type tree;
  std::filesystem::path root;
  std::optional<project> manifest;
  string_type name;
};

class resolver {
  const project &root;
  const resolve_options &options;
  std::map<string_type, std::map<version, string_type>> tags;
  // By identity and line.
  std::map<std::pair<string_type, string_type>, node> nodes;

  auto log(const string_type &p_text) const -> void {
    if (options.log)
      options.log(p_text);
  }

  auto updating(const string_type &p_name) const -> bool {
    return std::ranges::find(options.update, "*") != options.update.end() ||
           std::ranges::find(options.update, p_name) != options.update.end();
  }

  // A dependency line as written in `p_from`, with the root's overrides.
  auto requirement_of(const package_dependency &p_dependency,
                      const std::filesystem::path &p_from, const string_type &p_asked_by)
      const -> requirement {
    requirement result{.name = p_dependency.name,
                       .git = p_dependency.git,
                       .minimum = p_dependency.minimum,
                       .exact = p_dependency.exact,
                       .commit = p_dependency.commit,
                       .asked_by = p_asked_by};
    if (!p_dependency.path.empty())
      result.path = std::filesystem::weakly_canonical(p_from / p_dependency.path);
    for (const auto &override : root.overrides) {
      const bool by_name = override.name == p_dependency.name;
      const bool by_url = !p_dependency.git.empty() &&
                          normalized_url(override.name) == normalized_url(p_dependency.git);
      if (!by_name && !by_url)
        continue;
      if (!override.git.empty()) {
        result.git = override.git;
        result.path.clear();
      }
      if (!override.path.empty()) {
        result.path = std::filesystem::weakly_canonical(root.root / override.path);
        result.git.clear();
      }
      if (override.minimum) {
        result.minimum = override.minimum;
        result.exact = override.exact;
      }
      if (!override.commit.empty()) {
        result.commit = override.commit;
        result.exact = true;
      }
      result.asked_by += " (overridden by ovis.toml)";
    }
    result.identity = result.path.empty() ? "git:" + normalized_url(result.git)
                                          : "path:" + result.path.string();
    return result;
  }

  auto tags_of(const string_type &p_url) -> const std::map<version, string_type> & {
    const auto key = normalized_url(p_url);
    if (const auto found = tags.find(key); found != tags.end())
      return found->second;
    if (options.offline)
      throw error("Choosing a version of " + p_url +
                  " needs its tags, and the build is --offline (run once without it).");
    log("listing the versions of " + p_url);
    return tags[key] = list_version_tags(p_url);
  }

  // The line a requirement is on. A commit or a path says it by its own
  // ovis.toml, so it is read first.
  auto line_of(const requirement &p_requirement) -> string_type {
    if (p_requirement.minimum)
      return p_requirement.minimum->line();
    if (!p_requirement.path.empty()) {
      const auto manifest = read_manifest(p_requirement.path, p_requirement.name);
      return manifest.package->package_version.line();
    }
    const auto folder = fetch_package(p_requirement.git, p_requirement.commit);
    return read_manifest(folder, p_requirement.name).package->package_version.line();
  }

  static auto read_manifest(const std::filesystem::path &p_folder,
                            const string_type &p_name) -> project {
    const auto file = p_folder / project_file_name;
    std::error_code ignored;
    if (!std::filesystem::exists(file, ignored))
      throw error("The dependency `" + p_name + "` (" + p_folder.string() +
                  ") has no ovis.toml, so it is not an Ovis package.");
    auto manifest = load_project(file);
    if (!manifest.package)
      throw error("The dependency `" + p_name + "` (" + file.string() +
                  ") has no [package] table, so it is not an Ovis package.");
    return manifest;
  }

  auto add(const requirement &p_requirement) -> bool {
    const auto line = line_of(p_requirement);
    auto &entry = nodes[{p_requirement.identity, line}];
    bool changed = entry.identity.empty();
    entry.identity = p_requirement.identity;
    entry.git = p_requirement.git;
    entry.path = p_requirement.path;
    entry.line = line;
    const auto who = p_requirement.asked_by + " asks " +
                     (p_requirement.exact ? "=" : "") +
                     (p_requirement.minimum ? p_requirement.minimum->text()
                                            : p_requirement.commit.substr(0, 12));
    if (std::ranges::find(entry.asked_by, who) == entry.asked_by.end())
      entry.asked_by.push_back(who);
    entry.names.insert(p_requirement.name);
    const auto wanted = p_requirement.minimum.value_or(version{});
    if (p_requirement.exact && p_requirement.commit.empty()) {
      if (entry.pinned && *entry.pinned != wanted)
        throw error("Two exact versions of " + p_requirement.name + ": " +
                    entry.pinned->text() + " and " + wanted.text() + ".");
      entry.pinned = wanted;
    }
    if (!p_requirement.commit.empty())
      entry.commit_pin = p_requirement.commit;
    if (wanted > entry.minimum || changed) {
      changed = changed || wanted > entry.minimum;
      entry.minimum = std::max(entry.minimum, wanted);
    }
    return changed;
  }

  auto lock_entry(const node &p_node) const -> const locked_package * {
    if (options.lock == nullptr)
      return nullptr;
    for (const auto &entry : options.lock->packages)
      if (entry.line == p_node.line &&
          (p_node.path.empty() ? !entry.git.empty() &&
                                     normalized_url(entry.git) == normalized_url(p_node.git)
                               : entry.path == p_node.path.string()))
        return &entry;
    return nullptr;
  }

  // Picks the version of `p_node` and reads it. True when it changed.
  auto choose(node &p_node) -> bool {
    std::optional<version> chosen;
    string_type commit;
    string_type tree;
    if (!p_node.path.empty()) {
      auto manifest = read_manifest(p_node.path, p_node.path.filename().string());
      chosen = manifest.package->package_version;
      if (*chosen < p_node.minimum)
        throw error(p_node.path.string() + " is " + manifest.package->name + " " +
                    chosen->text() + ", older than " + p_node.minimum.text() + " (" +
                    p_node.asked_by.back() + ").");
    } else if (!p_node.commit_pin.empty()) {
      commit = p_node.commit_pin;
    } else {
      if (p_node.pinned && *p_node.pinned < p_node.minimum) {
        string_type asked;
        for (const auto &who : p_node.asked_by)
          asked += (asked.empty() ? "" : "; ") + who;
        throw error(p_node.git + ": " + asked + ". The exact version is older than "
                    "another needs: raise it, or drop the `=`.");
      }
      const auto target = p_node.pinned.value_or(p_node.minimum);
      const auto *locked = lock_entry(p_node);
      const auto locked_version =
          locked != nullptr ? parse_version(locked->version) : std::nullopt;
      bool update = locked != nullptr && updating(locked->name);
      for (const auto &name : p_node.names)
        update = update || updating(name);
      if (locked_version && !update && *locked_version >= p_node.minimum &&
          (!p_node.pinned || *locked_version == *p_node.pinned)) {
        chosen = locked_version;
        commit = locked->commit;
        tree = locked->tree;
      } else {
        const auto &available = tags_of(p_node.git);
        for (const auto &[candidate, candidate_commit] : available) {
          if (candidate.line() != p_node.line || candidate < target)
            continue;
          if (p_node.pinned && candidate != *p_node.pinned)
            continue;
          // The least that satisfies; the newest when updating.
          chosen = candidate;
          commit = candidate_commit;
          if (!update)
            break;
        }
        if (!chosen) {
          string_type asked;
          for (const auto &who : p_node.asked_by)
            asked += (asked.empty() ? "" : "; ") + who;
          throw error(p_node.git + " has no version " + (p_node.pinned ? "=" : "") +
                      target.text() +
                      (p_node.pinned ? "" : " or later on its " + p_node.line + " line") +
                      " (" + asked + ").");
        }
      }
    }
    if (p_node.chosen == chosen && p_node.commit == commit && p_node.manifest)
      return false;
    p_node.chosen = chosen;
    p_node.commit = commit;
    if (p_node.path.empty()) {
      p_node.root = fetch_package(p_node.git, commit, tree);
      p_node.tree = tree.empty() ? tree_hash(p_node.root) : tree;
    } else {
      p_node.root = p_node.path;
    }
    p_node.manifest = read_manifest(p_node.root, p_node.git.empty() ? p_node.path.string()
                                                                    : p_node.git);
    const auto &stated = p_node.manifest->package->package_version;
    if (p_node.chosen && stated != *p_node.chosen)
      throw error(p_node.git + " v" + p_node.chosen->text() + " says it is version " +
                  stated.text() + " in its ovis.toml: its tag and `version` must agree.");
    p_node.chosen = stated;
    p_node.name = p_node.manifest->package->name;
    if (stated.line() != p_node.line)
      throw error(p_node.name + " at " + commit.substr(0, 12) + " is " + stated.text() +
                  ", not on the " + p_node.line + " line it was asked for.");
    return true;
  }

public:
  resolver(const project &p_root, const resolve_options &p_options)
      : root(p_root), options(p_options) {}

  auto run() -> package_graph {
    // Rounds until nothing rises: each reads the requirements of the
    // versions chosen so far, raises minimums, and reads what rose.
    for (int round = 0;; ++round) {
      if (round > 1000)
        throw error("Choosing package versions didn't settle (a cycle?).");
      bool changed = false;
      std::set<std::pair<string_type, string_type>> reached;
      std::vector<std::pair<requirement, std::pair<string_type, string_type>>> pending;
      for (const auto &dependency : root.dependencies) {
        auto wanted = requirement_of(dependency, root.root, "ovis.toml");
        changed = add(wanted) || changed;
        pending.push_back({wanted, {wanted.identity, line_of(wanted)}});
      }
      for (std::size_t index = 0; index < pending.size(); ++index) {
        const auto key = pending[index].second;
        if (!reached.insert(key).second)
          continue;
        auto &entry = nodes.at(key);
        changed = choose(entry) || changed;
        const auto who = entry.name + " " + entry.chosen->text();
        for (const auto &dependency : entry.manifest->dependencies) {
          auto wanted = requirement_of(dependency, entry.root, who);
          if (wanted.exact)
            throw error(who + " pins " + dependency.name +
                        "; a package must give the oldest version it works with.");
          changed = add(wanted) || changed;
          pending.push_back({wanted, {wanted.identity, line_of(wanted)}});
        }
      }
      if (!changed)
        return graph(reached);
    }
  }

  auto graph(const std::set<std::pair<string_type, string_type>> &p_reached)
      -> package_graph {
    package_graph result;
    // Ids: name@line, and ~2, ~3 for another identity with the same.
    std::map<std::pair<string_type, string_type>, string_type> ids;
    std::map<string_type, int> taken;
    for (const auto &key : p_reached) {
      const auto &entry = nodes.at(key);
      auto id = entry.name + "@" + entry.line;
      if (const auto count = ++taken[id]; count > 1)
        id += "~" + std::to_string(count);
      ids[key] = id;
    }
    const auto id_of = [&](const package_dependency &p_dependency,
                           const std::filesystem::path &p_from) {
      const auto wanted = requirement_of(p_dependency, p_from, "");
      return ids.at({wanted.identity, line_of(wanted)});
    };
    for (const auto &key : p_reached) {
      const auto &entry = nodes.at(key);
      resolved_package package{.id = ids.at(key),
                               .name = entry.name,
                               .git = entry.git,
                               .path = entry.path,
                               .line = entry.line,
                               .package_version = *entry.chosen,
                               .commit = entry.commit,
                               .tree = entry.tree,
                               .root = entry.root,
                               .manifest = *entry.manifest,
                               .asked_by = entry.asked_by};
      for (const auto &dependency : entry.manifest->dependencies)
        package.dependencies[dependency.name] = id_of(dependency, entry.root);
      result.packages.push_back(std::move(package));
    }
    for (const auto &dependency : root.dependencies)
      result.roots[dependency.name] = id_of(dependency, root.root);
    std::ranges::sort(result.packages, {}, &resolved_package::id);
    return result;
  }
};

} // namespace

auto resolve_packages(const project &p_project, const resolve_options &p_options)
    -> package_graph {
  if (p_project.dependencies.empty())
    return {};
  return resolver(p_project, p_options).run();
}

auto lock_packages(const package_graph &p_graph) -> std::vector<locked_package> {
  std::vector<locked_package> result;
  for (const auto &package : p_graph.packages) {
    locked_package entry{.name = package.name,
                         .git = package.git,
                         .path = package.path.empty() ? string_type() : package.path.string(),
                         .line = package.line,
                         .version = package.package_version.text(),
                         .commit = package.commit,
                         .tree = package.tree};
    for (const auto &[name, id] : package.dependencies)
      entry.dependencies.push_back(id);
    std::ranges::sort(entry.dependencies);
    result.push_back(std::move(entry));
  }
  return result;
}

auto combined_c_dependencies(const project &p_root, const package_graph &p_graph)
    -> std::vector<c_dependency> {
  struct kept {
    c_dependency dependency;
    // "ovis.toml" or "csv 1.0.0": who asked, for a conflict's message.
    string_type asked_by;
  };
  std::vector<kept> result;
  const auto identity = [](const c_dependency &p_dependency) {
    switch (p_dependency.kind) {
    case c_dependency::kind_type::pkg_config:
      return "pkg-config:" + p_dependency.source.substr(0, p_dependency.source.find(' '));
    case c_dependency::kind_type::git:
      return "git:" + normalized_url(p_dependency.source);
    default:
      return "file:" + p_dependency.source;
    }
  };
  const auto override_tag = [&](const string_type &p_name) -> std::optional<string_type> {
    for (const auto &override : p_root.overrides)
      if (override.name == "c." + p_name && !override.tag.empty())
        return override.tag;
    return std::nullopt;
  };
  const auto add = [&](c_dependency p_dependency, const std::filesystem::path &p_from,
                       const string_type &p_asked_by) {
    if (p_dependency.kind == c_dependency::kind_type::path ||
        p_dependency.kind == c_dependency::kind_type::cmake)
      p_dependency.source = (p_from / p_dependency.source).lexically_normal().string();
    if (const auto tag = override_tag(p_dependency.name);
        tag && p_dependency.kind == c_dependency::kind_type::git) {
      p_dependency.tag = *tag;
      p_dependency.commit.clear();
    }
    for (auto &existing : result) {
      if (identity(existing.dependency) != identity(p_dependency))
        continue;
      auto &have = existing.dependency;
      if (have.kind == c_dependency::kind_type::git &&
          (have.tag != p_dependency.tag || have.commit != p_dependency.commit))
        throw error("Two versions of the C library " + p_dependency.source + ": " +
                    existing.asked_by + " wants " +
                    (have.commit.empty() ? have.tag : have.commit) + ", " + p_asked_by +
                    " wants " +
                    (p_dependency.commit.empty() ? p_dependency.tag : p_dependency.commit) +
                    ". A program has one of each C library: choose one with "
                    "[overrides] \"c." + have.name + "\" = { tag = \"…\" } in ovis.toml.");
      for (const auto &target : p_dependency.targets)
        if (std::ranges::find(have.targets, target) == have.targets.end())
          have.targets.push_back(target);
      return;
    }
    for (const auto &existing : result)
      if (existing.dependency.name == p_dependency.name)
        throw error("[c." + p_dependency.name + "] means " + existing.dependency.source +
                    " for " + existing.asked_by + " and " + p_dependency.source + " for " +
                    p_asked_by + "; one name can't be two C libraries in one program.");
    result.push_back({std::move(p_dependency), p_asked_by});
  };
  for (const auto &dependency : p_root.c_dependencies)
    add(dependency, std::filesystem::absolute(p_root.root), "ovis.toml");
  for (const auto &package : p_graph.packages)
    for (const auto &dependency : package.manifest.c_dependencies)
      add(dependency, package.root, package.name + " " + package.package_version.text());
  std::vector<c_dependency> combined;
  for (auto &each : result)
    combined.push_back(std::move(each.dependency));
  return combined;
}

auto describe_graph(const package_graph &p_graph) -> string_type {
  // How many lines each package name is on, to say where two copies are.
  std::map<string_type, std::vector<const resolved_package *>> by_name;
  for (const auto &package : p_graph.packages)
    by_name[package.name].push_back(&package);
  std::ostringstream out;
  std::set<string_type> shown;
  const std::function<void(const string_type &, const string_type &, int)> show =
      [&](const string_type &p_name, const string_type &p_id, int p_depth) {
        const auto *package = p_graph.find(p_id);
        out << string_type(static_cast<std::size_t>(p_depth) * 2, ' ') << p_name;
        if (p_name != package->name)
          out << " (" << package->name << ")";
        out << " " << package->package_version.text();
        out << (package->git.empty() ? " " + package->path.string() : " " + package->git);
        const auto &copies = by_name[package->name];
        if (copies.size() > 1) {
          out << " (" << copies.size() << " copies:";
          for (const auto *copy : copies)
            out << " " << copy->package_version.text();
          out << ")";
        }
        if (!shown.insert(p_id).second) {
          out << " (above)\n";
          return;
        }
        out << "\n";
        for (const auto &[name, id] : package->dependencies)
          show(name, id, p_depth + 1);
      };
  for (const auto &[name, id] : p_graph.roots)
    show(name, id, 0);
  return out.str();
}

} // namespace ovis::project
