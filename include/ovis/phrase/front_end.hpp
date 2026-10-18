#ifndef OVIS_PHRASE_FRONT_END_HPP
#define OVIS_PHRASE_FRONT_END_HPP

#include <functional>
#include <vector>

#include <ovis/core/ast.hpp>
#include <ovis/english/language_model.hpp>

// The phrase front end, end to end: English-like lines to a checked Ovis
// Core program. This is the first, minimal slice (no user functions, no
// lockfile); the plan is M1 in docs/remaining-tasks.md.
namespace ovis::phrase {

struct front_end_options {
  // Questions still open when the model has used this much time go to the
  // author, so a build never waits long on the model. Loading the model
  // (during the first question) isn't counted.
  double model_budget_seconds = 10;
  std::function<void(string_view_type)> log;
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
  std::size_t by_rule = 0;
  std::size_t by_ranking = 0;
  std::size_t by_model = 0;
};

struct front_end_result {
  core::program program;
  // "line N: …", in source order. The build stops if there are any.
  std::vector<string_type> problems;
  // The source with the `Read as:` lines of new decisions written in.
  string_type source;
  front_end_timing timing;

  auto succeeded() const -> bool { return problems.empty(); }
};

// Reads a program in phrases. Unclear spans are settled by their `Read as:`
// line, else by `p_model` choosing between readings that type-check (written
// back as a `Read as:` line), else reported to the author. Without a model
// (`p_model == nullptr`), unsettled spans are reported.
auto compile_phrases(string_view_type p_source, english::language_model *p_model,
                     const front_end_options &p_options = {})
    -> front_end_result;

} // namespace ovis::phrase

#endif // OVIS_PHRASE_FRONT_END_HPP
