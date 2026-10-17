#include <ovis/compiler.hpp>
#include <ovis/core/checker.hpp>
#include <ovis/core/elaborate.hpp>
#include <ovis/english/prompt.hpp>

namespace ovis {

namespace {

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

struct evaluation {
  core::program program;
  // The paragraph each unit of `program` came from.
  std::vector<std::size_t> owners;
};

// Parses, elaborates and checks everything translated so far, recording the
// problems and assumptions of each paragraph.
auto evaluate(std::vector<translated_paragraph> &p_paragraphs) -> evaluation {
  evaluation result;
  for (std::size_t index = 0; index < p_paragraphs.size(); ++index) {
    auto &paragraph = p_paragraphs[index];
    paragraph.problems.clear();
    paragraph.assumptions.clear();
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
  const auto describe = [](const core::diagnostic &p_diagnostic) {
    return "line " + std::to_string(p_diagnostic.location.line) + ": " +
           p_diagnostic.message;
  };
  auto notes = core::elaborate(result.program);
  for (const auto &problem : core::check(result.program, &notes))
    p_paragraphs[result.owners[problem.unit_index]].problems.push_back(
        describe(problem));
  for (const auto &note : notes)
    p_paragraphs[result.owners[note.unit_index]].assumptions.push_back(
        describe(note));
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

} // namespace

auto compilation::succeeded() const -> bool {
  for (const auto &paragraph : paragraphs)
    if (!paragraph.problems.empty())
      return false;
  return true;
}

auto compilation::report() const -> string_type {
  string_type result;
  for (const auto &paragraph : paragraphs) {
    if (paragraph.problems.empty())
      continue;
    result += "Paragraph at line " + std::to_string(paragraph.source.line) +
              ":\n" + indent(paragraph.source.text, "  | ") + "\n";
    if (paragraph.core.empty()) {
      result += "  has no translation. Run without --offline so a language "
                "model can translate it.\n\n";
      continue;
    }
    result += "  was translated" +
              string_type(paragraph.from_lockfile ? " (from the lockfile)"
                                                  : "") +
              " to:\n" + indent(paragraph.core, "  > ") + "\n  but:\n";
    for (const auto &problem : paragraph.problems)
      result += "  - " + problem + "\n";
    result += "\n";
  }
  return result;
}

auto compile(string_view_type p_source, english::lockfile &p_lockfile,
             english::language_model *p_model,
             const compile_options &p_options) -> compilation {
  const auto log = [&](const string_type &p_message) {
    if (p_options.log)
      p_options.log(p_message);
  };

  compilation result;
  for (auto &paragraph : english::split_paragraphs(p_source)) {
    translated_paragraph translated{.source = std::move(paragraph)};
    if (!p_options.retranslate) {
      if (auto core = p_lockfile.find(translated.source.text)) {
        translated.core = std::move(*core);
        translated.from_lockfile = true;
      }
    }
    result.paragraphs.push_back(std::move(translated));
  }

  auto state = evaluate(result.paragraphs);
  while (p_model != nullptr) {
    bool translated_any = false;
    for (std::size_t index = 0; index < result.paragraphs.size(); ++index) {
      auto &paragraph = result.paragraphs[index];
      if (paragraph.problems.empty() ||
          paragraph.attempts >= p_options.max_attempts)
        continue;

      auto request = english::translation_request(paragraph.source.text,
                                                  context_for(state, index));
      const bool is_repair = !paragraph.core.empty();
      if (is_repair)
        request = english::repair_request(std::move(request), paragraph.core,
                                          paragraph.problems);
      log((is_repair ? "Fixing" : "Translating") +
          string_type(" the paragraph at line ") +
          std::to_string(paragraph.source.line) + "...");

      paragraph.core = english::extract_core(p_model->chat(request));
      paragraph.from_lockfile = false;
      ++paragraph.attempts;
      translated_any = true;
      // Later paragraphs get to see what this one declared.
      state = evaluate(result.paragraphs);
    }
    if (!translated_any)
      break;
  }
  result.program = std::move(state.program);

  std::vector<english::lockfile::entry> entries;
  bool used_model = false;
  for (const auto &paragraph : result.paragraphs) {
    if (paragraph.problems.empty())
      entries.push_back(
          {paragraph.source.text, paragraph.core, paragraph.assumptions});
    used_model = used_model || paragraph.attempts > 0;
  }
  // Keep what was learned even if some paragraphs still fail, but don't
  // forget earlier translations only because this run failed.
  if (result.succeeded() || used_model) {
    p_lockfile.entries = std::move(entries);
    if (used_model)
      p_lockfile.model = p_model->name();
  }
  return result;
}

} // namespace ovis
