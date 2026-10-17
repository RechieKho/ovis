#ifndef OVIS_COMPILER_HPP
#define OVIS_COMPILER_HPP

#include <functional>
#include <optional>
#include <vector>

#include <ovis/core/ast.hpp>
#include <ovis/english/language_model.hpp>
#include <ovis/english/lockfile.hpp>
#include <ovis/english/paragraph.hpp>

namespace ovis {

struct compile_options {
  // Detail paragraphs that have no steps written under them, and translate
  // every paragraph, without using the lockfile.
  bool retranslate = false;
  // How many times the model may try to translate a paragraph.
  int max_attempts = 3;
  std::function<void(string_view_type)> log;
};

struct translated_paragraph {
  // What the detail pass still has to do for this paragraph.
  enum class detail_action {
    none,
    // Write steps for it.
    detail,
    // Its text changed: write steps again, keeping what still fits from the
    // old ones (`previous_steps`).
    revise,
    // The author edited its steps: check they still match the paragraph.
    review
  };

  english::paragraph source;
  // The author's own lines, without the detail Ovis wrote under them.
  string_type intent;

  english::detail_result detail;
  bool is_detailed = false;
  detail_action pending = detail_action::none;
  std::vector<string_type> previous_steps;
  // Steps left behind after their sentence was deleted; removed from the
  // source.
  bool is_removed = false;
  // The steps came from the source and differ from what the model wrote
  // (the author edited or wrote them).
  bool steps_from_source = false;
  // Why the paragraph could not be detailed, if it couldn't.
  string_type detail_problem;

  string_type core;
  bool from_lockfile = false;
  int attempts = 0;
  std::vector<string_type> problems;
  // Details the elaborator and checker filled in deterministically.
  std::vector<string_type> compiler_assumptions;
  // The `Compiler:` lines already in the source, kept if the build fails.
  std::vector<string_type> written_compiler_notes;
  // The model's explanation when the paragraph could not become code.
  string_type failure_reason;
  string_type failure_suggestion;

  auto steps() const -> string_type {
    return english::numbered_steps(detail.steps);
  }
};

struct compilation {
  std::vector<translated_paragraph> paragraphs;
  // Where each paragraph starts in `detailed_source`.
  std::vector<std::size_t> detailed_lines;
  // Checked and ready for code generation when `succeeded()`.
  core::program program;
  // The source with every paragraph's detail written under it. Equal to the
  // input once the source is fully detailed.
  string_type detailed_source;
  std::vector<string_type> warnings;

  auto succeeded() const -> bool;
  // Makes reports refer to lines of `detailed_source`, once it has replaced
  // the file.
  auto use_detailed_lines() -> void;
  // Every problem, labelled with the paragraph it belongs to. `p_verbose`
  // adds the Ovis Core and its problems to the model's explanations.
  auto report(bool p_verbose = false) const -> string_type;
};

// English source to checked Ovis Core, in two model passes:
//
// 1. Detail: every paragraph becomes concrete numbered steps plus the
//    assumptions made to get there, or is rejected as too vague or
//    irrelevant (with the model's reason and suggestion), which stops the
//    build. Steps already written under a paragraph are used as they are, so
//    the author can edit them.
// 2. Translate: the steps become Ovis Core, which is elaborated and checked;
//    paragraphs with problems go back to the model until fixed or out of
//    attempts, and then the model explains what went wrong.
//
// Both passes reuse the lockfile and update it. Without a model
// (`p_model == nullptr`) only the lockfile is used.
auto compile(string_view_type p_source, english::lockfile &p_lockfile,
             english::language_model *p_model,
             const compile_options &p_options = {}) -> compilation;

} // namespace ovis

#endif // OVIS_COMPILER_HPP
