#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
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

namespace ovis::phrase {

namespace {

// How one line of the program reads: its Ovis Core, or the problem it has
// (`unclear` with the readings offered, best first).
struct line_reading {
  string_type core;
  string_type kind;
  string_type message;
  std::vector<string_type> readings;

  friend auto operator==(const line_reading &, const line_reading &) -> bool = default;
};

// Per own (not locked) file, each line's reading.
auto line_readings(const std::vector<phrase_file> &p_files,
                   const program_result &p_result)
    -> std::vector<std::map<std::size_t, line_reading>> {
  std::vector<std::map<std::size_t, line_reading>> result(p_files.size());
  for (std::size_t index = 0; index < p_files.size(); ++index)
    if (!p_files[index].locked && index < p_result.core_lines.size())
      for (const auto &[line, core] : p_result.core_lines[index])
        result[index][line].core = core;
  for (const auto &problem : p_result.diagnostics) {
    if (problem.line == 0 || p_files[problem.file].locked)
      continue;
    auto &reading = result[problem.file][problem.line];
    // The first problem of a line says what it is.
    if (!reading.kind.empty())
      continue;
    reading.kind = problem.kind;
    reading.message = problem.message.substr(0, problem.message.find('\n'));
    for (const auto &fix : problem.fixes)
      reading.readings.push_back(fix.reading);
  }
  return result;
}

auto some_readings(const std::vector<string_type> &p_readings) -> string_type {
  string_type result;
  for (std::size_t index = 0; index < p_readings.size() && index < 3; ++index)
    result += (index == 0 ? "" : ", ") + p_readings[index];
  if (p_readings.size() > 3)
    result += " and " + std::to_string(p_readings.size() - 3) + " more";
  return result;
}

// What the dependency did to a line that reads (or asks) without it, or
// nothing when the line is the same.
auto change_of(const line_reading &p_without, const line_reading &p_with,
               const string_type &p_name, const string_type &p_suffix)
    -> string_type {
  if (p_without == p_with)
    return {};
  const bool asked_before = p_without.kind == "unclear";
  const bool asks = p_with.kind == "unclear";
  if (!p_with.kind.empty() && !asks)
    return "doesn't read now: " + p_with.message;
  if (!asked_before && asks)
    return "asks now, offering " + some_readings(p_with.readings);
  const auto how = p_with.core.find(p_suffix) != string_type::npos
                       ? "reads with " + p_name + "'s phrases now"
                       : string_type("reads another way now");
  if (asked_before && !asks)
    return how + ", without asking";
  if (!asks)
    return how;
  std::vector<string_type> added;
  for (const auto &reading : p_with.readings)
    if (std::ranges::find(p_without.readings, reading) == p_without.readings.end())
      added.push_back(reading);
  if (added.empty())
    return "asks with its readings in another order, " +
           some_readings(p_with.readings) + " first";
  return "asks with " + std::to_string(added.size()) + " more reading" +
         (added.size() == 1 ? "" : "s") + ": " + some_readings(added);
}

auto source_line(string_view_type p_text, std::size_t p_line) -> string_type {
  std::istringstream text{string_type(p_text)};
  string_type line;
  for (std::size_t at = 0; at < p_line && std::getline(text, line); ++at) {
  }
  const auto begin = line.find_first_not_of(' ');
  return begin == string_type::npos ? string_type() : line.substr(begin);
}

} // namespace

auto check_dependency_phrases(const std::vector<phrase_file> &p_files,
                              const std::vector<string_type> &p_names,
                              const front_end_options &p_options,
                              std::ostream &p_out) -> int {
  auto options = p_options;
  options.rewrite = false;
  // Lines as read, the same in both readings.
  options.sources_written = false;
  std::map<string_type, string_type> dependencies;
  for (const auto &file : p_files)
    if (!file.locked)
      dependencies.insert(file.aliases.begin(), file.aliases.end());
  auto names = p_names;
  if (names.empty())
    for (const auto &[name, id] : dependencies)
      names.push_back(name);
  if (names.empty()) {
    p_out << "The program uses no packages.\n";
    return 0;
  }
  // The model is never asked: a question goes to the author with its
  // readings, which is what is compared.
  const auto with =
      line_readings(p_files, compile_phrase_program(p_files, nullptr, options));
  int changed = 0;
  for (const auto &name : names) {
    if (!dependencies.contains(name))
      throw error("The program has no dependency called '" + name + "'.");
    auto files = p_files;
    // Out of sight: its `use` line names nothing, so no file sees it.
    for (auto &file : files)
      if (!file.locked)
        file.aliases.erase(name);
    const auto without =
        line_readings(files, compile_phrase_program(files, nullptr, options));
    const auto suffix = package_core_suffix(dependencies.at(name));
    std::size_t uses = 0;
    string_type report;
    std::size_t lines = 0;
    for (std::size_t index = 0; index < p_files.size(); ++index) {
      if (p_files[index].locked)
        continue;
      std::set<std::size_t> numbers;
      for (const auto &[line, reading] : with[index])
        numbers.insert(line);
      for (const auto &[line, reading] : without[index])
        numbers.insert(line);
      for (const auto line : numbers) {
        const auto before = without[index].contains(line) ? without[index].at(line)
                                                          : line_reading{};
        const auto after =
            with[index].contains(line) ? with[index].at(line) : line_reading{};
        // A line that doesn't read without the package is one of its uses
        // (or its `use` line), and so is one that asked without it and now
        // reads with its phrases: the author wrote it for them.
        const bool reads_with_it =
            after.kind.empty() && after.core.find(suffix) != string_type::npos;
        if ((!before.kind.empty() && before.kind != "unclear") ||
            (before.kind == "unclear" && reads_with_it)) {
          uses += before != after;
          continue;
        }
        const auto change = change_of(before, after, name, suffix);
        if (change.empty())
          continue;
        ++lines;
        const auto &file = p_files[index];
        report += "  " +
                  (file.name.empty() ? file.path.filename().string() : file.name) +
                  ", line " + std::to_string(line) + ": " +
                  source_line(file.text, line) + "\n    " + change + "\n";
      }
    }
    const auto counted = uses == 0 ? string_type()
                         : " (" + std::to_string(uses) + " line" +
                               (uses == 1 ? "" : "s") + " read with them)";
    if (lines == 0) {
      p_out << name << "'s phrases change none of the program's other lines"
            << counted << ".\n";
      continue;
    }
    changed = 1;
    p_out << name << "'s phrases change " << lines << " of the program's lines"
          << counted << ":\n" << report
          << "  Settle each with `ovis fix` (its @[meaning: …] line keeps it), or "
             "write it in other words.\n";
  }
  return changed;
}

} // namespace ovis::phrase
