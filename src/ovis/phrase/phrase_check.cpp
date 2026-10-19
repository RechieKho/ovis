#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>

#include <ovis/english/language_model.hpp>
#include <ovis/phrase/front_end.hpp>
#include <ovis/phrase/phrase_check.hpp>

namespace ovis::phrase {

namespace {

// Answers every question "none of these", so the front end asks only its
// first round, which holds every candidate in ranking order; keeps the
// options of each question.
class ranking_model final : public english::language_model {
public:
  std::vector<std::vector<string_type>> asked;

  auto name() const -> string_type override { return "ranking"; }
  auto embed(const std::vector<string_type> &)
      -> std::vector<std::vector<float>> override {
    return {};
  }
  auto chat(const std::vector<english::chat_message> &, string_view_type)
      -> string_type override {
    throw error("the phrase front end only chooses");
  }
  auto choose(const std::vector<english::chat_message> &p_messages,
              std::size_t p_options) -> std::vector<double> override {
    std::vector<string_type> options;
    std::istringstream text(p_messages.back().content);
    for (string_type line; std::getline(text, line);) {
      const auto dot = line.find(". ");
      if (dot != string_type::npos && dot > 0 &&
          line.find_first_not_of("0123456789") == dot)
        options.push_back(line.substr(dot + 2));
    }
    asked.push_back(std::move(options));
    std::vector<double> answer(p_options, 0);
    answer.back() = 1;
    return answer;
  }
};

// The reading of the build's first decision, or "none".
auto decided_reading(const front_end_result &p_result) -> string_type {
  return p_result.decisions.empty() ? "none" : p_result.decisions.front().second;
}

// Where one case's right reading ranks, read by the ranking alone.
struct case_ranking {
  bool asked = false;
  // 0 when it is not among the candidates (or the case expects none).
  long rank = 0;
  std::size_t candidates = 0;

  auto is_first() const -> bool { return rank == 1; }
  auto is_shown() const -> bool {
    return rank > 0 && rank < static_cast<long>(english::max_choice_options);
  }
};

auto rank_case(const phrase_case &p_case, const string_type &p_prefix)
    -> case_ranking {
  front_end_options options;
  options.rewrite = false;
  ranking_model ranking;
  const auto result =
      compile_phrases(p_prefix + p_case.program, &ranking, options);
  case_ranking found{.asked = result.timing.questions > 0};
  if (!found.asked)
    return found;
  if (ranking.asked.empty()) {
    // Settled by a rule or the ranking shortcut, which takes the first.
    found.candidates = 1;
    found.rank = decided_reading(result) == p_case.expected ? 1 : 0;
    return found;
  }
  std::vector<string_type> candidates;
  for (const auto &options_asked : ranking.asked)
    candidates.insert(candidates.end(), options_asked.begin(),
                      options_asked.end() - 1);
  found.candidates = candidates.size();
  const auto at = std::ranges::find(candidates, p_case.expected);
  if (p_case.expected != "none" && at != candidates.end())
    found.rank = at - candidates.begin() + 1;
  return found;
}

auto describe(const case_ranking &p_ranking) -> string_type {
  if (!p_ranking.asked)
    return "no question";
  if (p_ranking.rank == 0)
    return "not shown (" + std::to_string(p_ranking.candidates) + " readings)";
  return "#" + std::to_string(p_ranking.rank) + " of " +
         std::to_string(p_ranking.candidates);
}

} // namespace

auto parse_phrase_cases(string_view_type p_text) -> std::vector<phrase_case> {
  std::vector<phrase_case> result;
  phrase_case current;
  const auto finish = [&] {
    if (!current.expected.empty() && !current.program.empty())
      result.push_back(current);
    current = {};
  };
  std::istringstream input{string_type(p_text)};
  for (string_type line; std::getline(input, line);) {
    if (current.expected.empty() && line.starts_with("#"))
      continue;
    if (line.empty())
      finish();
    else if (line.starts_with("expect: "))
      current.expected = line.substr(8);
    else
      current.program += line + "\n";
  }
  finish();
  return result;
}

auto read_phrase_definitions(const std::filesystem::path &p_path) -> string_type {
  std::vector<std::filesystem::path> files;
  if (std::filesystem::is_directory(p_path)) {
    for (const auto &entry : std::filesystem::recursive_directory_iterator(p_path))
      if (entry.path().extension() == ".ovis")
        files.push_back(entry.path());
    std::ranges::sort(files);
  } else {
    files.push_back(p_path);
  }
  string_type result;
  for (const auto &path : files) {
    std::ifstream file(path);
    if (!file)
      throw error("cannot read " + path.string());
    const bool is_helper = std::filesystem::is_directory(p_path) &&
                           path.filename() != "main.ovis";
    bool in_phrase = false;
    for (string_type line; std::getline(file, line);) {
      if (is_helper) {
        result += line + "\n";
        continue;
      }
      const bool is_top_level =
          !line.empty() && line.front() != ' ' && line.front() != '#';
      if (is_top_level)
        in_phrase = line.starts_with("to ");
      if (in_phrase || (is_top_level && line.starts_with("use ")))
        result += line + "\n";
    }
    result += "\n";
  }
  return result;
}

auto check_phrases(const std::vector<phrase_case> &p_cases,
                   const string_type &p_definitions, std::ostream &p_out)
    -> int {
  // The phrases must read on their own, or their questions would be counted
  // as the cases'.
  {
    ranking_model recorder;
    const auto alone =
        compile_phrases(p_definitions, &recorder, front_end_options{});
    if (alone.timing.questions > 0 || !alone.problems.empty()) {
      p_out << "The program's phrases don't read on their own:\n";
      for (const auto &problem : alone.problems)
        p_out << "  " << problem << "\n";
      if (alone.problems.empty())
        p_out << "  a line in them asks a question; build the program first "
                 "so its @[meaning: …] lines settle it.\n";
      return 2;
    }
  }

  int worse = 0, better = 0;
  const auto readable = std::ranges::count_if(
      p_cases, [](const phrase_case &p_case) { return p_case.expected != "none"; });
  int first_before = 0, first_after = 0, shown_before = 0, shown_after = 0;
  for (std::size_t index = 0; index < p_cases.size(); ++index) {
    const auto &item = p_cases[index];
    const auto before = rank_case(item, "");
    const auto after = rank_case(item, p_definitions + "\n");
    first_before += before.is_first();
    first_after += after.is_first();
    shown_before += before.is_shown();
    shown_after += after.is_shown();

    string_type verdict;
    if (before.asked && !after.asked)
      verdict = "WORSE now reads one way without asking, probably as one of "
                "the program's phrases";
    else if ((before.is_first() && !after.is_first()) ||
             (before.is_shown() && !after.is_shown()))
      verdict = "WORSE";
    else if (after.rank > 0 && (before.rank == 0 || after.rank < before.rank))
      verdict = "better";
    if (verdict.empty())
      continue;
    (verdict.starts_with("WORSE") ? worse : better) += 1;
    p_out << "#" << index + 1 << " expect " << item.expected << ": "
          << describe(before) << " -> " << describe(after) << "  " << verdict
          << "\n";
  }
  p_out << "\nWith the program's phrases: the right reading first in "
        << first_before << " -> " << first_after
        << ", shown in the first question in " << shown_before << " -> "
        << shown_after << ", of " << readable << " cases with one.\n"
        << worse << " cases worse, " << better << " better.\n";
  return worse > 0 ? 1 : 0;
}

} // namespace ovis::phrase
