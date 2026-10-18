#ifndef OVIS_PHRASE_QUESTIONS_HPP
#define OVIS_PHRASE_QUESTIONS_HPP

#include <functional>

#include <ovis/english/language_model.hpp>
#include <ovis/phrase/front_end.hpp>

#include "parser.hpp"

// Questions about unclear spans: their candidate readings, and asking the
// model to choose between them.
namespace ovis::phrase::internal {

// What a question is about (phrase-grammar.md §4).
enum class question_kind {
  // Words no phrase reads: `mean of numbers`.
  unknown_phrase,
  // Several readings type-check: `square root of x + 1`.
  ambiguous_parse,
  // A word that is no name in scope: `show totl`.
  unknown_name,
  // `it` or `them`.
  reference
};

struct candidate {
  string_type reading;
  // How alike its words are to the span's: for order, ties and the
  // ranking shortcut.
  double score = 0;
  // Every word of the span besides its arguments and function words is
  // like one of the phrase's: only then may the shortcut take it ("is
  // below zero" isn't "is zero").
  bool covers_span = false;
};

struct question {
  question_kind kind = question_kind::unknown_phrase;
  // The words in doubt, as tokens of the line.
  std::size_t begin = 0;
  std::size_t end = 0;
  std::vector<candidate> candidates;
  // A deterministic rule's answer, which needs no model: the one name of
  // the right type for `it`, the one close spelling of a name.
  std::optional<std::size_t> rule;
  // For a word no field is called: the record (its fields are the
  // candidates). For `reference`, the record named (`the book`), if any.
  string_type record;
};

// Readings an unknown span might have: templates whose slots take the
// span's arguments (names, numbers, texts, bracketed groups), in some order
// the types allow, ranked by how alike their words are to the span's
// leftover words. `p_recent` (most recently set first) supplies an argument
// the line may have left out. Only readings `p_fits` accepts are kept.
auto unknown_candidates(const std::vector<token> &p_tokens, std::size_t p_begin,
                        std::size_t p_end, bool p_statement, bool p_block,
                        const std::map<string_type, value_kind> &p_scope,
                        const std::vector<string_type> &p_recent,
                        const vocabulary &p_vocabulary,
                        const std::function<bool(const string_type &)> &p_fits)
    -> std::vector<candidate>;

// Names in scope (most recently set first) with which the line reads, in
// place of the word at `p_at`.
auto name_candidates(const std::vector<token> &p_tokens, std::size_t p_at,
                     const line_shape &p_shape,
                     const std::map<string_type, value_kind> &p_scope,
                     const std::vector<string_type> &p_recent,
                     const vocabulary &p_vocabulary) -> std::vector<candidate>;

// The words of `p_words` with which the line reads, in place of the word at
// `p_at`: a record's fields for a word no field is called.
auto word_candidates(const std::vector<token> &p_tokens, std::size_t p_at,
                     const line_shape &p_shape,
                     const std::map<string_type, value_kind> &p_scope,
                     const std::vector<string_type> &p_words,
                     const vocabulary &p_vocabulary) -> std::vector<candidate>;

auto is_pronoun(string_view_type p_word) -> bool;

// The ranking shortcut: a candidate that covers the span and whose words
// are so much closer to the span's than any other's that the model isn't
// asked. Measured with `ovis_eval questions`: at least one more shared whole
// word (3) than the next.
constexpr double shortcut_score = 3;
constexpr double shortcut_margin = 3;
auto ranking_shortcut(const std::vector<candidate> &p_candidates)
    -> std::optional<std::size_t>;

// Readings in one question; the last option is "none of these".
constexpr std::size_t per_question = english::max_choice_options - 1;

// The accepted reading, or nothing when the model isn't sure or picks none.
struct model_answer {
  std::optional<std::size_t> choice;
  std::vector<double> probabilities;
};

class question_asker {
  english::language_model *model;
  const front_end_options &options;
  front_end_timing &timing;

public:
  // Why the model can no longer be asked, once it can't.
  string_type unavailable;

private:
  // Whether the model may still be used; sets `unavailable` if not.
  auto available() -> bool;
  // Adds a use of the model to the timing. The first use also loads it.
  auto count_time(double p_milliseconds) -> void;

public:

  question_asker(english::language_model *p_model,
                 const front_end_options &p_options, front_end_timing &p_timing)
      : model(p_model), options(p_options), timing(p_timing) {}


  // Orders readings the ranking ties by how alike the model finds each to
  // the span: the ranking only compares spellings, so synonyms (`floored`,
  // `rounded down`) tie, and then library order would decide. Leaves them
  // as they are when the model can't embed.
  auto order_ties(const string_type &p_span, std::vector<candidate> &p_options)
      -> void;
  // Asks about at most `per_question` readings plus "none of these".
  auto ask(const string_type &p_line, const string_type &p_known,
           const string_type &p_span, const std::vector<candidate> &p_options)
      -> model_answer;
  // More readings than fit in one question: each group picks its best, then
  // the winners are asked about together.
  auto choose(const string_type &p_line, const string_type &p_known,
              const string_type &p_span, std::vector<candidate> p_options)
      -> std::optional<string_type>;
};

} // namespace ovis::phrase::internal

#endif // OVIS_PHRASE_QUESTIONS_HPP
