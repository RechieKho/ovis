#include <algorithm>
#include <numeric>
#include <set>

#include "questions.hpp"

namespace ovis::phrase::internal {

namespace {

// How alike two words are, for ranking which template an unknown phrase
// might mean: whole words, then a shared start, then shared trigrams.
auto word_similarity(string_view_type p_left, string_view_type p_right)
    -> double {
  if (same_word(p_left, p_right))
    return 3;
  const auto left = lower(p_left);
  const auto right = lower(p_right);
  std::size_t prefix = 0;
  while (prefix < left.size() && prefix < right.size() &&
         left[prefix] == right[prefix])
    ++prefix;
  if (prefix >= 4)
    return 2;
  std::set<string_type> left_trigrams;
  std::set<string_type> right_trigrams;
  for (std::size_t index = 0; index + 3 <= left.size(); ++index)
    left_trigrams.insert(left.substr(index, 3));
  for (std::size_t index = 0; index + 3 <= right.size(); ++index)
    right_trigrams.insert(right.substr(index, 3));
  std::size_t shared = 0;
  for (const auto &trigram : left_trigrams)
    shared += right_trigrams.contains(trigram);
  const auto all = left_trigrams.size() + right_trigrams.size() - shared;
  return all > 0 && double(shared) / double(all) >= 0.4 ? 1 : 0;
}

auto is_function_word(string_view_type p_word) -> bool {
  static const std::set<string_view_type> words = {
      "of", "to", "in", "from", "by", "for", "with", "and", "a",
      "an", "the", "is", "at", "on", "as", "into", "it"};
  return words.contains(lower(p_word));
}

} // namespace

// How much lower a reading ranks for using the arguments in another order
// than written.
constexpr double order_penalty = 0.1;

auto unknown_candidates(const std::vector<token> &p_tokens, std::size_t p_begin,
                        std::size_t p_end, bool p_statement, bool p_block,
                        const std::map<string_type, value_kind> &p_scope,
                        const std::vector<string_type> &p_recent,
                        const vocabulary &p_vocabulary,
                        const std::function<bool(const string_type &)> &p_fits)
    -> std::vector<candidate> {
  line_parser parser(p_tokens, p_scope, p_vocabulary, false);
  std::vector<parse> arguments;
  std::vector<string_type> verb;
  // Names that are also plain words (`how many items in items`): each may
  // be part of the verb instead.
  std::vector<std::pair<std::size_t, string_type>> name_words;
  for (auto index = p_begin; index < p_end; ++index) {
    const auto &item = p_tokens[index];
    if (is_article(item) || item.is_symbol(","))
      continue;
    if (item.is_symbol("(") || item.is_symbol("[")) {
      auto close = index;
      for (int level = 0; close < p_end; ++close) {
        level += p_tokens[close].is_symbol("(") || p_tokens[close].is_symbol("[");
        level -= p_tokens[close].is_symbol(")") || p_tokens[close].is_symbol("]");
        if (level == 0)
          break;
      }
      const auto &inside = parser.expressions(index, close + 1);
      if (!inside.empty())
        arguments.push_back(inside.front());
      index = close;
      continue;
    }
    // A minus before a value is part of it (`how far -5 is from zero`),
    // unless it follows a value (`3 - 5`).
    if (item.is_symbol("-") && index + 1 < p_end &&
        (index == p_begin || parser.expressions(index - 1, index).empty())) {
      const auto &negative = parser.expressions(index, index + 2);
      if (!negative.empty()) {
        arguments.push_back(negative.front());
        ++index;
        continue;
      }
    }
    const auto &single = parser.expressions(index, index + 1);
    if (!single.empty()) {
      if (item.kind == token::kind_type::word && p_scope.contains(item.text))
        name_words.emplace_back(arguments.size(), item.text);
      arguments.push_back(single.front());
    } else if (item.kind == token::kind_type::word) {
      verb.push_back(item.text);
    }
  }

  // The arguments as written, and with one more: a recent name the line may
  // have left out (`show rounded to 2 decimals`), offered only when it ranks
  // above every reading of the arguments as written. Each set has the verb words it
  // leaves over and how much lower it ranks.
  struct argument_set {
    std::vector<parse> arguments;
    std::vector<string_type> verb;
    double penalty = 0;
  };
  std::vector<argument_set> argument_sets = {{arguments, verb, 0}};
  if (name_words.size() > 1)
    for (const auto &[position, word] : name_words) {
      auto fewer = arguments;
      fewer.erase(fewer.begin() + static_cast<std::ptrdiff_t>(position));
      auto more_verb = verb;
      more_verb.push_back(word);
      argument_sets.push_back({std::move(fewer), std::move(more_verb), 0.5});
    }
  for (std::size_t index = 0; index < p_recent.size() && index < 3; ++index) {
    const auto found = p_scope.find(p_recent[index]);
    if (found == p_scope.end())
      continue;
    auto more = arguments;
    more.push_back({.core = core_name(found->first),
                    .reading = found->first,
                    .type = found->second});
    argument_sets.push_back({std::move(more), verb, 1});
  }

  std::vector<candidate> result;
  std::set<string_type> seen;
  std::set<string_type> left_out;
  // Readings with the arguments in another order than written: lower only
  // among equals, never cut for it (`3 goes into n` is `n is divisible
  // by 3`).
  std::set<string_type> reordered;
  for (const auto &[arguments, verb, penalty] : argument_sets)
  for (const auto &item : p_vocabulary.templates) {
    if ((item.kind == phrase_template::kind_type::statement) != p_statement ||
        (p_statement && item.takes_block != p_block))
      continue;
    const auto &pattern = item.patterns[0];
    std::vector<std::size_t> slots;
    for (const auto &element : pattern)
      if (element.kind == pattern_element::kind_type::slot)
        slots.push_back(element.slot);
    // Types and fields are words, not arguments.
    if (std::ranges::any_of(slots, [&](std::size_t p_slot) {
          return item.slots[p_slot].type.name == "type" ||
                 item.slots[p_slot].type.kind == type_name::kind_type::field;
        }))
      continue;
    const auto variadic = !slots.empty() && item.slots[slots.back()].is_variadic;
    // A left-out argument fills a fixed slot, not one more of a list.
    if ((penalty > 0 && variadic) || arguments.size() < slots.size() ||
        (!variadic && arguments.size() != slots.size()) || arguments.size() > 4)
      continue;

    double score = 0;
    bool complete = true;
    for (const auto &word : verb) {
      double best = 0;
      for (const auto &alternative : item.patterns)
        for (const auto &element : alternative) {
          std::vector<string_type> words;
          if (element.kind == pattern_element::kind_type::word)
            words.push_back(element.word);
          for (const auto &branch : element.branches)
            for (const auto &part : branch)
              words.push_back(part.word);
          for (const auto &candidate_word : words)
            best = std::max(best, word_similarity(word, candidate_word) *
                                      (is_function_word(word) ? 0.2 : 1.0));
        }
      score += best;
      complete = complete && (best > 0 || is_function_word(word));
    }

    std::vector<std::size_t> order(arguments.size());
    std::iota(order.begin(), order.end(), 0);
    do {
      slot_fills fills;
      fills.values.resize(item.slots.size());
      fills.types.resize(item.slots.size());
      for (std::size_t index = 0; index < order.size(); ++index) {
        const auto slot = slots[std::min(index, slots.size() - 1)];
        fills.values[slot].push_back(arguments[order[index]]);
      }
      if (const auto built = build_phrase(item, pattern, fills, p_vocabulary))
        if (seen.insert(built->reading).second && p_fits(built->reading)) {
          if (penalty >= 1)
            left_out.insert(built->reading);
          const bool as_written = std::ranges::is_sorted(order);
          if (!as_written)
            reordered.insert(built->reading);
          // The order as written comes first among equals.
          result.push_back({built->reading,
                            score - penalty - (as_written ? 0 : order_penalty),
                            complete && penalty == 0});
        }
    } while (std::ranges::next_permutation(order).found);
  }
  double best_as_written = -1e9;
  for (const auto &item : result)
    if (!left_out.contains(item.reading))
      best_as_written = std::max(best_as_written, item.score);
  std::erase_if(result, [&](const candidate &p_item) {
    return left_out.contains(p_item.reading) && p_item.score <= best_as_written;
  });
  // The 8 best fit in one question (phrase-grammar §4). Readings as good
  // as the 8th stay, for a run-off: the ranking can't tell them apart, and
  // cutting them by the library's order lost right readings.
  const auto unordered = [&](const candidate &p_item) {
    return p_item.score + (reordered.contains(p_item.reading) ? order_penalty : 0);
  };
  if (result.size() > per_question) {
    std::vector<double> scores;
    for (const auto &item : result)
      scores.push_back(unordered(item));
    std::ranges::sort(scores, std::ranges::greater{});
    const auto last = scores[per_question - 1];
    std::erase_if(result,
                  [&](const candidate &p_item) { return unordered(p_item) < last; });
  }
  std::ranges::stable_sort(result, std::ranges::greater{}, &candidate::score);
  if (result.size() > 21)
    result.resize(21);
  return result;
}

auto name_candidates(const std::vector<token> &p_tokens, std::size_t p_at,
                     const line_shape &p_shape,
                     const std::map<string_type, value_kind> &p_scope,
                     const std::vector<string_type> &p_recent,
                     const vocabulary &p_vocabulary) -> std::vector<candidate> {
  std::vector<candidate> result;
  for (const auto &name : p_recent) {
    if (!p_scope.contains(name))
      continue;
    auto tokens = p_tokens;
    tokens[p_at].text = name;
    line_parser parser(tokens, p_scope, p_vocabulary, false);
    if (!readings_of(parser, p_shape).empty())
      result.push_back({name});
  }
  return result;
}

auto word_candidates(const std::vector<token> &p_tokens, std::size_t p_at,
                     const line_shape &p_shape,
                     const std::map<string_type, value_kind> &p_scope,
                     const std::vector<string_type> &p_words,
                     const vocabulary &p_vocabulary) -> std::vector<candidate> {
  std::vector<candidate> result;
  for (const auto &word : p_words) {
    auto tokens = p_tokens;
    tokens[p_at].text = word;
    line_parser parser(tokens, p_scope, p_vocabulary, false);
    if (!readings_of(parser, p_shape).empty())
      result.push_back({word});
  }
  return result;
}

auto is_pronoun(string_view_type p_word) -> bool {
  const auto word = lower(p_word);
  return word == "it" || word == "them";
}

auto ranking_shortcut(const std::vector<candidate> &p_candidates)
    -> std::optional<std::size_t> {
  if (p_candidates.empty())
    return std::nullopt;
  const auto best = static_cast<std::size_t>(
      std::ranges::max_element(p_candidates, {}, &candidate::score) -
      p_candidates.begin());
  double second = 0;
  for (std::size_t index = 0; index < p_candidates.size(); ++index)
    if (index != best)
      second = std::max(second, p_candidates[index].score);
  if (p_candidates[best].covers_span &&
      p_candidates[best].score >= shortcut_score &&
      p_candidates[best].score - second >= shortcut_margin)
    return best;
  return std::nullopt;
}

namespace {

constexpr string_view_type question_instructions =
    "You help a compiler read a program written in plain English. Some words "
    "in a line are not in the compiler's vocabulary. You are shown the line "
    "and numbered phrases the compiler knows. Answer with the number of the "
    "phrase that means the same as the unknown words, as an English speaker "
    "would understand them. If none means the same, answer with the number "
    "of \"none of these\".";

} // namespace

auto question_asker::available() -> bool {
  if (!unavailable.empty())
    return false;
  if (model == nullptr)
    unavailable = "there is no model (--offline)";
  // The first question also loads the model, which says nothing about
  // how long questions take, so it doesn't count.
  else if (timing.model_ms - timing.first_question_ms >
           options.model_budget_seconds * 1000)
    unavailable = "the model used up its time (--model-budget)";
  return unavailable.empty();
}

auto question_asker::count_time(double p_milliseconds) -> void {
  if (timing.asked == 0)
    timing.first_question_ms += p_milliseconds;
  timing.model_ms += p_milliseconds;
}

auto question_asker::order_ties(const string_type &p_span,
                                std::vector<candidate> &p_options) -> void {
  // Runs of equal score, as [begin, end) of the sorted readings.
  std::vector<std::pair<std::size_t, std::size_t>> ties;
  for (std::size_t begin = 0; begin < p_options.size();) {
    auto end = begin + 1;
    while (end < p_options.size() &&
           p_options[end].score == p_options[begin].score)
      ++end;
    if (end - begin > 1)
      ties.emplace_back(begin, end);
    begin = end;
  }
  if (ties.empty() || !available())
    return;

  std::vector<string_type> texts = {p_span};
  for (const auto &[begin, end] : ties)
    for (auto index = begin; index < end; ++index)
      texts.push_back(p_options[index].reading);
  const auto start = clock_type::now();
  std::vector<std::vector<float>> vectors;
  try {
    vectors = model->embed(texts);
  } catch (const error &p_error) {
    unavailable = p_error.what();
    return;
  }
  count_time(milliseconds_since(start));
  if (vectors.size() != texts.size())
    return;

  std::size_t next = 1;
  for (const auto &[begin, end] : ties) {
    std::vector<std::pair<double, candidate>> scored;
    for (auto index = begin; index < end; ++index, ++next) {
      double similarity = 0;
      for (std::size_t at = 0; at < vectors[0].size(); ++at)
        similarity += double(vectors[0][at]) * vectors[next][at];
      scored.emplace_back(similarity, p_options[index]);
    }
    std::ranges::stable_sort(scored, std::ranges::greater{},
                             &std::pair<double, candidate>::first);
    for (auto index = begin; index < end; ++index)
      p_options[index] = scored[index - begin].second;
    // How far the first is ahead, for judging when to trust it.
    if (begin == 0 && options.log)
      options.log("embedding: \"" + p_span + "\" is " +
                  scored[0].second.reading + ", similarity " +
                  std::to_string(scored[0].first) + ", margin " +
                  std::to_string(scored[0].first - scored[1].first));
  }
}

auto question_asker::ask(const string_type &p_line, const string_type &p_known,
         const string_type &p_span, const std::vector<candidate> &p_options)
    -> model_answer {
  if (!available())
    return {};

  auto text = "Line: " + p_line + "\n";
  if (!p_known.empty())
    text += "Known: " + p_known + "\n";
  text += "Which phrase means the same as \"" + p_span + "\"?\n";
  for (std::size_t index = 0; index < p_options.size(); ++index)
    text += string_type(1, english::choice_digit(index + 1)) + ". " +
            p_options[index].reading + "\n";
  text += string_type(1, english::choice_digit(p_options.size() + 1)) +
          ". none of these\nAnswer:";

  const auto start = clock_type::now();
  model_answer answer;
  try {
    answer.probabilities = model->choose(
        {{"system", string_type(question_instructions)}, {"user", text}},
        p_options.size() + 1);
  } catch (const error &p_error) {
    unavailable = p_error.what();
    return {};
  }
  const auto elapsed = milliseconds_since(start);
  count_time(elapsed);
  ++timing.asked;

  std::vector<std::size_t> ranked(answer.probabilities.size());
  std::iota(ranked.begin(), ranked.end(), 0);
  std::ranges::stable_sort(ranked, [&](std::size_t p_left, std::size_t p_right) {
    return answer.probabilities[p_left] > answer.probabilities[p_right];
  });
  const auto best = ranked[0];
  const auto margin =
      answer.probabilities[best] - answer.probabilities[ranked[1]];
  if (best < p_options.size() && answer.probabilities[best] >= 0.6 &&
      margin >= 0.3)
    answer.choice = best;
  if (options.log) {
    string_type summary = "asked about \"" + p_span + "\":";
    for (std::size_t index = 0; index < ranked.size() && index < 3; ++index) {
      const auto option = ranked[index];
      summary += " " +
                 (option < p_options.size() ? p_options[option].reading
                                            : string_type("none")) +
                 " " +
                 std::to_string(int(answer.probabilities[option] * 100 + 0.5)) +
                 "%";
    }
    options.log(summary + " (" + std::to_string(int(elapsed)) + " ms)");
  }
  return answer;
}

auto question_asker::choose(const string_type &p_line, const string_type &p_known,
            const string_type &p_span, std::vector<candidate> p_options)
    -> std::optional<string_type> {
  while (p_options.size() > per_question) {
    std::vector<candidate> winners;
    for (std::size_t begin = 0; begin < p_options.size(); begin += per_question) {
      std::vector<candidate> group(
          p_options.begin() + static_cast<std::ptrdiff_t>(begin),
          p_options.begin() +
              static_cast<std::ptrdiff_t>(std::min(begin + per_question, p_options.size())));
      // A group of one goes on to the final unasked.
      if (group.size() == 1) {
        winners.push_back(group.front());
        continue;
      }
      const auto answer = ask(p_line, p_known, p_span, group);
      if (!unavailable.empty())
        return std::nullopt;
      // A group's favourite goes on even when it isn't sure yet.
      const auto best = static_cast<std::size_t>(
          std::ranges::max_element(answer.probabilities) -
          answer.probabilities.begin());
      if (best < group.size())
        winners.push_back(group[best]);
    }
    p_options = std::move(winners);
  }
  if (p_options.empty())
    return std::nullopt;
  const auto answer = ask(p_line, p_known, p_span, p_options);
  if (!answer.choice)
    return std::nullopt;
  return p_options[*answer.choice].reading;
}

} // namespace ovis::phrase::internal
