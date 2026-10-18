// Measures the built-in model.
//
// `detail`: how often the detail pass's verdict (ok, note, too_vague,
// irrelevant) is the expected one, and what steps it writes. Used to tune
// the prompts; see eval/detail-cases.txt.
//
// `questions`: the phrase front end's questions (eval/questions.txt). Where
// the ranking puts the right reading, and how often the model picks it, how
// sure it is and how long it takes. `--no-model` measures the ranking only;
// `--compare-cpu` also asks on the CPU and compares the probabilities with
// the GPU's.
//
//   ovis_eval detail <cases.txt> [--model-path FILE] [--only N]
//   ovis_eval questions <cases.txt> [--model-path FILE] [--only N]
//             [--no-model] [--cpu] [--compare-cpu] [--threads N]

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

#include <ovis/english/language_model.hpp>
#include <ovis/english/prompt.hpp>
#include <ovis/phrase/front_end.hpp>

namespace {

using ovis::string_type;

struct detail_case {
  std::vector<string_type> expected;
  string_type paragraph;
};

auto read_cases(const std::filesystem::path &p_path)
    -> std::vector<detail_case> {
  std::ifstream file(p_path);
  if (!file)
    throw std::runtime_error("cannot read " + p_path.string());
  std::vector<detail_case> result;
  detail_case current;
  const auto finish = [&] {
    if (!current.expected.empty() && !current.paragraph.empty())
      result.push_back(current);
    current = {};
  };
  string_type line;
  while (std::getline(file, line)) {
    if (line.starts_with("#"))
      continue;
    if (line.empty()) {
      finish();
    } else if (line.starts_with("expect: ")) {
      std::stringstream statuses(line.substr(8));
      string_type status;
      while (std::getline(statuses, status, '|'))
        current.expected.push_back(status);
    } else {
      current.paragraph += (current.paragraph.empty() ? "" : "\n") + line;
    }
  }
  finish();
  return result;
}

auto run_detail(const std::vector<detail_case> &p_cases,
                ovis::english::language_model &p_model, int p_only) -> int {
  int correct = 0;
  int total = 0;
  for (std::size_t index = 0; index < p_cases.size(); ++index) {
    if (p_only > 0 && static_cast<int>(index) + 1 != p_only)
      continue;
    const auto &item = p_cases[index];
    const auto start = std::chrono::steady_clock::now();
    const auto reply = p_model.chat(
        ovis::english::detail_request(item.paragraph, {}),
        ovis::english::detail_grammar(
            ovis::english::may_be_note(item.paragraph)));
    const auto seconds = std::chrono::duration<double>(
                             std::chrono::steady_clock::now() - start)
                             .count();
    string_type got = "unreadable";
    try {
      got = string_type(ovis::english::status_name(
          ovis::english::parse_detail(reply).status));
    } catch (const std::exception &) {
    }
    const bool is_right =
        std::find(item.expected.begin(), item.expected.end(), got) !=
        item.expected.end();
    correct += is_right ? 1 : 0;
    ++total;
    std::cout << (is_right ? "PASS" : "FAIL") << " #" << index + 1
              << " expected " << item.expected.front() << ", got " << got
              << " (" << static_cast<int>(seconds) << "s)\n  "
              << item.paragraph << "\n";
    std::istringstream lines(reply);
    for (string_type line; std::getline(lines, line);)
      std::cout << "    | " << line << "\n";
    std::cout.flush();
  }
  std::cout << correct << " of " << total << " verdicts as expected.\n";
  return correct == total ? 0 : 1;
}

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

auto run_questions(const std::vector<question_case> &p_cases,
                   ovis::english::language_model *p_model,
                   ovis::english::language_model *p_cpu_model, int p_only)
    -> int {
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
    if (ranking.asked.empty()) {
      std::cout << "  FAIL no question asked\n";
      for (const auto &problem : unasked.problems)
        std::cout << "    " << problem << "\n";
      continue;
    }
    ++totals.questioned;
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
            << ", of " << readable << " cases with one.\n";
  if (p_model == nullptr)
    return totals.questioned == totals.cases ? 0 : 1;
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
  return totals.right == totals.cases ? 0 : 1;
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
    std::vector<string_type> positional;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
      if (arguments[index] == "--model-path" && index + 1 < arguments.size())
        model_path = arguments[++index];
      else if (arguments[index] == "--only" && index + 1 < arguments.size())
        only = std::stoi(arguments[++index]);
      else if (arguments[index] == "--threads" && index + 1 < arguments.size())
        threads = std::stoi(arguments[++index]);
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
        (positional[0] != "detail" && positional[0] != "questions")) {
      std::cerr << "usage: ovis_eval detail <cases.txt> [--model-path FILE] "
                   "[--only N]\n"
                   "       ovis_eval questions <cases.txt> [--model-path FILE] "
                   "[--only N] [--no-model] [--cpu] [--compare-cpu] "
                   "[--threads N]\n";
      return 2;
    }
    if (positional[0] == "questions" && no_model)
      return run_questions(read_question_cases(positional[1]), nullptr,
                           nullptr, only);
    const auto location = ovis::english::find_builtin_model(
        model_path, std::filesystem::path(p_arguments[0]));
    if (!location) {
      std::cerr << "ovis_eval: no model found; run `ovis model pull`.\n";
      return 2;
    }
    ovis::english::builtin_model model(
        {.location = *location, .use_gpu = !cpu, .threads = threads});
    if (positional[0] == "detail")
      return run_detail(read_cases(positional[1]), model, only);

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
    return run_questions(read_question_cases(positional[1]), &model,
                         cpu_model.get(), only);
  } catch (const std::exception &p_error) {
    std::cerr << "ovis_eval: " << p_error.what() << "\n";
    return 2;
  }
}
