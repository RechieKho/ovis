#ifndef OVIS_PHRASE_FRONT_END_HPP
#define OVIS_PHRASE_FRONT_END_HPP

#include <filesystem>
#include <functional>
#include <set>
#include <vector>

#include <ovis/core/ast.hpp>
#include <ovis/core/c_import.hpp>
#include <ovis/english/language_model.hpp>
#include <ovis/phrase/lockfile.hpp>

// The phrase front end, end to end: English-like lines to a checked Ovis
// Core program. How it works: docs/phrase-front-end.md.
namespace ovis::phrase {

struct front_end_options {
  // Questions still open when the model has used this much time go to the
  // author, so a build never waits long on the model. Loading the model
  // (during the first question) isn't counted.
  double model_budget_seconds = 10;
  std::function<void(string_view_type)> log;
  // For `compile_phrases`: the program's lockfile from the last build,
  // whose readings are used before asking the model.
  const lockfile *previous = nullptr;
  // Where `use "header.h"` looks for headers and libraries.
  core::c_import_options c_imports;
  // Experiments, measured with `ovis_eval questions` (state.md): readings
  // the ranking ties are ordered by the model's embeddings, and with
  // `choose_by_embedding` the first is taken without asking. Off: neither
  // raised the model's right picks.
  bool order_ties_by_embedding = false;
  bool choose_by_embedding = false;
  // When no reading offered is taken, the model may write the unclear line
  // again in words the compiler reads (checked before it is kept).
  bool rewrite = true;
};

// Where the time went, for judging the approach.
struct front_end_timing {
  double lex_ms = 0;
  // Parsing and building questions, without the model.
  double parse_ms = 0;
  double model_ms = 0;
  // The first question also loads the model.
  double first_question_ms = 0;
  double check_ms = 0;
  std::size_t lines = 0;
  // Unclear spans found, and how many of those the model was asked about
  // (the others were settled by `Read as:` lines).
  std::size_t questions = 0;
  std::size_t asked = 0;
  // How the unclear spans were settled: by `Read as:` lines, by a
  // deterministic rule (the one fitting name), by the ranking shortcut, or
  // by the model. The rest went to the author.
  std::size_t by_source = 0;
  std::size_t by_lockfile = 0;
  std::size_t by_rule = 0;
  std::size_t by_ranking = 0;
  std::size_t by_model = 0;
  // Settled by the model's rewrite of the line.
  std::size_t by_rewrite = 0;
};

struct front_end_result {
  core::program program;
  // "line N: …", in source order. The build stops if there are any.
  std::vector<string_type> problems;
  // Worth telling the author, but not stopping the build.
  std::vector<string_type> warnings;
  // The source with the `Read as:` lines of new decisions written in.
  string_type source;
  // The readings of this build that came from the model or the previous
  // lockfile: the lockfile to save.
  lockfile lock;
  front_end_timing timing;
  // The templates the program's lines were read with, as "lists.phr:12".
  std::set<string_type> templates_used;

  auto succeeded() const -> bool { return problems.empty(); }
};

// One file of a program in phrases.
struct phrase_file {
  std::filesystem::path path;
  // How reports name it, e.g. `pricing/tax.ovis`; empty for a single-file
  // program.
  string_type name;
  // Its module, e.g. `pricing.tax` or `main`; empty for a single-file
  // program.
  string_type module;
  string_type text;
  // Its lockfile from the last build.
  lockfile lock;
};

// The files of the program `p_path` names:
// - a folder, or a file called `main.ovis`: every `.ovis` file in that folder
//   and its subfolders (`pricing/tax.ovis` is the module `pricing.tax`),
//   helpers first and `main.ovis` last. A subfolder with its own `main.ovis`
//   is a separate program and is left out;
// - any other `.ovis` file: a program on its own.
// Lockfiles are loaded next to each file. Throws `error`.
auto load_phrase_program(const std::filesystem::path &p_path)
    -> std::vector<phrase_file>;

struct program_result {
  core::program program;
  // "pricing/tax.ovis, line N: …" (no file name for a single file).
  std::vector<string_type> problems;
  std::vector<string_type> warnings;
  // Per file, in the order given: its source with new `Read as:` lines, and
  // the lockfile to save.
  std::vector<string_type> sources;
  std::vector<lockfile> locks;
  front_end_timing timing;
  std::set<string_type> templates_used;

  auto succeeded() const -> bool { return problems.empty(); }
};

// Reads a program of several files. Phrases defined in any file work in
// all of them; top-level variables of helpers are visible to the files
// after them; helpers may only define (phrases, `use`, and constants).
auto compile_phrase_program(const std::vector<phrase_file> &p_files,
                            english::language_model *p_model,
                            const front_end_options &p_options = {})
    -> program_result;

// Reads a program in phrases. Unclear spans are settled by their `Read as:`
// line, else by `p_model` choosing between readings that type-check (written
// back as a `Read as:` line), else reported to the author. Without a model
// (`p_model == nullptr`), unsettled spans are reported.
auto compile_phrases(string_view_type p_source, english::language_model *p_model,
                     const front_end_options &p_options = {})
    -> front_end_result;

} // namespace ovis::phrase

#endif // OVIS_PHRASE_FRONT_END_HPP
