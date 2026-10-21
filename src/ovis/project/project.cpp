#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

#include <ovis/project/project.hpp>
#include <ovis/project/toml.hpp>

namespace ovis::project {

auto version::text() const -> string_type {
  return std::to_string(major) + "." + std::to_string(minor) + "." +
         std::to_string(patch);
}

auto version::line() const -> string_type {
  return major == 0 ? "0." + std::to_string(minor) : std::to_string(major);
}

auto parse_version(string_view_type p_text) -> std::optional<version> {
  if (p_text.starts_with('v'))
    p_text.remove_prefix(1);
  int64_type parts[3] = {0, 0, 0};
  std::size_t count = 0;
  while (count < 3) {
    std::size_t length = 0;
    while (length < p_text.size() &&
           std::isdigit(static_cast<unsigned char>(p_text[length])))
      ++length;
    if (length == 0 || length > 9)
      return std::nullopt;
    parts[count++] = std::stoll(string_type(p_text.substr(0, length)));
    p_text.remove_prefix(length);
    if (p_text.empty())
      break;
    if (p_text.front() != '.')
      return std::nullopt;
    p_text.remove_prefix(1);
  }
  if (!p_text.empty())
    return std::nullopt;
  return version{parts[0], parts[1], parts[2]};
}

auto project::program_name() const -> string_type {
  if (!name.empty())
    return name;
  return std::filesystem::absolute(root).lexically_normal().filename().string();
}

namespace {

class checker {
  const std::filesystem::path &file;
  string_type file_name;

public:
  explicit checker(const std::filesystem::path &p_file)
      : file(p_file), file_name(p_file.filename().string()) {}

  [[noreturn]] auto fail(std::size_t p_line, const string_type &p_message) const
      -> void {
    throw error(file_name + ", line " + std::to_string(p_line) + ": " + p_message);
  }

  auto expect(const toml_value &p_value, toml_value::kind_type p_kind,
              const string_type &p_what) const -> void {
    if (p_value.kind != p_kind) {
      toml_value wanted;
      wanted.kind = p_kind;
      fail(p_value.line, p_what + " must be " + string_type(wanted.kind_name()) +
                             ", not " + string_type(p_value.kind_name()));
    }
  }

  auto text(const toml_value &p_value, const string_type &p_what) const
      -> string_type {
    expect(p_value, toml_value::kind_type::string, p_what);
    return p_value.text;
  }

  auto texts(const toml_value &p_value, const string_type &p_what) const
      -> std::vector<string_type> {
    expect(p_value, toml_value::kind_type::array, p_what);
    std::vector<string_type> result;
    for (const auto &item : p_value.items)
      result.push_back(text(item, "each of " + p_what));
    return result;
  }

  // The line a table's keys start at, for messages about the table.
  static auto line_of(const toml_value &p_table) -> std::size_t {
    if (p_table.line != 0)
      return p_table.line;
    return p_table.entries.empty() ? 1 : p_table.entries.front().value.line;
  }

  auto only_keys(const toml_value &p_table, const string_type &p_table_name,
                 std::initializer_list<string_view_type> p_keys) const -> void {
    for (const auto &entry : p_table.entries)
      if (std::ranges::find(p_keys, entry.key) == p_keys.end()) {
        string_type known;
        for (const auto key : p_keys)
          known += (known.empty() ? "" : ", ") + string_type(key);
        fail(entry.value.line, "unknown key `" + entry.key + "` in " +
                                   p_table_name + " (it takes " + known + ")");
      }
  }

  auto read_project(const toml_value &p_table, project &p_project) const -> void {
    expect(p_table, toml_value::kind_type::table, "[project]");
    only_keys(p_table, "[project]", {"name", "toolchain"});
    if (const auto *name = p_table.find("name")) {
      p_project.name = text(*name, "`name`");
      if (p_project.name.empty() || p_project.name.find('/') != string_type::npos)
        fail(name->line, "`name` must be a file name");
    }
    if (const auto *toolchain = p_table.find("toolchain"))
      p_project.toolchain = text(*toolchain, "`toolchain`");
  }

  auto read_build(const toml_value &p_table, project &p_project) const -> void {
    expect(p_table, toml_value::kind_type::table, "[build]");
    only_keys(p_table, "[build]", {"include", "defines", "phrases", "c"});
    if (const auto *value = p_table.find("include"))
      p_project.include = texts(*value, "`include`");
    if (const auto *value = p_table.find("defines"))
      p_project.defines = texts(*value, "`defines`");
    if (const auto *value = p_table.find("phrases"))
      p_project.phrases = texts(*value, "`phrases`");
    if (const auto *value = p_table.find("c"))
      p_project.c_sources = texts(*value, "`c`");
  }

  auto read_c(const toml_value &p_table, project &p_project) const -> void {
    expect(p_table, toml_value::kind_type::table, "[c]");
    for (const auto &[name, table] : p_table.entries) {
      const auto table_name = "[c." + name + "]";
      if (table.kind != toml_value::kind_type::table)
        fail(table.line, "`c." + name + "` must be a table: write [c." + name +
                             "] with pkg-config, git, path or cmake");
      only_keys(table, table_name,
                {"pkg-config", "git", "path", "cmake", "tag", "commit", "targets"});
      c_dependency dependency{.name = name, .line = line_of(table)};
      std::size_t sources = 0;
      using kind = c_dependency::kind_type;
      for (const auto &[key, value_kind] :
           {std::pair{"pkg-config", kind::pkg_config}, std::pair{"git", kind::git},
            std::pair{"path", kind::path}, std::pair{"cmake", kind::cmake}})
        if (const auto *value = table.find(key)) {
          ++sources;
          dependency.kind = value_kind;
          dependency.source = text(*value, "`" + string_type(key) + "`");
        }
      if (sources != 1)
        fail(dependency.line, table_name + " needs exactly one of pkg-config, "
                                           "git, path or cmake");
      if (const auto *tag = table.find("tag"))
        dependency.tag = text(*tag, "`tag`");
      if (const auto *commit = table.find("commit"))
        dependency.commit = text(*commit, "`commit`");
      if (const auto *targets = table.find("targets"))
        dependency.targets = texts(*targets, "`targets`");
      if (dependency.kind == kind::git && dependency.tag.empty() &&
          dependency.commit.empty())
        fail(dependency.line, table_name + " is a git dependency: give its `tag` "
                                           "(or `commit`), so builds stay the same");
      if (dependency.kind != kind::git &&
          (!dependency.tag.empty() || !dependency.commit.empty()))
        fail(dependency.line, "`tag` and `commit` are for git dependencies");
      if (dependency.kind == kind::pkg_config && !dependency.targets.empty())
        fail(dependency.line, "a pkg-config dependency has no `targets`");
      if (dependency.kind != kind::pkg_config && dependency.targets.empty())
        fail(dependency.line, table_name + " needs `targets`: the CMake targets "
                                           "the program links from it");
      p_project.c_dependencies.push_back(std::move(dependency));
    }
  }

  auto read_package(const toml_value &p_table, project &p_project) const -> void {
    expect(p_table, toml_value::kind_type::table, "[package]");
    only_keys(p_table, "[package]", {"name", "version", "ovis"});
    package_info package;
    const auto *name = p_table.find("name");
    const auto *package_version = p_table.find("version");
    if (name == nullptr || package_version == nullptr)
      fail(line_of(p_table), "[package] needs a `name` and a `version`");
    package.name = text(*name, "`name`");
    const auto parsed = parse_version(text(*package_version, "`version`"));
    if (!parsed)
      fail(package_version->line, "`version` must be a version such as 1.4.0");
    package.package_version = *parsed;
    if (const auto *ovis = p_table.find("ovis")) {
      package.ovis = parse_version(text(*ovis, "`ovis`"));
      if (!package.ovis)
        fail(ovis->line, "`ovis` must be a version such as 0.1");
    }
    p_project.package = std::move(package);
  }

  auto read_dependency(const string_type &p_name, const toml_value &p_value,
                       const string_type &p_table_name) const
      -> package_dependency {
    package_dependency dependency{.name = p_name, .line = p_value.line};
    if (p_value.kind != toml_value::kind_type::table)
      fail(p_value.line, "`" + p_name + "` must be a table, as { git = \"URL\", "
                                        "version = \"1.2\" }");
    only_keys(p_value, p_table_name + "'s `" + p_name + "`",
              {"git", "path", "version", "commit", "tag"});
    if (const auto *git = p_value.find("git"))
      dependency.git = text(*git, "`git`");
    if (const auto *path = p_value.find("path"))
      dependency.path = text(*path, "`path`");
    if (dependency.git.empty() == dependency.path.empty() &&
        p_table_name != "[overrides]")
      fail(p_value.line, "`" + p_name + "` needs one of `git` or `path`");
    if (const auto *value = p_value.find("version")) {
      auto written = text(*value, "`version`");
      if (written.starts_with('=')) {
        dependency.exact = true;
        written.erase(0, 1);
      }
      dependency.minimum = parse_version(written);
      if (!dependency.minimum)
        fail(value->line, "`version` must be a version such as 1.2 or =1.2.3");
    }
    // A tag is an exact version, as the proposal's overrides write it.
    if (const auto *tag = p_value.find("tag")) {
      dependency.tag = text(*tag, "`tag`");
      dependency.exact = true;
      if (p_name.starts_with("c."))
        return dependency;
      dependency.minimum = parse_version(dependency.tag);
      if (!dependency.minimum)
        fail(tag->line, "`tag` must be a version tag such as v1.4.1");
      dependency.exact = true;
    }
    if (const auto *commit = p_value.find("commit")) {
      dependency.commit = text(*commit, "`commit`");
      dependency.exact = true;
    }
    if (!dependency.git.empty() && !dependency.minimum && dependency.commit.empty())
      fail(p_value.line, "`" + p_name + "` needs a `version` (or, in a project "
                                        "that is not a package, a `commit`)");
    return dependency;
  }

  auto read_dependencies(const toml_value &p_table, const string_type &p_name,
                         std::vector<package_dependency> &p_into) const -> void {
    expect(p_table, toml_value::kind_type::table, p_name);
    for (const auto &[name, value] : p_table.entries)
      p_into.push_back(read_dependency(name, value, p_name));
  }

  auto run(const toml_value &p_root) const -> project {
    project result;
    result.file = file;
    result.root = file.parent_path().empty() ? std::filesystem::path(".")
                                             : file.parent_path();
    for (const auto &[key, value] : p_root.entries) {
      if (key == "project")
        read_project(value, result);
      else if (key == "build")
        read_build(value, result);
      else if (key == "c")
        read_c(value, result);
      else if (key == "package")
        read_package(value, result);
      else if (key == "dependencies")
        read_dependencies(value, "[dependencies]", result.dependencies);
      else if (key == "overrides")
        read_dependencies(value, "[overrides]", result.overrides);
      else
        fail(value.line == 0 ? line_of(value) : value.line,
             "unknown table [" + key +
                 "] (ovis.toml has [project], [build], [c.NAME], [package], "
                 "[dependencies] and [overrides])");
    }
    // A package that pinned would force its exact choice on everyone above
    // it (packages.md §2).
    if (result.package) {
      for (const auto &dependency : result.dependencies)
        if (dependency.exact)
          fail(dependency.line, "a package can't pin `" + dependency.name +
                                    "` to one version or commit: give the "
                                    "oldest version it works with");
      if (!result.overrides.empty())
        fail(result.overrides.front().line,
             "[overrides] are for the project at the top, not a package");
    }
    for (std::size_t index = 0; index < result.dependencies.size(); ++index)
      for (std::size_t other = 0; other < index; ++other)
        if (result.dependencies[index].name == result.dependencies[other].name)
          fail(result.dependencies[index].line,
               "`" + result.dependencies[index].name + "` is given twice");
    return result;
  }
};

} // namespace

auto parse_project(string_view_type p_text, const std::filesystem::path &p_file)
    -> project {
  const auto root = parse_toml(p_text, p_file.filename().string());
  return checker(p_file).run(root);
}

auto load_project(const std::filesystem::path &p_file) -> project {
  std::ifstream input(p_file);
  if (!input)
    throw error("Can't read " + p_file.string() + ".");
  std::stringstream text;
  text << input.rdbuf();
  return parse_project(text.str(), p_file);
}

auto find_project(const std::filesystem::path &p_start)
    -> std::optional<std::filesystem::path> {
  std::error_code ignored;
  auto directory = std::filesystem::absolute(p_start, ignored).lexically_normal();
  if (!std::filesystem::is_directory(directory, ignored))
    directory = directory.parent_path();
  for (;;) {
    const auto candidate = directory / project_file_name;
    if (std::filesystem::is_regular_file(candidate, ignored))
      return candidate;
    if (!directory.has_parent_path() || directory.parent_path() == directory)
      return std::nullopt;
    directory = directory.parent_path();
  }
}

} // namespace ovis::project
