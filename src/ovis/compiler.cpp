#include <ovis/compiler.hpp>
#include <ovis/core/checker.hpp>
#include <ovis/core/elaborate.hpp>
#include <ovis/english/prompt.hpp>

namespace ovis {

namespace {

using status_type = english::detail_result::status_type;

auto indent(string_view_type p_text, string_view_type p_prefix)
    -> string_type {
  string_type result(p_prefix);
  for (const auto character : p_text) {
    result.push_back(character);
    if (character == '\n')
      result += p_prefix;
  }
  return result;
}

// Keeps reports readable when a translation went badly wrong.
auto shorten(string_view_type p_text) -> string_type {
  constexpr std::size_t limit = 1200;
  if (p_text.size() <= limit)
    return string_type(p_text);
  return string_type(p_text.substr(0, limit)) + " ... (" +
         std::to_string(p_text.size() - limit) + " more characters)";
}

auto line_label(const translated_paragraph &p_paragraph) -> string_type {
  return "Paragraph at line " + std::to_string(p_paragraph.source.line);
}

struct evaluation {
  core::program program;
  // The paragraph each unit of `program` came from.
  std::vector<std::size_t> owners;
};

// Parses, elaborates and checks everything translated so far, recording the
// problems and assumptions of each paragraph.
auto evaluate(std::vector<translated_paragraph> &p_paragraphs,
              const compile_options &p_options) -> evaluation {
  evaluation result;
  for (std::size_t index = 0; index < p_paragraphs.size(); ++index) {
    auto &paragraph = p_paragraphs[index];
    paragraph.problems.clear();
    paragraph.compiler_assumptions.clear();
    if (paragraph.core.empty()) {
      paragraph.problems.push_back("Not translated yet.");
      continue;
    }
    try {
      result.program.units.push_back(core::parse_unit(paragraph.core));
      result.owners.push_back(index);
    } catch (const error &p_error) {
      paragraph.problems.push_back(p_error.what());
    }
  }
  for (const auto &problem :
       core::resolve_c_imports(result.program, p_options.c_imports))
    p_paragraphs[result.owners[problem.unit_index]].problems.push_back(
        problem.message);
  auto notes = core::elaborate(result.program);
  for (const auto &problem : core::check(result.program, &notes))
    p_paragraphs[result.owners[problem.unit_index]].problems.push_back(
        "line " + std::to_string(problem.location.line) + ": " +
        problem.message);
  for (const auto &note : notes)
    p_paragraphs[result.owners[note.unit_index]]
        .compiler_assumptions.push_back(note.message);
  return result;
}

auto context_for(const evaluation &p_state, std::size_t p_paragraph)
    -> std::vector<string_type> {
  std::vector<string_type> result;
  for (std::size_t index = 0; index < p_state.program.units.size(); ++index) {
    if (p_state.owners[index] == p_paragraph)
      continue;
    for (auto &declaration :
         core::describe_declarations(p_state.program.units[index]))
      result.push_back(std::move(declaration));
  }
  return result;
}

// Names of the functions the program defines so far, for the repair grammar.
auto defined_names(const evaluation &p_state) -> std::vector<string_type> {
  std::vector<string_type> result;
  for (const auto &unit : p_state.program.units) {
    for (const auto &function : unit.functions)
      result.push_back(function.name);
    for (const auto &function : unit.externs)
      result.push_back(function.name);
  }
  return result;
}

// What the other paragraphs do, for the detail pass.
auto other_steps(const std::vector<translated_paragraph> &p_paragraphs,
                 std::size_t p_paragraph) -> std::vector<string_type> {
  std::vector<string_type> result;
  for (std::size_t index = 0; index < p_paragraphs.size(); ++index) {
    const auto &other = p_paragraphs[index];
    if (index == p_paragraph || !other.is_detailed || other.is_removed ||
        other.detail.status != status_type::ok)
      continue;
    for (const auto &step : other.detail.steps)
      result.push_back("- " + english::normalize(step));
  }
  return result;
}

auto indent_steps(const std::vector<string_type> &p_steps) -> string_type {
  return indent(english::numbered_steps(p_steps), "    ");
}

// Decides, without the model, what a paragraph's detail is or what the
// detail pass still has to do. A block of steps never outlives the sentence
// it was written for.
auto reconcile_detail(translated_paragraph &p_paragraph,
                      const english::lockfile &p_lockfile,
                      const compile_options &p_options,
                      std::vector<string_type> &p_warnings) -> void {
  using action = translated_paragraph::detail_action;
  const auto written = english::parse_detailed(p_paragraph.source.text);
  p_paragraph.intent = written.intent;
  p_paragraph.written_compiler_notes = written.compiler_notes;
  const bool has_steps = written.has_detail && !written.steps.empty();

  if (english::normalize(written.intent).empty()) {
    // The sentence was deleted but its steps were not.
    if (has_steps && p_lockfile.find_origin(written.steps) != nullptr) {
      p_paragraph.is_removed = true;
      p_paragraph.is_detailed = true;
      p_paragraph.detail.status = status_type::note;
      p_warnings.push_back("The sentence above the steps at line " +
                           std::to_string(p_paragraph.source.line) +
                           " was deleted, so these steps were removed too:\n" +
                           indent_steps(written.steps));
      return;
    }
    p_paragraph.detail_problem =
        "These steps have no sentence above them saying what they are for. "
        "Write that sentence above the steps, or delete the steps.";
    return;
  }

  const auto *const known = p_lockfile.find_detail(written.intent);
  if (has_steps) {
    // Unchanged since the model wrote or checked them. A mismatch found
    // earlier stands until the steps or the sentence change.
    if (known != nullptr && english::numbered_steps(known->detail.steps) ==
                                english::numbered_steps(written.steps)) {
      p_paragraph.detail =
          known->detail.status == status_type::ok
              ? english::detail_result{status_type::ok, written.steps,
                                       written.assumptions}
              : known->detail;
      p_paragraph.is_detailed = true;
      return;
    }
    // The sentence changed under steps written for its old version.
    if (known == nullptr && p_lockfile.find_origin(written.steps) != nullptr) {
      p_paragraph.pending = action::revise;
      p_paragraph.previous_steps = written.steps;
      p_warnings.push_back(line_label(p_paragraph) +
                           " changed, so its steps are written again, keeping "
                           "only what still fits it.");
      return;
    }
    // The author edited the steps, or wrote them.
    p_paragraph.pending = action::review;
    p_paragraph.detail.steps = written.steps;
    p_paragraph.steps_from_source = true;
    return;
  }
  if (known != nullptr && !p_options.retranslate) {
    p_paragraph.detail = known->detail;
    p_paragraph.is_detailed = true;
    return;
  }
  p_paragraph.pending = action::detail;
}

// Returns whether the model was asked anything.
auto run_detail_pass(compilation &p_result, english::language_model &p_model,
                     const std::function<void(const string_type &)> &p_log)
    -> bool {
  using action = translated_paragraph::detail_action;
  bool asked = false;
  auto &paragraphs = p_result.paragraphs;
  for (std::size_t index = 0; index < paragraphs.size(); ++index) {
    auto &paragraph = paragraphs[index];
    if (paragraph.pending == action::none)
      continue;
    const auto line = std::to_string(paragraph.source.line);
    asked = true;
    try {
      if (paragraph.pending == action::review) {
        p_log("Checking the edited steps of the paragraph at line " + line +
              "...");
        auto review = english::parse_review(p_model.chat(
            english::review_request(paragraph.intent, paragraph.detail.steps,
                                    other_steps(paragraphs, index)),
            english::review_grammar()));
        review.steps = paragraph.detail.steps;
        paragraph.detail = std::move(review);
      } else {
        p_log("Detailing the paragraph at line " + line + "...");
        paragraph.detail = english::parse_detail(p_model.chat(
            english::detail_request(paragraph.intent,
                                    other_steps(paragraphs, index),
                                    paragraph.previous_steps),
            english::detail_grammar()));
      }
      paragraph.is_detailed = true;
      paragraph.pending = action::none;
    } catch (const error &p_error) {
      paragraph.detail_problem = p_error.what();
    }
  }
  return asked;
}

auto run_translation(compilation &p_result, english::lockfile &p_lockfile,
                     english::language_model *p_model,
                     const compile_options &p_options,
                     const std::function<void(const string_type &)> &p_log)
    -> evaluation {
  auto &paragraphs = p_result.paragraphs;
  for (auto &paragraph : paragraphs) {
    if (paragraph.detail.status == status_type::note) {
      paragraph.core = "(pass)";
    } else if (!p_options.retranslate) {
      if (auto core = p_lockfile.find_translation(paragraph.steps())) {
        paragraph.core = std::move(*core);
        paragraph.from_lockfile = true;
      }
    }
  }

  auto state = evaluate(paragraphs, p_options);
  while (p_model != nullptr) {
    bool translated_any = false;
    for (std::size_t index = 0; index < paragraphs.size(); ++index) {
      auto &paragraph = paragraphs[index];
      if (paragraph.problems.empty() ||
          paragraph.attempts >= p_options.max_attempts)
        continue;

      auto request = english::translation_request(
          paragraph.intent, paragraph.steps(), context_for(state, index));
      const bool is_repair = !paragraph.core.empty();
      if (is_repair)
        request = english::repair_request(std::move(request), paragraph.core,
                                          paragraph.problems);
      p_log((is_repair ? "Fixing" : "Translating") +
            string_type(" the paragraph at line ") +
            std::to_string(paragraph.source.line) + "...");

      // A first translation may call functions other paragraphs haven't
      // defined yet; a repair happens once they are all known.
      const auto grammar =
          is_repair ? english::restricted_core_grammar(defined_names(state))
                    : string_type(english::core_grammar());
      paragraph.core = english::extract_core(p_model->chat(request, grammar));
      paragraph.from_lockfile = false;
      ++paragraph.attempts;
      translated_any = true;
      // Later paragraphs get to see what this one declared.
      state = evaluate(paragraphs, p_options);
    }
    if (!translated_any)
      break;
  }

  // Tell the author, in English, why what's left doesn't work.
  if (p_model != nullptr) {
    for (auto &paragraph : paragraphs) {
      if (paragraph.problems.empty() || paragraph.core.empty())
        continue;
      p_log("Asking why the paragraph at line " +
            std::to_string(paragraph.source.line) + " doesn't work...");
      const auto [reason, suggestion] =
          english::parse_explanation(p_model->chat(
              english::explanation_request(paragraph.intent,
                                           paragraph.steps(), paragraph.core,
                                           paragraph.problems),
              english::explanation_grammar()));
      paragraph.failure_reason = reason;
      paragraph.failure_suggestion = suggestion;
    }
  }
  return state;
}

// `Compiler:` lines are only refreshed by a build that succeeds; a broken
// program says little about what a working one assumes.
auto render_source(string_view_type p_source,
                   const std::vector<translated_paragraph> &p_paragraphs,
                   bool p_succeeded, std::vector<std::size_t> &p_new_lines)
    -> string_type {
  std::vector<english::paragraph> spans;
  std::vector<string_type> texts;
  for (const auto &paragraph : p_paragraphs) {
    spans.push_back(paragraph.source);
    if (paragraph.is_removed)
      texts.emplace_back();
    else if (paragraph.is_detailed &&
             paragraph.detail.status == status_type::ok)
      texts.push_back(english::render_detailed(
          paragraph.intent, paragraph.detail.steps,
          paragraph.detail.assumptions,
          p_succeeded ? paragraph.compiler_assumptions
                      : paragraph.written_compiler_notes));
    else
      texts.push_back(paragraph.source.text);
  }
  return english::replace_paragraphs(p_source, spans, texts, &p_new_lines);
}

} // namespace

auto compilation::succeeded() const -> bool {
  for (const auto &paragraph : paragraphs)
    if (!paragraph.is_detailed || paragraph.detail.is_refusal() ||
        !paragraph.problems.empty())
      return false;
  return true;
}

auto compilation::use_detailed_lines() -> void {
  for (std::size_t index = 0; index < paragraphs.size(); ++index)
    paragraphs[index].source.line = detailed_lines[index];
}

auto compilation::report(bool p_verbose) const -> string_type {
  string_type result;
  for (const auto &paragraph : paragraphs) {
    const auto text = indent(paragraph.intent, "  | ") + "\n";
    if (!paragraph.is_detailed) {
      if (!paragraph.detail_problem.empty()) {
        result += line_label(paragraph) + ":\n" +
                  indent(paragraph.source.text, "  | ") + "\n  " +
                  paragraph.detail_problem + "\n\n";
        continue;
      }
      if (paragraph.pending == translated_paragraph::detail_action::review)
        result += line_label(paragraph) + " has edited steps:\n" + text +
                  "  Run without --offline so the model can check them.\n\n";
      else
        result += line_label(paragraph) + " has not been detailed yet:\n" +
                  text +
                  "  Run without --offline so the model can detail it.\n\n";
      continue;
    }
    if (paragraph.detail.is_refusal()) {
      const auto status = paragraph.detail.status;
      result +=
          line_label(paragraph) +
          (status == status_type::too_vague
               ? " is too vague to turn into a program:\n" + text
           : status == status_type::irrelevant
               ? " is not about what the program does:\n" + text
               : " has steps that no longer do what it says:\n" + text +
                     indent_steps(paragraph.detail.steps) + "\n") +
          "  Why: " + paragraph.detail.reason + "\n";
      if (!paragraph.detail.suggestion.empty())
        result += "  Try: " + paragraph.detail.suggestion + "\n";
      result += "\n";
      continue;
    }
    if (paragraph.problems.empty())
      continue;

    result += line_label(paragraph) + " could not be turned into code:\n" +
              text;
    if (paragraph.core.empty()) {
      result += paragraph.steps_from_source
                    ? "  Its steps were edited, so it needs translating again. "
                      "Run without --offline so the model can do that.\n\n"
                    : "  It has no translation yet. Run without --offline so "
                      "the model can translate it.\n\n";
      continue;
    }
    const bool is_explained = !paragraph.failure_reason.empty();
    if (is_explained) {
      result += "  Why: " + paragraph.failure_reason + "\n";
      if (!paragraph.failure_suggestion.empty())
        result += "  Try: " + paragraph.failure_suggestion + "\n";
    }
    if (p_verbose || !is_explained) {
      result += "  Its steps were translated" +
                string_type(paragraph.from_lockfile ? " (from the lockfile)"
                                                    : "") +
                " to:\n" + indent(shorten(paragraph.core), "  > ") +
                "\n  but:\n";
      for (const auto &problem : paragraph.problems)
        result += "  - " + problem + "\n";
    } else {
      result += "  (--verbose shows the code and its problems.)\n";
    }
    result += "\n";
  }
  return result;
}

auto compile(string_view_type p_source, english::lockfile &p_lockfile,
             english::language_model *p_model,
             const compile_options &p_options) -> compilation {
  const std::function<void(const string_type &)> log =
      [&](const string_type &p_message) {
        if (p_options.log)
          p_options.log(p_message);
      };

  compilation result;
  for (auto &paragraph : english::split_paragraphs(p_source)) {
    translated_paragraph translated{.source = std::move(paragraph)};
    reconcile_detail(translated, p_lockfile, p_options, result.warnings);
    result.paragraphs.push_back(std::move(translated));
  }

  // 1. Detail.
  bool used_model =
      p_model != nullptr && run_detail_pass(result, *p_model, log);
  std::vector<english::lockfile::detail_entry> details;
  for (const auto &paragraph : result.paragraphs)
    if (paragraph.is_detailed && !paragraph.is_removed)
      details.push_back({paragraph.intent, paragraph.detail});
  p_lockfile.details = std::move(details);

  bool all_detailed = true;
  for (const auto &paragraph : result.paragraphs)
    all_detailed = all_detailed && paragraph.is_detailed &&
                   !paragraph.detail.is_refusal();
  if (!all_detailed) {
    result.detailed_source = render_source(p_source, result.paragraphs, false,
                                           result.detailed_lines);
    return result;
  }

  // 2. Translate.
  auto state = run_translation(result, p_lockfile, p_model, p_options, log);
  result.program = std::move(state.program);

  std::vector<english::lockfile::translation_entry> translations;
  for (const auto &paragraph : result.paragraphs) {
    used_model = used_model || paragraph.attempts > 0;
    if (paragraph.problems.empty() &&
        paragraph.detail.status == status_type::ok)
      translations.push_back({paragraph.steps(), paragraph.core});
  }
  // Keep what was learned even if some paragraphs still fail, but don't
  // forget earlier translations only because this run failed.
  if (result.succeeded() || used_model)
    p_lockfile.translations = std::move(translations);
  if (used_model && p_model != nullptr) {
    p_lockfile.model = p_model->name();
    p_lockfile.model_fingerprint = p_model->fingerprint();
  }

  result.detailed_source =
      render_source(p_source, result.paragraphs, result.succeeded(),
                    result.detailed_lines);
  return result;
}

} // namespace ovis
