#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <filesystem>
#include <map>
#include <sstream>

#include <ovis/phrase/front_end.hpp>

using Catch::Matchers::ContainsSubstring;

namespace {

// Says "none of these" to every question, so the front end shows every
// candidate in its first round, and remembers them in order. Rewrites
// nothing.
class listing_model final : public ovis::english::language_model {
public:
  std::vector<std::string> options;
  std::size_t questions = 0;

  auto name() const -> std::string override { return "listing"; }
  auto chat(const std::vector<ovis::english::chat_message> &,
            std::string_view) -> std::string override {
    return {};
  }
  auto choose(const std::vector<ovis::english::chat_message> &p_messages,
              std::size_t p_options) -> std::vector<double> override {
    ++questions;
    std::istringstream lines(p_messages.back().content);
    for (std::string line; std::getline(lines, line);) {
      const auto dot = line.find(". ");
      if (dot != std::string::npos && dot > 0 &&
          line.find_first_not_of("0123456789") == dot &&
          line.substr(dot + 2) != "none of these")
        options.push_back(line.substr(dot + 2));
    }
    std::vector<double> result(p_options, 0);
    result.back() = 1;
    return result;
  }
};

// The candidates the line's question offers, in order.
auto candidates(const std::string &p_source) -> std::vector<std::string> {
  listing_model model;
  ovis::phrase::compile_phrases(p_source, &model);
  return model.options;
}

// How the program reads with no model: its source with `Read as:` lines.
auto settled(const std::string &p_source) -> ovis::phrase::front_end_result {
  return ovis::phrase::compile_phrases(p_source, nullptr);
}

using strings = std::vector<std::string>;

} // namespace

TEST_CASE("Offer the readings of words no phrase reads.", "[phrase][question]") {
  const std::string numbers = "numbers = [3, 1, 2]\n";
  REQUIRE(candidates(numbers + "x = mean of numbers\n") ==
          strings{"length of numbers", "first item of numbers",
                  "last item of numbers", "sum of numbers",
                  "average of numbers", "smallest of numbers",
                  "largest of numbers", "numbers as text", "numbers is empty"});
  // Only phrases whose slots take a text; those sharing a word ("in")
  // first.
  const auto text = candidates("name = \"Ann\"\nx = name in green\n");
  REQUIRE(text.size() == 18);
  REQUIRE(text[1] == "name in capitals");
  REQUIRE(std::ranges::find(text, "name is empty") != text.end());
  REQUIRE(std::ranges::find(text, "length of numbers") == text.end());
  // Statements are offered statements; `show`, which takes any values, only
  // when some word of the line is like its own.
  REQUIRE(candidates(numbers + "put 5 into numbers\n") ==
          strings{"add 5 to numbers"});
  REQUIRE(candidates(numbers + "showing 5, numbers\n") ==
          strings{"show 5, numbers", "show numbers, 5", "add 5 to numbers"});
  // Several readings that type-check.
  REQUIRE(candidates("x = 9\nshow square root of x + 7\n") ==
          strings{"show ((square root of x) + 7)", "show square root of (x + 7)"});
  // Arguments in another order than written rank lower among equals, but
  // are never cut for it.
  const auto divides = candidates("n = 12\nif 3 goes into n:\n    show n\n");
  REQUIRE(std::ranges::find(divides, "n is divisible by 3") != divides.end());
  // A minus belongs to its number.
  const auto distance = candidates("x = how far -5 is from zero\n");
  REQUIRE(std::ranges::find(distance, "absolute value of (-5)") != distance.end());
  REQUIRE(std::ranges::find(distance, "5 is positive") == distance.end());
}

TEST_CASE("Offer the names that fit an unknown word.", "[phrase][question]") {
  // Two names are as close to `cnt`: the model or the author decides, the
  // most recently set first.
  REQUIRE(candidates("cat = 1\ncot = 2\nshow cnt + 1\n") ==
          strings{"cot", "cat"});
  // Only names with which the line reads: a list can't be added to 1, so
  // `it` is the one number.
  REQUIRE_THAT(
      ovis::phrase::compile_phrases(
          "ages = [1]\nsizes = [2]\ncount = 3\nshow 1 + it\n", nullptr)
          .source,
      ContainsSubstring("Read as: \"it\" is count"));
  REQUIRE(candidates("ages = [1]\nsizes = [2]\nsort it\n") ==
          strings{"sizes", "ages"});
}

TEST_CASE("Settle what a rule decides, without the model.", "[phrase][question]") {
  // One close spelling.
  auto result = settled("total = 1\nshow totl\n");
  REQUIRE(result.succeeded());
  REQUIRE_THAT(result.source, ContainsSubstring("    Read as: \"totl\" is total\n"));
  REQUIRE(result.timing.by_rule == 1);
  // One list for `it`; `it` is a whole word, not the end of "split".
  result = settled("count = 2\nwords = [\"b\", \"a\"]\nsort it\nshow words\n");
  REQUIRE(result.succeeded());
  REQUIRE_THAT(result.source, ContainsSubstring("sort it\n    Read as: \"it\" is words\n"));
  // The ranking shortcut: `root of` shares a whole word with only one
  // phrase.
  result = settled("x = root of 16\n");
  REQUIRE(result.succeeded());
  REQUIRE(result.timing.by_ranking == 1);
  REQUIRE_THAT(result.source,
               ContainsSubstring("Read as: \"root of 16\" is square root of 16"));
  // A line starting with a record's name makes one, whatever words sit
  // between the values.
  result = settled("a person has:\n  name (text)\n  age (whole number)\n"
                   "w = a person namely \"W\", aged 12\nshow w's name\n");
  REQUIRE(result.succeeded());
  REQUIRE(result.timing.by_ranking == 1);
  REQUIRE_THAT(result.source,
               ContainsSubstring("is a person with name \"W\", age 12"));
  // A rebuild reads the `Read as:` lines and asks nothing.
  const auto again = settled(result.source);
  REQUIRE(again.source == result.source);
  REQUIRE(again.timing.by_source == 1);
  REQUIRE(again.timing.questions == 0);
}

TEST_CASE("Offer a left-out argument.", "[phrase][question]") {
  // `rounded to 2 decimals` of what? The most recent number comes first.
  const auto offered =
      candidates("price = 2.345\ntotal = 7.5\nshow rounded to 2 decimals\n");
  REQUIRE(!offered.empty());
  REQUIRE(offered.front() == "total rounded to 2 decimal");
}

namespace {

// Picks the first reading of every question, surely.
class first_model final : public ovis::english::language_model {
public:
  std::size_t questions = 0;

  auto name() const -> std::string override { return "first"; }
  auto fingerprint() -> std::string override { return "weights 1"; }
  auto chat(const std::vector<ovis::english::chat_message> &,
            std::string_view) -> std::string override {
    FAIL("Only choose is used.");
    return {};
  }
  auto choose(const std::vector<ovis::english::chat_message> &,
              std::size_t p_options) -> std::vector<double> override {
    ++questions;
    std::vector<double> result(p_options, 0);
    result.front() = 1;
    return result;
  }
};

} // namespace

TEST_CASE("The examples build offline and stay as they are.",
          "[phrase][examples]") {
  const std::filesystem::path examples = OVIS_EXAMPLES_DIR;
  std::size_t built = 0;
  for (const auto &entry : std::filesystem::directory_iterator(examples)) {
    const auto &path = entry.path();
    if (path.extension() != ".ovis" && !entry.is_directory())
      continue;
    INFO(path.string());
    const auto files = ovis::phrase::load_phrase_program(path);
    // Twice: the second build reads what the first wrote.
    for (int round = 0; round < 2; ++round) {
      const auto result = ovis::phrase::compile_phrase_program(files, nullptr);
      std::string problems;
      for (const auto &problem : result.problems)
        problems += problem + "\n";
      INFO(problems);
      // The vague example is a question nobody can answer offline.
      REQUIRE(result.succeeded() == (path.stem() != "vague"));
      for (std::size_t index = 0; index < files.size(); ++index) {
        REQUIRE(result.sources[index] == files[index].text);
        if (result.succeeded())
          REQUIRE(result.locks[index] == files[index].lock);
      }
    }
    ++built;
  }
  REQUIRE(built == 11);
}

TEST_CASE("Keep the model's readings in the lockfile.", "[phrase][lockfile]") {
  const std::string source = "numbers = [3, 1, 2]\nx = mean of numbers\nshow x\n";
  first_model model;
  const auto first = ovis::phrase::compile_phrases(source, &model);
  REQUIRE(first.succeeded());
  // The 9 readings fit in one question.
  REQUIRE(model.questions == 1);
  REQUIRE(first.lock.readings.size() == 1);
  REQUIRE(first.lock.readings[0].span == "mean of numbers");
  REQUIRE(first.lock.model == "first");
  REQUIRE(first.lock.model_fingerprint == "weights 1");
  // Saved and loaded unchanged.
  const auto path = std::filesystem::temp_directory_path() / "ovis-test.lock";
  first.lock.save(path);
  const auto loaded = ovis::phrase::lockfile::load(path);
  std::filesystem::remove(path);
  REQUIRE(loaded == first.lock);

  // Without its `Read as:` line (`--keep-source`), the lockfile settles the
  // line with no model.
  const auto offline = ovis::phrase::compile_phrases(
      source, nullptr, {.previous = &loaded});
  REQUIRE(offline.succeeded());
  REQUIRE(offline.timing.by_lockfile == 1);
  REQUIRE(offline.source == first.source);
  REQUIRE(offline.lock == loaded);

  // With it, both files stay as they are.
  const auto rebuilt = ovis::phrase::compile_phrases(first.source, nullptr,
                                                     {.previous = &loaded});
  REQUIRE(rebuilt.succeeded());
  REQUIRE(rebuilt.source == first.source);
  REQUIRE(rebuilt.lock == loaded);

  // A changed line is a new question: its old reading doesn't apply.
  const auto changed = ovis::phrase::compile_phrases(
      "numbers = [3, 1, 2]\ny = mean of numbers\nshow y\n", nullptr,
      {.previous = &loaded});
  REQUIRE(!changed.succeeded());
  REQUIRE(changed.timing.by_lockfile == 0);
}

namespace {

// Says "none of these", and rewrites each line as it is told.
class rewriting_model final : public ovis::english::language_model {
public:
  std::map<std::string, std::string> rewrites;
  std::vector<std::string> grammars;

  auto name() const -> std::string override { return "rewriting"; }
  auto chat(const std::vector<ovis::english::chat_message> &p_messages,
            std::string_view p_grammar) -> std::string override {
    grammars.emplace_back(p_grammar);
    const auto &asked = p_messages.back().content;
    for (const auto &[part, rewrite] : rewrites)
      if (asked.find("Part: \"" + part + "\"") != std::string::npos)
        return rewrite;
    return {};
  }
  auto choose(const std::vector<ovis::english::chat_message> &,
              std::size_t p_options) -> std::vector<double> override {
    std::vector<double> result(p_options, 0);
    result.back() = 1;
    return result;
  }
};

} // namespace

TEST_CASE("Rewrite a line no reading offered fits.", "[phrase][question]") {
  const std::string person = "a person has:\n  name (text)\n  age (whole number)\n"
                             "w = a person with name \"W\", age 1\n";
  rewriting_model model;
  model.rewrites = {{"w's age becomes 13", "set w's age to 13"},
                    {"remove w", "show 1"}};
  const auto result = ovis::phrase::compile_phrases(
      person + "w's age becomes 13\nshow w's age\n", &model);
  INFO((result.problems.empty() ? std::string() : result.problems.front()));
  REQUIRE(result.succeeded());
  REQUIRE(result.timing.by_rewrite == 1);
  REQUIRE_THAT(result.source,
               ContainsSubstring("w's age becomes 13\n    Read as: \"w's age "
                                 "becomes 13\" is set w's age to 13\n"));
  REQUIRE(result.lock.readings.size() == 1);
  // The grammar has the program's words and texts, not the example's.
  REQUIRE_THAT(model.grammars.front(), ContainsSubstring("\"age\""));
  REQUIRE_THAT(model.grammars.front(), !ContainsSubstring("\"box\""));

  // The lockfile settles it again with no model.
  const auto again = ovis::phrase::compile_phrases(
      person + "w's age becomes 13\nshow w's age\n", nullptr,
      {.previous = &result.lock});
  REQUIRE(again.succeeded());
  REQUIRE(again.timing.by_lockfile == 1);

  // A rewrite that loses a name of the line is refused.
  const auto refused =
      ovis::phrase::compile_phrases(person + "remove w\n", &model);
  REQUIRE(!refused.succeeded());
  REQUIRE(refused.timing.by_rewrite == 0);
}
