#include <algorithm>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include <llvm/ADT/SmallString.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Program.h>

#include <ovis/project/lock.hpp>

namespace ovis::project {

namespace {

auto read_text(const std::filesystem::path &p_path) -> string_type {
  std::ifstream input(p_path);
  std::stringstream text;
  text << input.rdbuf();
  return text.str();
}

auto text_of(const nlohmann::json &p_object, const char *p_key) -> string_type {
  const auto found = p_object.find(p_key);
  return found != p_object.end() && found->is_string() ? found->get<string_type>()
                                                       : string_type();
}

} // namespace

auto project_lock::load(const std::filesystem::path &p_path) -> project_lock {
  project_lock result;
  std::error_code ignored;
  if (!std::filesystem::exists(p_path, ignored))
    return result;
  nlohmann::json json;
  try {
    json = nlohmann::json::parse(read_text(p_path));
  } catch (const nlohmann::json::exception &p_error) {
    throw error(p_path.string() + " is not a lockfile ovis can read (" +
                p_error.what() + "); delete it to make it again.");
  }
  if (const auto c = json.find("c"); c != json.end() && c->is_object())
    for (const auto &[name, entry] : c->items())
      result.c.push_back({name, text_of(entry, "git"), text_of(entry, "tag"),
                          text_of(entry, "commit")});
  if (const auto packages = json.find("packages");
      packages != json.end() && packages->is_array())
    for (const auto &entry : *packages) {
      locked_package package{.name = text_of(entry, "name"),
                             .git = text_of(entry, "git"),
                             .path = text_of(entry, "path"),
                             .line = text_of(entry, "line"),
                             .version = text_of(entry, "version"),
                             .commit = text_of(entry, "commit"),
                             .tree = text_of(entry, "tree")};
      if (const auto dependencies = entry.find("dependencies");
          dependencies != entry.end() && dependencies->is_array())
        for (const auto &dependency : *dependencies)
          if (dependency.is_string())
            package.dependencies.push_back(dependency.get<string_type>());
      result.packages.push_back(std::move(package));
    }
  return result;
}

auto project_lock::to_text() const -> string_type {
  nlohmann::ordered_json json;
  json["ovis_project_lock_version"] = 1;
  auto sorted_c = c;
  std::ranges::sort(sorted_c, {}, &locked_c_dependency::name);
  json["c"] = nlohmann::ordered_json::object();
  for (const auto &entry : sorted_c)
    json["c"][entry.name] = {{"git", entry.git}, {"tag", entry.tag}, {"commit", entry.commit}};
  auto sorted_packages = packages;
  std::ranges::sort(sorted_packages, [](const auto &p_left, const auto &p_right) {
    return std::tie(p_left.name, p_left.line, p_left.git) <
           std::tie(p_right.name, p_right.line, p_right.git);
  });
  json["packages"] = nlohmann::ordered_json::array();
  for (const auto &package : sorted_packages) {
    nlohmann::ordered_json entry = {{"name", package.name}};
    if (!package.git.empty())
      entry["git"] = package.git;
    if (!package.path.empty())
      entry["path"] = package.path;
    entry["line"] = package.line;
    entry["version"] = package.version;
    if (!package.commit.empty())
      entry["commit"] = package.commit;
    if (!package.tree.empty())
      entry["tree"] = package.tree;
    entry["dependencies"] = package.dependencies;
    json["packages"].push_back(std::move(entry));
  }
  return json.dump(2) + "\n";
}

auto project_lock::save(const std::filesystem::path &p_path) const -> void {
  const auto text = to_text();
  std::error_code ignored;
  if (std::filesystem::exists(p_path, ignored) && read_text(p_path) == text)
    return;
  // Replaced whole, so an interrupted write never leaves half a lockfile.
  const auto partial = p_path.string() + ".part";
  {
    std::ofstream output(partial);
    output << text;
    if (!output)
      throw error("Can't write " + partial + ".");
  }
  std::filesystem::rename(partial, p_path);
}

auto project_lock::find_c(string_view_type p_name) const
    -> const locked_c_dependency * {
  for (const auto &entry : c)
    if (entry.name == p_name)
      return &entry;
  return nullptr;
}

auto resolve_git_tag(const string_type &p_url, const string_type &p_tag)
    -> string_type {
  const auto git = llvm::sys::findProgramByName("git");
  if (!git)
    throw error("git is needed to fetch " + p_url + ".");
  llvm::SmallString<128> output_path;
  llvm::SmallString<128> error_path;
  if (llvm::sys::fs::createTemporaryFile("ovis-git", "out", output_path) ||
      llvm::sys::fs::createTemporaryFile("ovis-git", "err", error_path))
    throw error("Can't make a temporary file.");
  const auto tag = "refs/tags/" + p_tag;
  const auto peeled = tag + "^{}";
  const std::optional<llvm::StringRef> redirects[] = {
      llvm::StringRef(), llvm::StringRef(output_path), llvm::StringRef(error_path)};
  const auto status = llvm::sys::ExecuteAndWait(
      *git, {*git, "ls-remote", "--tags", p_url, tag, peeled}, std::nullopt, redirects);
  const auto output = read_text(output_path.str().str());
  auto problem = read_text(error_path.str().str());
  llvm::sys::fs::remove(output_path);
  llvm::sys::fs::remove(error_path);
  if (status != 0) {
    while (!problem.empty() && problem.back() == '\n')
      problem.pop_back();
    throw error("Can't list the tags of " + p_url + ": " + problem);
  }
  // An annotated tag's own object, then (^{}) the commit it names.
  string_type direct;
  std::istringstream lines(output);
  for (string_type line; std::getline(lines, line);) {
    const auto tab = line.find('\t');
    if (tab == string_type::npos)
      continue;
    const auto name = line.substr(tab + 1);
    if (name == peeled)
      return line.substr(0, tab);
    if (name == tag)
      direct = line.substr(0, tab);
  }
  if (direct.empty())
    throw error(p_url + " has no tag " + p_tag + ".");
  return direct;
}

auto lock_c_dependencies(const project &p_project, project_lock &p_lock,
                         const std::vector<string_type> &p_update)
    -> std::vector<string_type> {
  std::vector<string_type> messages;
  const bool everything = std::ranges::find(p_update, "*") != p_update.end();
  std::vector<locked_c_dependency> kept;
  for (const auto &dependency : p_project.c_dependencies) {
    if (dependency.kind != c_dependency::kind_type::git)
      continue;
    const auto asked = dependency.commit.empty() ? dependency.tag : dependency.commit;
    const auto *locked = p_lock.find_c(dependency.name);
    const bool update =
        everything || std::ranges::find(p_update, dependency.name) != p_update.end();
    if (locked != nullptr && locked->git == dependency.source && locked->tag == asked &&
        !update) {
      kept.push_back(*locked);
      continue;
    }
    // A commit is its own pin; a tag is looked up once and then kept, so a
    // tag moved upstream can't change the build until `ovis update`.
    const auto commit =
        dependency.commit.empty() ? resolve_git_tag(dependency.source, dependency.tag)
                                  : dependency.commit;
    if (locked != nullptr && locked->git == dependency.source && locked->tag == asked &&
        locked->commit != commit)
      messages.push_back("[c." + dependency.name + "]: " + asked + " moved from " +
                         locked->commit.substr(0, 12) + " to " + commit.substr(0, 12));
    else if (locked == nullptr || locked->commit != commit)
      messages.push_back("[c." + dependency.name + "]: " + asked + " is " +
                         commit.substr(0, 12));
    kept.push_back({dependency.name, dependency.source, asked, commit});
  }
  p_lock.c = std::move(kept);
  return messages;
}

} // namespace ovis::project
