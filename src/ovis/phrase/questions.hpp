#ifndef OVIS_PHRASE_QUESTIONS_HPP
#define OVIS_PHRASE_QUESTIONS_HPP

#include <ovis/english/language_model.hpp>
#include <ovis/phrase/front_end.hpp>

#include "parser.hpp"

// Questions about unclear spans: their candidate readings, and asking the
// model to choose between them.
namespace ovis::phrase::internal {

struct candidate {
  string_type reading;
  double score = 0;
};

// Readings an unknown span might have: templates whose slots take the
// span's arguments (names, numbers, texts, bracketed groups), in some order
// the types allow, ranked by how alike their words are to the span's
// leftover words.
auto unknown_candidates(const std::vector<token> &p_tokens, std::size_t p_begin,
                        std::size_t p_end, bool p_statement, bool p_block,
                        const std::map<string_type, value_kind> &p_scope,
                        const vocabulary &p_vocabulary)
    -> std::vector<candidate>;

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

  question_asker(english::language_model *p_model,
                 const front_end_options &p_options, front_end_timing &p_timing)
      : model(p_model), options(p_options), timing(p_timing) {}


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
