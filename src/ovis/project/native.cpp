#include <algorithm>
#include <fstream>
#include <sstream>

#include <llvm/ADT/SmallString.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Program.h>

#include <ovis/project/native.hpp>

namespace ovis::project {

auto path_matches(string_view_type p_pattern, string_view_type p_path) -> bool {
  if (p_pattern.empty())
    return p_path.empty();
  if (p_pattern.starts_with("**")) {
    auto rest = p_pattern.substr(2);
    if (rest.empty())
      return true;
    if (rest.starts_with('/'))
      rest.remove_prefix(1);
    // `**/` matches no folder, or any number of them.
    if (path_matches(rest, p_path))
      return true;
    for (auto slash = p_path.find('/'); slash != string_view_type::npos;
         slash = p_path.find('/', slash + 1))
      if (path_matches(rest, p_path.substr(slash + 1)))
        return true;
    return false;
  }
  if (p_pattern.front() == '*') {
    for (std::size_t at = 0;; ++at) {
      if (path_matches(p_pattern.substr(1), p_path.substr(at)))
        return true;
      if (at == p_path.size() || p_path[at] == '/')
        return false;
    }
  }
  if (p_path.empty())
    return false;
  if (p_pattern.front() == '?' ? p_path.front() == '/'
                               : p_pattern.front() != p_path.front())
    return false;
  return path_matches(p_pattern.substr(1), p_path.substr(1));
}

auto c_source_files(const project &p_project)
    -> std::vector<std::filesystem::path> {
  std::vector<std::filesystem::path> result;
  for (const auto &pattern : p_project.c_sources) {
    std::size_t matched = 0;
    if (pattern.find_first_of("*?") == string_type::npos) {
      const auto file = p_project.root / pattern;
      if (!std::filesystem::is_regular_file(file))
        throw error(p_project.file.filename().string() + ": `c` names " + pattern +
                    ", which is not a file in " + p_project.root.string() + ".");
      result.push_back(file);
      continue;
    }
    std::error_code ignored;
    for (auto entry = std::filesystem::recursive_directory_iterator(
             p_project.root, std::filesystem::directory_options::skip_permission_denied,
             ignored);
         entry != std::filesystem::recursive_directory_iterator(); ++entry) {
      const auto name = entry->path().filename().string();
      // Not the build's own output, nor hidden folders (.git).
      if (entry->is_directory() &&
          (name.starts_with('.') ||
           (name == "build" && entry->path().parent_path() == p_project.root))) {
        entry.disable_recursion_pending();
        continue;
      }
      if (!entry->is_regular_file())
        continue;
      const auto relative =
          entry->path().lexically_relative(p_project.root).generic_string();
      if (path_matches(pattern, relative)) {
        result.push_back(entry->path());
        ++matched;
      }
    }
    if (matched == 0)
      throw error(p_project.file.filename().string() + ": `c` pattern " + pattern +
                  " matches no file in " + p_project.root.string() + ".");
  }
  std::ranges::sort(result);
  const auto repeated = std::ranges::unique(result);
  result.erase(repeated.begin(), repeated.end());
  return result;
}

namespace {

auto read_text(const std::filesystem::path &p_path) -> string_type {
  std::ifstream input(p_path);
  std::stringstream text;
  text << input.rdbuf();
  return text.str();
}

auto split_words(string_view_type p_text) -> std::vector<string_type> {
  std::vector<string_type> words;
  std::istringstream input{string_type(p_text)};
  for (string_type word; input >> word;)
    words.push_back(word);
  return words;
}

auto find_c_compiler() -> string_type {
  for (const auto *const name : {"cc", "clang", "gcc"})
    if (llvm::sys::findProgramByName(name))
      return name;
  throw error("A C compiler (cc, clang or gcc) is needed for the project's C files.");
}

auto modified(const std::filesystem::path &p_path)
    -> std::optional<std::filesystem::file_time_type> {
  std::error_code problem;
  const auto time = std::filesystem::last_write_time(p_path, problem);
  if (problem)
    return std::nullopt;
  return time;
}

// Whether `p_object` is older than its source or any header its `.d` file
// (written by -MMD) lists, or the flags it was made with changed.
auto out_of_date(const std::filesystem::path &p_object,
                 const std::filesystem::path &p_depfile,
                 const std::filesystem::path &p_flags_file,
                 const string_type &p_flags) -> bool {
  const auto made = modified(p_object);
  if (!made || read_text(p_flags_file) != p_flags)
    return true;
  auto text = read_text(p_depfile);
  if (text.empty())
    return true;
  // `object.o: a.c b.h \` with line continuations; names after the colon.
  std::erase(text, '\\');
  const auto colon = text.find(": ");
  if (colon == string_type::npos)
    return true;
  for (const auto &input : split_words(string_view_type(text).substr(colon + 1))) {
    const auto time = modified(input);
    if (!time || *time > *made)
      return true;
  }
  return false;
}

} // namespace

auto run_command(const string_type &p_program,
                 const std::vector<string_type> &p_arguments) -> command_result {
  command_result result;
  const auto found = llvm::sys::findProgramByName(p_program);
  if (!found) {
    result.errors = p_program + " is not installed";
    return result;
  }
  llvm::SmallString<128> output_path;
  llvm::SmallString<128> error_path;
  if (llvm::sys::fs::createTemporaryFile("ovis-run", "out", output_path) ||
      llvm::sys::fs::createTemporaryFile("ovis-run", "err", error_path))
    throw error("Can't make a temporary file.");
  std::vector<llvm::StringRef> arguments = {*found};
  for (const auto &argument : p_arguments)
    arguments.push_back(argument);
  const std::optional<llvm::StringRef> redirects[] = {
      llvm::StringRef(), llvm::StringRef(output_path), llvm::StringRef(error_path)};
  result.status = llvm::sys::ExecuteAndWait(*found, arguments, std::nullopt, redirects);
  result.output = read_text(output_path.str().str());
  result.errors = read_text(error_path.str().str());
  llvm::sys::fs::remove(output_path);
  llvm::sys::fs::remove(error_path);
  return result;
}

auto pkg_config_flags(const c_dependency &p_dependency, bool p_libraries)
    -> std::vector<string_type> {
  const auto result = run_command("pkg-config", {p_libraries ? "--libs" : "--cflags",
                                         p_dependency.source});
  if (result.status != 0) {
    auto message = result.errors;
    while (!message.empty() && message.back() == '\n')
      message.pop_back();
    throw error("[c." + p_dependency.name + "]: pkg-config can't find `" +
                p_dependency.source + "`" +
                (message.empty() ? string_type(".") : ": " + message));
  }
  return split_words(result.output);
}

auto needs_cmake(const project &p_project) -> bool {
  return std::ranges::any_of(p_project.c_dependencies, [](const auto &p_dependency) {
    return p_dependency.kind != c_dependency::kind_type::pkg_config;
  });
}

auto build_native_parts(const project &p_project,
                        const std::filesystem::path &p_build_directory,
                        bool p_compile) -> native_parts {
  native_parts parts;
  std::vector<string_type> c_flags;
  for (const auto &directory : p_project.include) {
    parts.include_directories.push_back(p_project.root / directory);
    c_flags.push_back("-I" + (p_project.root / directory).string());
  }
  for (const auto &define : p_project.defines) {
    parts.compiler_flags.push_back("-D" + define);
    c_flags.push_back("-D" + define);
  }
  std::vector<string_type> libraries;
  for (const auto &dependency : p_project.c_dependencies) {
    if (dependency.kind != c_dependency::kind_type::pkg_config)
      continue;
    for (const auto &flag : pkg_config_flags(dependency, false)) {
      if (flag.starts_with("-I") && flag.size() > 2)
        parts.include_directories.emplace_back(flag.substr(2));
      else
        parts.compiler_flags.push_back(flag);
      c_flags.push_back(flag);
    }
    if (p_compile)
      for (const auto &flag : pkg_config_flags(dependency, true))
        libraries.push_back(flag);
  }

  const auto sources = c_source_files(p_project);
  if (p_compile && !sources.empty()) {
    const auto compiler = find_c_compiler();
    const auto objects = p_build_directory / "c";
    std::filesystem::create_directories(objects);
    string_type flags_text;
    for (const auto &flag : c_flags)
      flags_text += flag + "\n";
    for (const auto &source : sources) {
      // build/c/<path with / as _>.o, so files of one name in two folders
      // don't collide.
      auto stem = source.lexically_relative(p_project.root).generic_string();
      std::ranges::replace(stem, '/', '_');
      const auto object = objects / (stem + ".o");
      const auto depfile = objects / (stem + ".d");
      const auto flags_file = objects / (stem + ".flags");
      if (out_of_date(object, depfile, flags_file, flags_text)) {
        std::vector<string_type> arguments = {"-c", "-O2", "-fPIC", "-MMD", "-MF",
                                              depfile.string()};
        arguments.insert(arguments.end(), c_flags.begin(), c_flags.end());
        arguments.insert(arguments.end(), {source.string(), "-o", object.string()});
        const auto result = run_command(compiler, arguments);
        if (result.status != 0)
          throw error("Compiling " +
                      source.lexically_relative(p_project.root).string() +
                      " failed:\n" + result.errors);
        std::ofstream(flags_file) << flags_text;
      }
      parts.link_inputs.push_back(object.string());
    }
  }
  parts.link_inputs.insert(parts.link_inputs.end(), libraries.begin(),
                           libraries.end());
  return parts;
}

} // namespace ovis::project
