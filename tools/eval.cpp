// Measures the built-in model on the detail pass: how often its verdict
// (ok, note, too_vague, irrelevant) is the expected one, and what steps it
// writes. Used to tune the prompts; see eval/detail-cases.txt.
//
//   ovis_eval detail <cases.txt> [--model-path FILE] [--only N]

#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>

#include <ovis/english/language_model.hpp>
#include <ovis/english/prompt.hpp>

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

} // namespace

auto main(int p_count, char **p_arguments) -> int {
  try {
    std::vector<string_type> arguments(p_arguments + 1,
                                       p_arguments + p_count);
    std::optional<std::filesystem::path> model_path;
    int only = 0;
    std::vector<string_type> positional;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
      if (arguments[index] == "--model-path" && index + 1 < arguments.size())
        model_path = arguments[++index];
      else if (arguments[index] == "--only" && index + 1 < arguments.size())
        only = std::stoi(arguments[++index]);
      else
        positional.push_back(arguments[index]);
    }
    if (positional.size() != 2 || positional[0] != "detail") {
      std::cerr << "usage: ovis_eval detail <cases.txt> [--model-path FILE] "
                   "[--only N]\n";
      return 2;
    }
    const auto location = ovis::english::find_builtin_model(
        model_path, std::filesystem::path(p_arguments[0]));
    if (!location) {
      std::cerr << "ovis_eval: no model found; run `ovis model pull`.\n";
      return 2;
    }
    ovis::english::builtin_model model({.location = *location});
    return run_detail(read_cases(positional[1]), model, only);
  } catch (const std::exception &p_error) {
    std::cerr << "ovis_eval: " << p_error.what() << "\n";
    return 2;
  }
}
