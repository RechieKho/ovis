#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>

#include <llvm/Support/Process.h>
#include <llvm/Support/Program.h>

#include <ovis/codegen/llvm_codegen.hpp>
#include <ovis/compiler.hpp>

namespace {

constexpr const char *usage = R"usage(Ovis: programs written in plain English, compiled with LLVM.

Usage: ovis <command> <file> [options]

Commands:
  run        translate, compile and run the program
  build      translate and compile to an executable
  translate  show what every paragraph was translated into (Ovis Core)
  explain    show the details Ovis filled in for every paragraph
  ir         print the generated LLVM IR

Options:
  -o <path>          output executable for `build` (default: file name without extension)
  --endpoint <url>   OpenAI-compatible API of the model
                     (default: $OVIS_ENDPOINT or http://127.0.0.1:8080/v1)
  --model <name>     model name to request (default: $OVIS_MODEL or "local")
  --api-key <key>    sent as a bearer token (default: $OVIS_API_KEY)
  --offline          never call the model; use only the lockfile
  --retranslate      ignore the lockfile and translate everything again
  --no-grammar       don't send the GBNF grammar (for servers that reject it)
  --attempts <n>     translation attempts per paragraph (default: 3)
  --quiet            don't print progress

Translations are kept in <file>.lock next to the program. Commit it: it makes
builds reproducible and lets you read or correct what the model understood.
)usage";

struct arguments {
  std::string command;
  std::filesystem::path source;
  std::filesystem::path output;
  ovis::english::endpoint_options endpoint;
  ovis::compile_options compile;
  bool offline = false;
  bool quiet = false;
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
  result.source = p_values[2];
  result.endpoint.endpoint =
      environment("OVIS_ENDPOINT", result.endpoint.endpoint);
  result.endpoint.model = environment("OVIS_MODEL", result.endpoint.model);
  result.endpoint.api_key = environment("OVIS_API_KEY", "");

  for (int index = 3; index < p_count; ++index) {
    const std::string option = p_values[index];
    const auto value = [&]() -> std::string {
      if (index + 1 >= p_count)
        throw ovis::error(option + " needs a value.");
      return p_values[++index];
    };
    if (option == "-o")
      result.output = value();
    else if (option == "--endpoint")
      result.endpoint.endpoint = value();
    else if (option == "--model")
      result.endpoint.model = value();
    else if (option == "--api-key")
      result.endpoint.api_key = value();
    else if (option == "--offline")
      result.offline = true;
    else if (option == "--retranslate")
      result.compile.retranslate = true;
    else if (option == "--no-grammar")
      result.endpoint.use_grammar = false;
    else if (option == "--attempts")
      result.compile.max_attempts = std::stoi(value());
    else if (option == "--quiet")
      result.quiet = true;
    else
      throw ovis::error("Unknown option " + option + ".\n\n" + usage);
  }

  if (result.output.empty()) {
    result.output = result.source;
    result.output.replace_extension();
    if (result.output == result.source)
      result.output += ".out";
  }
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

auto print_translation(const ovis::compilation &p_compilation,
                       bool p_with_core) -> void {
  for (const auto &paragraph : p_compilation.paragraphs) {
    std::cout << "; line " << paragraph.source.line << ":\n";
    std::stringstream english(paragraph.source.text);
    for (std::string line; std::getline(english, line);)
      std::cout << ";   " << line << '\n';
    for (const auto &assumption : paragraph.assumptions)
      std::cout << ";   * " << assumption << '\n';
    if (p_with_core)
      std::cout << paragraph.core << '\n';
    std::cout << '\n';
  }
}

auto run(const arguments &p_arguments) -> int {
  const auto &command = p_arguments.command;
  if (command != "run" && command != "build" && command != "translate" &&
      command != "explain" && command != "ir")
    throw ovis::error("Unknown command " + command + ".\n\n" + usage);

  const auto source = read_file(p_arguments.source);
  const auto lock_path = ovis::english::lockfile::path_for(p_arguments.source);
  auto lockfile = ovis::english::lockfile::load(lock_path);
  const auto previous_entries = lockfile.entries;

  std::unique_ptr<ovis::english::language_model> model;
  if (!p_arguments.offline)
    model = std::make_unique<ovis::english::endpoint_model>(
        p_arguments.endpoint);

  auto options = p_arguments.compile;
  if (!p_arguments.quiet)
    options.log = [](ovis::string_view_type p_message) {
      std::cerr << "ovis: " << p_message << '\n';
    };

  const auto compilation =
      ovis::compile(source, lockfile, model.get(), options);
  if (lockfile.entries != previous_entries)
    lockfile.save(lock_path);
  if (!compilation.succeeded()) {
    std::cerr << compilation.report();
    return 1;
  }

  if (command == "translate" || command == "explain") {
    print_translation(compilation, command == "translate");
    return 0;
  }

  llvm::LLVMContext context;
  auto module = ovis::codegen::generate_module(
      compilation.program, context, p_arguments.source.filename().string());
  ovis::codegen::optimize(*module);
  if (command == "ir") {
    std::cout << ovis::codegen::to_ir_text(*module);
    return 0;
  }

  const auto libraries = ovis::codegen::libraries_of(compilation.program);
  if (command == "build") {
    ovis::codegen::build_executable(*module, p_arguments.output, libraries);
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
    return run(parse_arguments(p_count, p_values));
  } catch (const std::exception &p_error) {
    std::cerr << p_error.what() << '\n';
    return 1;
  }
}
