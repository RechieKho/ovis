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

namespace {

constexpr const char *usage = R"usage(Ovis: programs written in plain English, compiled with LLVM.

Usage: ovis <command> <file> [options]
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
  -o <path>           output executable for `build` (default: file name without extension)
  --offline           never call the model; use only the lockfile
  --retranslate       ignore the lockfile and translate everything again
  --attempts <n>      translation attempts per paragraph (default: 3)
  --keep-source       don't write the detail into the source file
  --quiet             don't print progress
  --verbose           show the code behind failures, and llama.cpp's log

Built-in model (the default):
  --model-path <gguf> model file to use (default: $OVIS_MODEL_PATH, the model
                      embedded in ovis, or the default model next to ovis or in
                      the cache directory)
  --threads <n>       CPU threads (default: up to 4)

Model server instead of the built-in model:
  --endpoint <url>    OpenAI-compatible API, e.g. http://127.0.0.1:8080/v1
                      (default: $OVIS_ENDPOINT if set)
  --model <name>      model name to request (default: $OVIS_MODEL or "local")
  --api-key <key>     sent as a bearer token (default: $OVIS_API_KEY)
  --no-grammar        don't send the GBNF grammar (for servers that reject it)

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
  bool verbose = false;
  ovis::compile_options compile;
  bool offline = false;
  bool quiet = false;
  bool keep_source = false;
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
    else if (option == "--threads")
      result.threads = std::stoi(value());
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
    else
      throw ovis::error("Unknown option " + option + ".\n\n" + usage);
  }
  if (result.positional.empty())
    throw ovis::error(usage);
  return result;
}

auto read_file(const std::filesystem::path &p_path) -> std::string {
  std::ifstream input(p_path);
  if (!input)
    throw ovis::error("Cannot open " + p_path.string() + ".");
  std::stringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
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
                                     .threads = p_arguments.threads,
                                     .verbose = p_arguments.verbose});
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
  for (const auto &paragraph : p_compilation.paragraphs) {
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

auto run(const arguments &p_arguments, const char *p_argument_zero) -> int {
  const auto &command = p_arguments.command;
  if (command == "model")
    return run_model_command(p_arguments, p_argument_zero);
  if (command != "run" && command != "build" && command != "translate" &&
      command != "explain" && command != "ir")
    throw ovis::error("Unknown command " + command + ".\n\n" + usage);

  const std::filesystem::path source_path = p_arguments.positional[0];
  const auto source = read_file(source_path);
  const auto lock_path = ovis::english::lockfile::path_for(source_path);
  auto lockfile = ovis::english::lockfile::load(lock_path);
  const auto previous_lockfile = lockfile.to_text();

  const auto model = make_model(p_arguments, p_argument_zero);
  auto options = p_arguments.compile;
  if (!p_arguments.quiet)
    options.log = [](ovis::string_view_type p_message) {
      std::cerr << "ovis: " << p_message << '\n';
    };

  const auto compilation =
      ovis::compile(source, lockfile, model.get(), options);
  if (lockfile.to_text() != previous_lockfile)
    lockfile.save(lock_path);
  for (const auto &warning : compilation.warnings)
    std::cerr << "ovis: " << warning << '\n';
  if (!p_arguments.keep_source && compilation.detailed_source != source) {
    write_source(source_path, compilation.detailed_source);
    if (!p_arguments.quiet)
      std::cerr << "ovis: wrote the detail into " << source_path.string()
                << '\n';
  }
  if (!compilation.succeeded()) {
    std::cerr << compilation.report(p_arguments.verbose);
    return 1;
  }

  if (command == "translate" || command == "explain") {
    print_translation(compilation, command == "translate");
    return 0;
  }

  llvm::LLVMContext context;
  auto module = ovis::codegen::generate_module(
      compilation.program, context, source_path.filename().string());
  ovis::codegen::optimize(*module);
  if (command == "ir") {
    std::cout << ovis::codegen::to_ir_text(*module);
    return 0;
  }

  const auto libraries = ovis::codegen::libraries_of(compilation.program);
  if (command == "build") {
    auto output = p_arguments.output;
    if (output.empty()) {
      output = source_path;
      output.replace_extension();
      if (output == source_path)
        output += ".out";
    }
    ovis::codegen::build_executable(*module, output, libraries);
    return 0;
  }

  const auto executable =
      std::filesystem::temp_directory_path() /
      ("ovis-run-" + std::to_string(llvm::sys::Process::getProcessId()));
  ovis::codegen::build_executable(*module, executable, libraries);
  const auto path = executable.string();
  const auto status = llvm::sys::ExecuteAndWait(path, {path});
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
