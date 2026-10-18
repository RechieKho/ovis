#ifdef __linux__
#include <cerrno>
#include <csignal>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>

#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Process.h>
#include <llvm/Support/Program.h>

#include <ovis/codegen/llvm_codegen.hpp>
#include <ovis/compiler.hpp>
#include <ovis/phrase/front_end.hpp>

namespace {

constexpr const char *usage = R"usage(Ovis: programs written in plain English, compiled with LLVM.

Usage: ovis <command> <file or folder> [options]
       ovis model <path | pull | embed MODEL -o OUTPUT>

Commands:
  run        translate, compile and run the program
  build      translate and compile to an executable
  translate  show every paragraph's steps, assumptions and Ovis Core
  explain    show every paragraph's steps and assumptions
  ir         print the generated LLVM IR

  model path   show which built-in model would be used
  model pull   download the default model into the cache directory
  model embed  write a copy of ovis with MODEL built into the executable

Options:
  -o <path>           output executable for `build` (default: the program's name)
  -I <dir>, -L <dir>  where to look for C headers and libraries of (use-c ...)
  --offline           never call the model; use only the lockfile
  --retranslate       ignore the lockfile and translate everything again
  --attempts <n>      translation attempts per paragraph (default: 3)
  --keep-source       don't write the detail into the source file
  --phrases           read the program as phrases (the new front end, in
                      progress; becomes the only one)
  --model-budget <s>  with --phrases: seconds the model may spend on
                      questions (default: 10)
  --quiet             don't print progress
  --verbose           show the code behind failures, and llama.cpp's log

Built-in model (the default):
  --model-path <gguf> model file to use (default: $OVIS_MODEL_PATH, the model
                      embedded in ovis, or the default model next to ovis or in
                      the cache directory)
  --cpu               run the model on the CPU even if there is a GPU
  --threads <n>       CPU threads (default: the number of performance cores)

Model server instead of the built-in model:
  --endpoint <url>    OpenAI-compatible API, e.g. http://127.0.0.1:8080/v1
                      (default: $OVIS_ENDPOINT if set)
  --model <name>      model name to request (default: $OVIS_MODEL or "local")
  --api-key <key>     sent as a bearer token (default: $OVIS_API_KEY)
  --no-grammar        don't send the GBNF grammar (for servers that reject it)

A program is one .ovis file, or a folder with a main.ovis: then every .ovis
file in the folder and its subfolders is a module of the program. main.ovis
says what the program does; the other files only define things.

Ovis first details every paragraph: it writes concrete numbered steps and
the assumptions it made underneath your own words, in the source file. Edit
those steps to change what a paragraph does. Paragraphs that are too vague or
not about the program stop the build with an explanation and a suggestion.

Everything the model produced is kept in <file>.lock next to the program.
Commit it: it makes builds reproducible and lets them work without the model.
)usage";

struct arguments {
  std::string command;
  std::vector<std::string> positional;
  std::filesystem::path output;
  ovis::english::endpoint_options endpoint;
  bool use_endpoint = false;
  std::optional<std::filesystem::path> model_path;
  int threads = 0;
  bool use_gpu = true;
  bool verbose = false;
  ovis::compile_options compile;
  bool offline = false;
  bool quiet = false;
  bool keep_source = false;
  bool phrases = false;
  double model_budget = 10;
};

auto environment(const char *p_name, std::string p_fallback) -> std::string {
  const char *const value = std::getenv(p_name);
  return value != nullptr && *value != '\0' ? value : std::move(p_fallback);
}

auto parse_arguments(int p_count, char **p_values) -> arguments {
  if (p_count < 3)
    throw ovis::error(usage);

  arguments result;
  result.command = p_values[1];
  const auto endpoint = environment("OVIS_ENDPOINT", "");
  result.use_endpoint = !endpoint.empty();
  if (result.use_endpoint)
    result.endpoint.endpoint = endpoint;
  result.endpoint.model = environment("OVIS_MODEL", result.endpoint.model);
  result.endpoint.api_key = environment("OVIS_API_KEY", "");

  for (int index = 2; index < p_count; ++index) {
    const std::string option = p_values[index];
    const auto value = [&]() -> std::string {
      if (index + 1 >= p_count)
        throw ovis::error(option + " needs a value.");
      return p_values[++index];
    };
    if (!option.starts_with("-"))
      result.positional.push_back(option);
    else if (option == "-o")
      result.output = value();
    else if (option == "--endpoint") {
      result.endpoint.endpoint = value();
      result.use_endpoint = true;
    } else if (option == "--model")
      result.endpoint.model = value();
    else if (option == "--api-key")
      result.endpoint.api_key = value();
    else if (option == "--no-grammar")
      result.endpoint.use_grammar = false;
    else if (option == "--model-path")
      result.model_path = value();
    else if (option == "-I")
      result.compile.c_imports.include_directories.emplace_back(value());
    else if (option == "-L")
      result.compile.c_imports.library_directories.emplace_back(value());
    else if (option == "--threads")
      result.threads = std::stoi(value());
    else if (option == "--cpu")
      result.use_gpu = false;
    else if (option == "--verbose")
      result.verbose = true;
    else if (option == "--offline")
      result.offline = true;
    else if (option == "--retranslate")
      result.compile.retranslate = true;
    else if (option == "--attempts")
      result.compile.max_attempts = std::stoi(value());
    else if (option == "--quiet")
      result.quiet = true;
    else if (option == "--keep-source")
      result.keep_source = true;
    else if (option == "--phrases")
      result.phrases = true;
    else if (option == "--model-budget")
      result.model_budget = std::stod(value());
    else
      throw ovis::error("Unknown option " + option + ".\n\n" + usage);
  }
  if (result.positional.empty())
    throw ovis::error(usage);
  return result;
}

auto executable_path(const char *p_argument_zero) -> std::filesystem::path {
  static int anchor;
  return llvm::sys::fs::getMainExecutable(p_argument_zero, &anchor);
}

// Stands in for the built-in model when no weights are installed, so that
// building from the lockfile still works and translating explains what to do.
class missing_model final : public ovis::english::language_model {
public:
  auto name() const -> std::string override { return "none"; }
  auto chat(const std::vector<ovis::english::chat_message> &,
            std::string_view) -> std::string override {
    throw ovis::error(
        "No model is installed, so new paragraphs cannot be translated.\n"
        "Run `ovis model pull` to download the default model (" +
        std::string(ovis::english::default_model_file_name()) +
        "), pass --model-path <file.gguf>, or use --endpoint <url> for a "
        "model server.");
  }
};

// Runs the built program and gives back its exit status. On Linux the
// program dies with ovis: killed by a timeout, ovis would otherwise leave a
// program that loops forever running on its own.
auto run_program(const std::string &p_path) -> int {
#ifdef __linux__
  const auto parent = getpid();
  const auto child = fork();
  if (child == 0) {
    prctl(PR_SET_PDEATHSIG, SIGKILL);
    if (getppid() != parent)
      _exit(127);
    execl(p_path.c_str(), p_path.c_str(), static_cast<char *>(nullptr));
    _exit(127);
  }
  if (child > 0) {
    int status = 0;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
  }
#endif
  return llvm::sys::ExecuteAndWait(p_path, {p_path});
}

auto progress_log(const arguments &p_arguments)
    -> std::function<void(ovis::string_view_type)> {
  if (p_arguments.quiet)
    return {};
  return [](ovis::string_view_type p_message) {
    std::cerr << "ovis: " << p_message << '\n';
  };
}

auto make_model(const arguments &p_arguments, const char *p_argument_zero)
    -> std::unique_ptr<ovis::english::language_model> {
  if (p_arguments.offline)
    return nullptr;
  if (p_arguments.use_endpoint)
    return std::make_unique<ovis::english::endpoint_model>(
        p_arguments.endpoint);
  if (!ovis::english::has_builtin_model_support())
    throw ovis::error("This build of Ovis has no built-in model; use "
                      "--endpoint <url> or --offline.");
  const auto location = ovis::english::find_builtin_model(
      p_arguments.model_path, executable_path(p_argument_zero));
  if (!location)
    return std::make_unique<missing_model>();
  return std::make_unique<ovis::english::builtin_model>(
      ovis::english::builtin_options{.location = *location,
                                     .use_gpu = p_arguments.use_gpu,
                                     .threads = p_arguments.threads,
                                     .verbose = p_arguments.verbose,
                                     .log = progress_log(p_arguments)});
}

auto run_model_command(const arguments &p_arguments,
                       const char *p_argument_zero) -> int {
  const auto &subcommand = p_arguments.positional[0];
  const auto executable = executable_path(p_argument_zero);

  if (subcommand == "path") {
    const auto location =
        ovis::english::find_builtin_model(p_arguments.model_path, executable);
    if (!location) {
      std::cout << "No model found. Run `ovis model pull`, or put "
                << ovis::english::default_model_file_name() << " in "
                << ovis::english::model_cache_directory().string() << ".\n";
      return 1;
    }
    std::cout << location->file.string();
    if (location->is_embedded)
      std::cout << " (embedded, " << location->size << " bytes at offset "
                << location->offset << ")";
    std::cout << '\n';
    return 0;
  }

  if (subcommand == "pull") {
    const auto directory = ovis::english::model_cache_directory();
    std::filesystem::create_directories(directory);
    const auto target = directory / ovis::english::default_model_file_name();
    const auto partial = target.string() + ".part";
    const std::string url(ovis::english::default_model_url());
    std::cerr << "ovis: downloading " << url << "\n      to " << target.string()
              << '\n';
    // curl and wget handle proxies, redirects and resuming better than an
    // embedded HTTP client would.
    int status = -1;
    if (auto curl = llvm::sys::findProgramByName("curl")) {
      status = llvm::sys::ExecuteAndWait(
          *curl, {*curl, "-fL", "-C", "-", "--progress-bar", "-o", partial, url});
    } else if (auto wget = llvm::sys::findProgramByName("wget")) {
      status =
          llvm::sys::ExecuteAndWait(*wget, {*wget, "-c", "-O", partial, url});
    } else {
      throw ovis::error("Downloading needs curl or wget. Download " + url +
                        " yourself and put it in " + directory.string() + ".");
    }
    if (status != 0)
      throw ovis::error("The download failed.");
    std::filesystem::rename(partial, target);
    std::cerr << "ovis: done.\n";
    return 0;
  }

  if (subcommand == "embed") {
    if (p_arguments.positional.size() != 2 || p_arguments.output.empty())
      throw ovis::error("Usage: ovis model embed MODEL.gguf -o OUTPUT");
    ovis::english::embed_model(executable, p_arguments.positional[1],
                               p_arguments.output);
    return 0;
  }

  throw ovis::error("Unknown model command " + subcommand + ".\n\n" + usage);
}

auto print_translation(const ovis::compilation &p_compilation,
                       bool p_with_core) -> void {
  std::string file_name;
  for (const auto &paragraph : p_compilation.paragraphs) {
    if (paragraph.file_name != file_name) {
      file_name = paragraph.file_name;
      std::cout << "=== " << file_name << "\n\n";
    }
    std::cout << ovis::english::render_detailed(
                     paragraph.intent, paragraph.detail.steps,
                     paragraph.detail.assumptions,
                     paragraph.compiler_assumptions)
              << '\n';
    if (paragraph.detail.status ==
        ovis::english::detail_result::status_type::note)
      std::cout << "    (only describes the program)\n";
    if (p_with_core) {
      std::stringstream core(paragraph.core);
      for (std::string line; std::getline(core, line);)
        std::cout << "    > " << line << '\n';
    }
    std::cout << '\n';
  }
}

// Replaces the file only if needed, and never half-written.
auto write_source(const std::filesystem::path &p_path, std::string_view p_text)
    -> void {
  auto temporary = p_path;
  temporary += ".tmp";
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    output << p_text;
    if (!output)
      throw ovis::error("Cannot write " + temporary.string() + ".");
  }
  std::filesystem::rename(temporary, p_path);
}

auto generate(const arguments &p_arguments, const ovis::core::program &p_program,
              const std::string &program_name,
              const std::filesystem::path &p_source) -> int;

// `--phrases`: the program through the phrase front end, with where the
// time went.
auto run_phrases(const arguments &p_arguments, const char *p_argument_zero)
    -> int {
  const std::filesystem::path program_path = p_arguments.positional[0];
  const auto files = ovis::phrase::load_phrase_program(program_path);
  const auto model = make_model(p_arguments, p_argument_zero);
  const auto start = std::chrono::steady_clock::now();
  auto result = ovis::phrase::compile_phrase_program(
      files, model.get(),
      {.model_budget_seconds = p_arguments.model_budget,
       .log = progress_log(p_arguments),
       .c_imports = p_arguments.compile.c_imports});
  for (std::size_t index = 0; index < files.size(); ++index) {
    const auto &file = files[index];
    // A failed build keeps the old readings unless the model made new ones.
    if (result.locks[index] != file.lock &&
        (result.succeeded() || result.timing.by_model > 0))
      result.locks[index].save(ovis::phrase::lockfile::path_for(file.path));
    if (!p_arguments.keep_source && result.sources[index] != file.text) {
      write_source(file.path, result.sources[index]);
      if (!p_arguments.quiet)
        std::cerr << "ovis: wrote the readings into " << file.path.string()
                  << '\n';
    }
  }
  for (const auto &warning : result.warnings)
    std::cerr << "ovis: " << warning << '\n';
  const auto &timing = result.timing;
  if (!p_arguments.quiet)
    std::cerr << "ovis: " << timing.lines << " lines: read in "
              << int(timing.lex_ms + timing.parse_ms) << " ms, "
              << timing.questions << " unclear (" << timing.by_rule
              << " by rules, " << timing.by_ranking << " by ranking, "
              << timing.by_lockfile << " from the lockfile), " << timing.asked
              << " asked of the model in " << int(timing.model_ms) << " ms"
              << (timing.asked > 0 ? " (first, with loading: " +
                                         std::to_string(int(timing.first_question_ms)) +
                                         " ms)"
                                   : "")
              << ", checked in " << int(timing.check_ms) << " ms\n";
  if (!result.succeeded()) {
    // Problems name the file only in a program of several.
    const auto single =
        files[0].name.empty() ? program_path.string() + ", " : std::string();
    for (const auto &problem : result.problems)
      std::cerr << single << problem << '\n';
    return 1;
  }
  const auto program_name =
      files.size() > 1 || files[0].module == "main"
          ? std::filesystem::absolute(files.back().path)
                .parent_path()
                .filename()
                .string()
          : files[0].path.stem().string();
  const auto status = generate(p_arguments, result.program, program_name,
                               files.back().path);
  if (!p_arguments.quiet && p_arguments.command != "run")
    std::cerr << "ovis: done in "
              << int(std::chrono::duration<double, std::milli>(
                         std::chrono::steady_clock::now() - start)
                         .count())
              << " ms\n";
  return status;
}

auto run(const arguments &p_arguments, const char *p_argument_zero) -> int {
  const auto &command = p_arguments.command;
  if (command == "model")
    return run_model_command(p_arguments, p_argument_zero);
  if (command != "run" && command != "build" && command != "translate" &&
      command != "explain" && command != "ir")
    throw ovis::error("Unknown command " + command + ".\n\n" + usage);
  if (p_arguments.phrases)
    return run_phrases(p_arguments, p_argument_zero);

  const std::filesystem::path program_path = p_arguments.positional[0];
  auto files = ovis::load_program(program_path);
  std::vector<std::string> previous_lockfiles;
  for (const auto &file : files)
    previous_lockfiles.push_back(file.lockfile.to_text());

  const auto model = make_model(p_arguments, p_argument_zero);
  auto options = p_arguments.compile;
  options.log = progress_log(p_arguments);

  auto compilation = ovis::compile(files, model.get(), options);
  for (std::size_t index = 0; index < files.size(); ++index) {
    const auto &file = files[index];
    if (file.lockfile.to_text() != previous_lockfiles[index])
      file.lockfile.save(ovis::english::lockfile::path_for(file.path));
  }
  for (const auto &warning : compilation.warnings)
    std::cerr << "ovis: " << warning << '\n';
  if (!p_arguments.keep_source) {
    bool wrote_any = false;
    for (std::size_t index = 0; index < files.size(); ++index) {
      if (compilation.detailed_sources[index] == files[index].text)
        continue;
      write_source(files[index].path, compilation.detailed_sources[index]);
      wrote_any = true;
      if (!p_arguments.quiet)
        std::cerr << "ovis: wrote the detail into "
                  << files[index].path.string() << '\n';
    }
    if (wrote_any)
      compilation.use_detailed_lines();
  }
  if (!compilation.succeeded()) {
    std::cerr << compilation.report(p_arguments.verbose);
    return 1;
  }

  if (command == "translate" || command == "explain") {
    print_translation(compilation, command == "translate");
    return 0;
  }

  // A multi-file program is named after its folder.
  const auto program_name =
      files.size() > 1 || files[0].module == "main"
          ? std::filesystem::absolute(files.back().path)
                .parent_path()
                .filename()
                .string()
          : files[0].path.stem().string();
  return generate(p_arguments, compilation.program, program_name,
                  files.back().path);
}

// Native code for a checked program: printed (`ir`), written (`build`) or
// run (`run`).
auto generate(const arguments &p_arguments, const ovis::core::program &p_program,
              const std::string &program_name,
              const std::filesystem::path &p_source) -> int {
  const auto &command = p_arguments.command;
  llvm::LLVMContext context;
  auto module = ovis::codegen::generate_module(p_program, context,
                                               program_name);
  ovis::codegen::optimize(*module);
  if (command == "ir") {
    std::cout << ovis::codegen::to_ir_text(*module);
    return 0;
  }

  const auto libraries = ovis::codegen::libraries_of(p_program);
  if (command == "build") {
    auto output = p_arguments.output;
    if (output.empty()) {
      output = p_source.parent_path() / program_name;
      if (output == p_source)
        output += ".out";
    }
    ovis::codegen::build_executable(*module, output, libraries);
    return 0;
  }

  const auto executable =
      std::filesystem::temp_directory_path() /
      ("ovis-run-" + std::to_string(llvm::sys::Process::getProcessId()));
  ovis::codegen::build_executable(*module, executable, libraries);
  const auto status = run_program(executable.string());
  std::filesystem::remove(executable);
  return status;
}

} // namespace

int main(int p_count, char **p_values) {
  try {
    return run(parse_arguments(p_count, p_values), p_values[0]);
  } catch (const std::exception &p_error) {
    std::cerr << p_error.what() << '\n';
    return 1;
  }
}
