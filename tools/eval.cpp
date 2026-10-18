// Measures the built-in model.
//
// `questions`: the phrase front end's questions (eval/questions.txt). Where
// the ranking puts the right reading, and how often the model picks it, how
// sure it is and how long it takes. `--no-model` measures the ranking only;
// `--compare-cpu` also asks on the CPU and compares the probabilities with
// the GPU's. `--min-first N` and `--min-shown N` fail the run when fewer
// cases have the right reading ranked first, or shown in the first question:
// CI keeps the ranking from getting worse as the library grows.
//
//   ovis_eval questions <cases.txt> [--model-path FILE] [--only N]
//             [--no-model] [--cpu] [--compare-cpu] [--threads N]
//             [--min-first N] [--min-shown N]
//   ovis_eval programs <folder> [--model-path FILE] [--no-model] [--cpu]
//             [--threads N]
//
// `programs`: the programs of eval/programs (M1.14), each with the readings
// its author meant in `# expect` lines: questions per program, how each was
// settled, whether that was the reading meant, and the time.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>

#include <ovis/english/language_model.hpp>
#include <ovis/phrase/front_end.hpp>

namespace {

using ovis::string_type;

struct question_case {
  // A reading, or "none" when none fits.
  string_type expected;
  string_type program;
};

auto read_question_cases(const std::filesystem::path &p_path)
    -> std::vector<question_case> {
  std::ifstream file(p_path);
  if (!file)
    throw std::runtime_error("cannot read " + p_path.string());
  std::vector<question_case> result;
  question_case current;
  const auto finish = [&] {
    if (!current.expected.empty() && !current.program.empty())
      result.push_back(current);
    current = {};
  };
  string_type line;
  while (std::getline(file, line)) {
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

// One question as the front end asked it: the numbered readings (the last
// is "none of these") and the answer.
struct asked_question {
  std::vector<string_type> options;
  std::vector<double> probabilities;
  double milliseconds = 0;
};

// Passes the front end's questions on to `inner`, recording them. Without
// `inner` it answers "none of these", so the front end asks only its first
// round, which holds every candidate in ranking order.
class recording_model final : public ovis::english::language_model {
public:
  ovis::english::language_model *inner = nullptr;
  std::vector<asked_question> asked;

  auto name() const -> string_type override { return "recording"; }
  auto chat(const std::vector<ovis::english::chat_message> &,
            ovis::string_view_type) -> string_type override {
    throw std::runtime_error("the phrase front end only chooses");
  }
  auto choose(const std::vector<ovis::english::chat_message> &p_messages,
              std::size_t p_options) -> std::vector<double> override {
    asked_question question;
    std::istringstream lines(p_messages.back().content);
    for (string_type line; std::getline(lines, line);) {
      const auto dot = line.find(". ");
      if (dot != string_type::npos && dot > 0 &&
          line.find_first_not_of("0123456789") == dot)
        question.options.push_back(line.substr(dot + 2));
    }
    const auto start = std::chrono::steady_clock::now();
    if (inner != nullptr) {
      question.probabilities = inner->choose(p_messages, p_options);
    } else {
      question.probabilities.assign(p_options, 0);
      question.probabilities.back() = 1;
    }
    question.milliseconds = std::chrono::duration<double, std::milli>(
                                std::chrono::steady_clock::now() - start)
                                .count();
    asked.push_back(question);
    return asked.back().probabilities;
  }
};

// The reading of the first new `Read as:` line, or "none".
auto decided_reading(const string_type &p_source) -> string_type {
  const auto at = p_source.find("Read as: \"");
  if (at == string_type::npos)
    return "none";
  const auto is = p_source.find("\" is ", at + 10);
  const auto end = p_source.find('\n', is);
  return p_source.substr(is + 5, end - is - 5);
}

auto percent(double p_value) -> string_type {
  return std::to_string(static_cast<int>(std::lround(p_value * 100))) + "%";
}

// The best option of a question, and how far it is ahead of the second.
auto best_of(const asked_question &p_question)
    -> std::pair<std::size_t, double> {
  std::vector<double> sorted = p_question.probabilities;
  std::ranges::sort(sorted, std::ranges::greater{});
  const auto best = static_cast<std::size_t>(
      std::ranges::max_element(p_question.probabilities) -
      p_question.probabilities.begin());
  return {best, sorted[0] - (sorted.size() > 1 ? sorted[1] : 0)};
}

struct question_totals {
  int cases = 0;
  int questioned = 0;
  // Settled by a rule or the ranking shortcut, and of those right.
  int shortcuts = 0;
  int shortcuts_right = 0;
  int ranked_first = 0;
  int ranked_in_question = 0;
  int confident = 0;
  int confident_right = 0;
  int guess_right = 0;
  int right = 0;
  int asks = 0;
  double ask_ms = 0;
  // The last question of each case: its best reading's probability and
  // margin, and whether that reading is right; for other thresholds.
  struct final_answer {
    double probability;
    double margin;
    bool is_reading;
    bool right;
  };
  std::vector<final_answer> finals;
  // --compare-cpu: questions asked on both, the largest difference in a
  // probability, and the cases whose outcome differs.
  int compared = 0;
  double largest_difference = 0;
  int disagreements = 0;
};

// The fewest cases that may have the right reading ranked first, and shown
// in the first question.
struct ranking_floor {
  int first = 0;
  int shown = 0;
};

auto run_questions(const std::vector<question_case> &p_cases,
                   ovis::english::language_model *p_model,
                   ovis::english::language_model *p_cpu_model, int p_only,
                   ranking_floor p_floor) -> int {
  ovis::phrase::front_end_options options;
  options.model_budget_seconds = 1e9;
  question_totals totals;
  for (std::size_t index = 0; index < p_cases.size(); ++index) {
    if (p_only > 0 && static_cast<int>(index) + 1 != p_only)
      continue;
    const auto &item = p_cases[index];
    ++totals.cases;
    std::cout << "#" << index + 1 << " expect " << item.expected << "\n";

    recording_model ranking;
    const auto unasked = ovis::phrase::compile_phrases(item.program, &ranking, options);
    if (unasked.timing.questions == 0) {
      std::cout << "  FAIL no question asked\n";
      for (const auto &problem : unasked.problems)
        std::cout << "    " << problem << "\n";
      continue;
    }
    ++totals.questioned;
    if (ranking.asked.empty()) {
      // Settled by a rule or the ranking shortcut: no model needed.
      const auto answer = decided_reading(unasked.source);
      const bool right = answer == item.expected;
      ++totals.shortcuts;
      totals.shortcuts_right += right;
      // The shortcut takes the first-ranked reading.
      totals.ranked_first += right;
      totals.ranked_in_question += right;
      totals.right += right;
      std::cout << "  " << (right ? "PASS" : "FAIL") << " settled without the "
                << "model: " << answer << "\n";
      continue;
    }
    std::vector<string_type> candidates;
    for (const auto &question : ranking.asked)
      candidates.insert(candidates.end(), question.options.begin(),
                        question.options.end() - 1);
    const auto found = std::ranges::find(candidates, item.expected);
    const auto rank = found - candidates.begin() + 1;
    if (item.expected != "none") {
      totals.ranked_first += rank == 1;
      totals.ranked_in_question +=
          found != candidates.end() &&
          rank < static_cast<long>(ovis::english::max_choice_options);
    }
    std::cout << "  ranking: "
              << (item.expected == "none" ? string_type("-")
                  : found == candidates.end()
                      ? string_type("not a candidate")
                      : "#" + std::to_string(rank))
              << " of " << candidates.size() << ":";
    for (std::size_t shown = 0; shown < candidates.size() && shown < 4; ++shown)
      std::cout << (shown == 0 ? " " : ", ") << candidates[shown];
    std::cout << "\n";
    if (p_model == nullptr) {
      totals.right += item.expected == "none" || rank == 1;
      continue;
    }

    const auto ask = [&](ovis::english::language_model *p_inner) {
      recording_model recorder;
      recorder.inner = p_inner;
      const auto result =
          ovis::phrase::compile_phrases(item.program, &recorder, options);
      return std::pair{decided_reading(result.source), recorder.asked};
    };
    const auto [answer, asked] = ask(p_model);
    for (const auto &question : asked) {
      ++totals.asks;
      totals.ask_ms += question.milliseconds;
    }
    const auto &last = asked.back();
    const auto [best, margin] = best_of(last);
    const auto guess = best + 1 < last.probabilities.size()
                           ? last.options[best]
                           : string_type("none");
    const bool confident = answer != "none";
    const bool right = answer == item.expected;
    totals.confident += confident;
    totals.confident_right += confident && right;
    totals.guess_right += guess == item.expected;
    totals.finals.push_back({last.probabilities[best], margin,
                             best + 1 < last.probabilities.size(),
                             guess == item.expected});
    totals.right += right;
    std::cout << "  " << (right ? "PASS" : "FAIL") << " "
              << (confident ? "picked " + answer
                            : "unsure, best " + guess + " " +
                                  percent(last.probabilities[best]))
              << " (margin " << percent(margin) << ", " << asked.size()
              << " asked, " << static_cast<int>(last.milliseconds) << " ms)\n";

    if (p_cpu_model != nullptr) {
      const auto [cpu_answer, cpu_asked] = ask(p_cpu_model);
      for (std::size_t at = 0; at < asked.size() && at < cpu_asked.size(); ++at) {
        if (asked[at].options != cpu_asked[at].options)
          break;
        ++totals.compared;
        for (std::size_t option = 0; option < asked[at].probabilities.size(); ++option)
          totals.largest_difference =
              std::max(totals.largest_difference,
                       std::abs(asked[at].probabilities[option] -
                                cpu_asked[at].probabilities[option]));
      }
      if (cpu_answer != answer) {
        ++totals.disagreements;
        std::cout << "  the CPU decided " << cpu_answer << "\n";
      }
    }
    std::cout.flush();
  }

  const auto readable = totals.cases - static_cast<int>(std::ranges::count_if(
      p_cases, [](const question_case &p_case) { return p_case.expected == "none"; }));
  std::cout << "\n" << totals.questioned << " of " << totals.cases
            << " cases asked a question.\n"
            << "Ranking: the right reading first in " << totals.ranked_first
            << ", in the first question (top "
            << ovis::english::max_choice_options - 1 << ") in " << totals.ranked_in_question
            << ", of " << readable << " cases with one.\n"
            << "Settled without the model (rules, ranking shortcut): "
            << totals.shortcuts << ", right " << totals.shortcuts_right
            << ".\n";
  // The floor is for the whole set, not one case of it.
  const bool kept_floor = p_only > 0 ||
                          (totals.ranked_first >= p_floor.first &&
                           totals.ranked_in_question >= p_floor.shown);
  if (!kept_floor)
    std::cout << "FAIL the ranking got worse: at least " << p_floor.first
              << " first and " << p_floor.shown << " in the first question "
              << "are expected. If a change makes it better, raise the floor.\n";
  if (p_model == nullptr)
    return totals.questioned == totals.cases && kept_floor ? 0 : 1;
  std::cout << "Model: " << totals.right << " of " << totals.cases
            << " right. Confident in " << totals.confident << ", of those right "
            << totals.confident_right
            << (totals.confident > 0
                    ? " (" + percent(double(totals.confident_right) / totals.confident) + ")"
                    : string_type())
            << ". Best guess right in " << totals.guess_right << ".\n"
            << "Time: " << totals.asks << " questions asked, "
            << std::fixed << std::setprecision(0)
            << (totals.asks > 0 ? totals.ask_ms / totals.asks : 0)
            << " ms each on average.\n";
  // The front end takes p >= 0.6 and a margin >= 0.3; M1.14 tunes them.
  std::cout << "Picks at other thresholds (p, margin: picked, right):";
  for (const auto &[probability, margin] :
       std::vector<std::pair<double, double>>{
           {0.4, 0.1}, {0.5, 0.2}, {0.6, 0.3}, {0.7, 0.4}, {0.8, 0.5}}) {
    int picked = 0;
    int right = 0;
    for (const auto &answer : totals.finals)
      if (answer.is_reading && answer.probability >= probability &&
          answer.margin >= margin) {
        ++picked;
        right += answer.right;
      }
    std::cout << " " << percent(probability) << " " << percent(margin) << ": "
              << picked << " " << right << ";";
  }
  std::cout << "\n";
  if (p_cpu_model != nullptr)
    std::cout << "CPU: " << totals.compared << " questions compared, largest "
              << "difference in a probability " << std::setprecision(4)
              << totals.largest_difference << "; " << totals.disagreements
              << " cases decided differently.\n";
  return totals.right == totals.cases && kept_floor ? 0 : 1;
}

// A program of eval/programs and the readings its author meant.
struct labelled_program {
  string_type name;
  string_type text;
  // Span → the readings meant ("none": the author should be asked).
  std::map<string_type, std::vector<string_type>> meant;
};

auto read_programs(const std::filesystem::path &p_folder)
    -> std::vector<labelled_program> {
  std::vector<std::filesystem::path> paths;
  for (const auto &entry : std::filesystem::directory_iterator(p_folder))
    if (entry.path().extension() == ".ovis")
      paths.push_back(entry.path());
  std::ranges::sort(paths);
  std::vector<labelled_program> result;
  for (const auto &path : paths) {
    std::ifstream file(path);
    std::stringstream text;
    text << file.rdbuf();
    labelled_program program{path.stem().string(), text.str()};
    std::istringstream lines(program.text);
    for (string_type line; std::getline(lines, line);) {
      // # expect "SPAN" is READING | READING
      if (!line.starts_with("# expect \""))
        continue;
      const auto is = line.find("\" is ", 10);
      if (is == string_type::npos)
        continue;
      auto &readings = program.meant[line.substr(10, is - 10)];
      std::istringstream choices(line.substr(is + 5));
      for (string_type choice; std::getline(choices, choice, '|');) {
        while (choice.starts_with(' '))
          choice.erase(0, 1);
        while (choice.ends_with(' '))
          choice.pop_back();
        readings.push_back(choice);
      }
    }
    result.push_back(std::move(program));
  }
  return result;
}

// The new `Read as:` decisions of a built source: span → reading.
auto decisions_of(const string_type &p_source)
    -> std::vector<std::pair<string_type, string_type>> {
  std::vector<std::pair<string_type, string_type>> result;
  std::istringstream lines(p_source);
  for (string_type line; std::getline(lines, line);) {
    const auto at = line.find("Read as: \"");
    if (at == string_type::npos)
      continue;
    const auto is = line.find("\" is ", at + 10);
    result.emplace_back(line.substr(at + 10, is - at - 10), line.substr(is + 5));
  }
  return result;
}

// The spans a failed build left to the author.
auto unsettled_spans(const std::vector<string_type> &p_problems)
    -> std::vector<string_type> {
  std::vector<string_type> result;
  for (const auto &problem : p_problems) {
    const auto open = problem.find(": \"");
    const auto close = problem.find("\" is unclear");
    if (open != string_type::npos && close != string_type::npos && close > open)
      result.push_back(problem.substr(open + 3, close - open - 3));
  }
  return result;
}

auto run_programs(const std::vector<labelled_program> &p_programs,
                  ovis::english::language_model *p_model) -> int {
  ovis::phrase::front_end_options options;
  options.model_budget_seconds = 1e9;
  std::vector<double> questions;
  std::vector<double> times;
  std::size_t by_rule = 0, by_ranking = 0, by_model = 0, to_author = 0;
  std::size_t model_right = 0, rules_right = 0, author_right = 0;
  std::size_t unlabelled = 0, built = 0;
  for (const auto &program : p_programs) {
    const auto start = std::chrono::steady_clock::now();
    const auto result = ovis::phrase::compile_phrases(program.text, p_model, options);
    const auto milliseconds = std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() - start)
                                  .count();
    const auto &timing = result.timing;
    questions.push_back(double(timing.questions));
    times.push_back(milliseconds);
    by_rule += timing.by_rule;
    by_ranking += timing.by_ranking;
    by_model += timing.by_model;
    built += result.succeeded();
    std::cout << program.name << ": " << timing.questions << " unclear ("
              << timing.by_rule << " rule, " << timing.by_ranking
              << " ranking, " << timing.by_model << " model), "
              << timing.asked << " asked, " << int(milliseconds) << " ms"
              << (result.succeeded() ? "" : ", not built") << "\n";
    // Which decisions came from the model isn't recorded per line: those the
    // rules and ranking make are made again without it.
    const auto offline = ovis::phrase::compile_phrases(program.text, nullptr);
    std::set<std::pair<string_type, string_type>> without_model;
    for (const auto &decision : decisions_of(offline.source))
      without_model.insert(decision);
    for (const auto &[span, reading] : decisions_of(result.source)) {
      const auto found = program.meant.find(span);
      const auto by = without_model.contains({span, reading}) ? "rule/ranking"
                                                              : "model";
      if (found == program.meant.end()) {
        ++unlabelled;
        std::cout << "  ?    \"" << span << "\" is " << reading << " (" << by
                  << ", not labelled)\n";
        continue;
      }
      const bool right = std::ranges::find(found->second, reading) !=
                         found->second.end();
      (string_type(by) == "model" ? model_right : rules_right) += right;
      std::cout << "  " << (right ? "PASS" : "FAIL") << " \"" << span
                << "\" is " << reading << " (" << by << ")"
                << (right ? "" : ", meant " + found->second.front()) << "\n";
    }
    for (const auto &span : unsettled_spans(result.problems)) {
      ++to_author;
      const auto found = program.meant.find(span);
      const bool right = found != program.meant.end() &&
                         found->second.front() == "none";
      author_right += right;
      std::cout << "  " << (right ? "PASS" : "FAIL") << " \"" << span
                << "\" left to the author"
                << (found == program.meant.end()
                        ? string_type(" (not labelled)")
                        : right ? string_type()
                                : ", meant " + found->second.front())
                << "\n";
    }
    for (const auto &problem : result.problems)
      if (problem.find("\" is unclear") == string_type::npos)
        std::cout << "  problem: " << problem << "\n";
    std::cout.flush();
  }
  const auto median = [](std::vector<double> p_values) {
    std::ranges::sort(p_values);
    return p_values.empty() ? 0.0 : p_values[p_values.size() / 2];
  };
  std::cout << "\n"
            << p_programs.size() << " programs, " << built << " built. "
            << "Questions per program: median " << median(questions)
            << ", most " << std::ranges::max(questions) << ".\n"
            << "Settled by rules " << by_rule << ", by the ranking shortcut "
            << by_ranking << " (" << rules_right << " of these right), by the model "
            << by_model << " (" << model_right << " right), left to the author "
            << to_author << " (" << author_right << " meant to be).\n"
            << "Not labelled: " << unlabelled << ".\n"
            << "Time per program: median " << int(median(times)) << " ms, most "
            << int(std::ranges::max(times)) << " ms.\n";
  return 0;
}

} // namespace

auto main(int p_count, char **p_arguments) -> int {
  try {
    std::vector<string_type> arguments(p_arguments + 1,
                                       p_arguments + p_count);
    std::optional<std::filesystem::path> model_path;
    int only = 0;
    int threads = 0;
    bool no_model = false;
    bool cpu = false;
    bool compare_cpu = false;
    ranking_floor floor;
    std::vector<string_type> positional;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
      if (arguments[index] == "--model-path" && index + 1 < arguments.size())
        model_path = arguments[++index];
      else if (arguments[index] == "--only" && index + 1 < arguments.size())
        only = std::stoi(arguments[++index]);
      else if (arguments[index] == "--threads" && index + 1 < arguments.size())
        threads = std::stoi(arguments[++index]);
      else if (arguments[index] == "--min-first" && index + 1 < arguments.size())
        floor.first = std::stoi(arguments[++index]);
      else if (arguments[index] == "--min-shown" && index + 1 < arguments.size())
        floor.shown = std::stoi(arguments[++index]);
      else if (arguments[index] == "--no-model")
        no_model = true;
      else if (arguments[index] == "--cpu")
        cpu = true;
      else if (arguments[index] == "--compare-cpu")
        compare_cpu = true;
      else
        positional.push_back(arguments[index]);
    }
    if (positional.size() != 2 ||
        (positional[0] != "questions" && positional[0] != "programs")) {
      std::cerr << "usage: ovis_eval questions <cases.txt> [--model-path FILE] "
                   "[--only N] [--no-model] [--cpu] [--compare-cpu] "
                   "[--threads N] [--min-first N] [--min-shown N]\n"
                   "       ovis_eval programs <folder> [--model-path FILE] "
                   "[--no-model] [--cpu] [--threads N]\n";
      return 2;
    }
    if (positional[0] == "programs" && no_model)
      return run_programs(read_programs(positional[1]), nullptr);
    if (positional[0] == "questions" && no_model)
      return run_questions(read_question_cases(positional[1]), nullptr,
                           nullptr, only, floor);
    const auto location = ovis::english::find_builtin_model(
        model_path, std::filesystem::path(p_arguments[0]));
    if (!location) {
      std::cerr << "ovis_eval: no model found; run `ovis model pull`.\n";
      return 2;
    }
    ovis::english::builtin_model model(
        {.location = *location, .use_gpu = !cpu, .threads = threads});

    std::cout << "Model: " << location->file.filename().string() << "\n";
    // Loading isn't part of a question's time.
    const auto load = [](ovis::english::language_model &p_model) {
      const auto start = std::chrono::steady_clock::now();
      p_model.choose({{"user", "1. yes\n2. no\nAnswer:"}}, 2);
      return std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                           start)
          .count();
    };
    std::cout << "Loaded in " << std::fixed << std::setprecision(1)
              << load(model) << " s.\n\n";
    std::unique_ptr<ovis::english::builtin_model> cpu_model;
    if (compare_cpu) {
      cpu_model = std::make_unique<ovis::english::builtin_model>(
          ovis::english::builtin_options{
              .location = *location, .use_gpu = false, .threads = threads});
      load(*cpu_model);
    }
    std::cout.unsetf(std::ios::fixed);
    if (positional[0] == "programs")
      return run_programs(read_programs(positional[1]), &model);
    return run_questions(read_question_cases(positional[1]), &model,
                         cpu_model.get(), only, floor);
  } catch (const std::exception &p_error) {
    std::cerr << "ovis_eval: " << p_error.what() << "\n";
    return 2;
  }
}
