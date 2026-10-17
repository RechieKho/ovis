#ifndef OVIS_ENGLISH_PARAGRAPH_HPP
#define OVIS_ENGLISH_PARAGRAPH_HPP

#include <vector>

#include <ovis/def.hpp>

namespace ovis::english {

// The unit of translation. A program is plain text; blank lines separate
// paragraphs and that is the only structure the source has.
struct paragraph {
  std::size_t line = 1;
  std::size_t line_count = 1;
  string_type text;
};

auto split_paragraphs(string_view_type p_source) -> std::vector<paragraph>;

// Collapses whitespace so re-wrapping text doesn't change what it means.
auto normalize(string_view_type p_text) -> string_type;

// A paragraph as it appears in the source once Ovis has detailed it: the
// author's own lines, then an indented block Ovis writes underneath.
//
//   Work out the average of the scores and show it.
//       1. Add up every score into total, starting from 0.
//       2. Print "Average: " followed by total divided by the number of
//          scores, with two decimals.
//       Assumption: the scores are whole numbers, as none has a fraction.
//       Compiler: `total` holds a decimal number, because it is given one
//           later on.
//
// Steps and assumptions may be edited; `Compiler:` lines are rewritten by
// every successful build.
struct detailed_paragraph {
  string_type intent;
  std::vector<string_type> steps;
  std::vector<string_type> assumptions;
  std::vector<string_type> compiler_notes;
  bool has_detail = false;
};

auto parse_detailed(string_view_type p_text) -> detailed_paragraph;

auto render_detailed(string_view_type p_intent,
                     const std::vector<string_type> &p_steps,
                     const std::vector<string_type> &p_assumptions,
                     const std::vector<string_type> &p_compiler_notes)
    -> string_type;

// "1. first\n2. second": how steps are keyed and shown to the model.
auto numbered_steps(const std::vector<string_type> &p_steps) -> string_type;

// The source with each paragraph's lines replaced by the matching text.
auto replace_paragraphs(string_view_type p_source,
                        const std::vector<paragraph> &p_paragraphs,
                        const std::vector<string_type> &p_texts)
    -> string_type;

} // namespace ovis::english

#endif // OVIS_ENGLISH_PARAGRAPH_HPP
