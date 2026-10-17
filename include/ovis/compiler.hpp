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
  // Translate every paragraph again instead of using the lockfile.
  bool retranslate = false;
  // How many times the model may try to translate a paragraph.
  int max_attempts = 3;
  std::function<void(string_view_type)> log;
};

struct translated_paragraph {
  english::paragraph source;
  string_type core;
  bool from_lockfile = false;
  int attempts = 0;
  std::vector<string_type> problems;
  // Details the elaborator and checker filled in deterministically.
  std::vector<string_type> assumptions;
};

struct compilation {
  std::vector<translated_paragraph> paragraphs;
  // Checked and ready for code generation when `succeeded()`.
  core::program program;

  auto succeeded() const -> bool;
  // Every problem, labelled with the paragraph it belongs to.
  auto report() const -> string_type;
};

// English source to checked Ovis Core.
//
// Paragraphs found in the lockfile are reused, the rest are sent to the
// model. Each translation is parsed and the whole program is type checked;
// paragraphs with problems are sent back to the model together with the
// problems until they are fixed or run out of attempts. Without a model
// (`p_model == nullptr`) only the lockfile is used.
//
// On success the lockfile is updated with the translations that were used.
auto compile(string_view_type p_source, english::lockfile &p_lockfile,
             english::language_model *p_model,
             const compile_options &p_options = {}) -> compilation;

} // namespace ovis

#endif // OVIS_COMPILER_HPP
