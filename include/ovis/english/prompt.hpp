#ifndef OVIS_ENGLISH_PROMPT_HPP
#define OVIS_ENGLISH_PROMPT_HPP

#include <utility>
#include <vector>

#include <ovis/english/language_model.hpp>
#include <ovis/english/lockfile.hpp>

namespace ovis::english {

// Detail pass: from the author's paragraph to concrete numbered steps, or a
// verdict that it can't be done.

// GBNF grammar of the detail pass's reply format.
auto detail_grammar() -> string_view_type;

// `p_others` describes what the rest of the program does (its steps).
// `p_previous_steps` are steps written for an earlier version of the
// paragraph: the model keeps what still fits and drops the rest.
auto detail_request(string_view_type p_paragraph,
                    const std::vector<string_type> &p_others,
                    const std::vector<string_type> &p_previous_steps = {})
    -> std::vector<chat_message>;

// Review of steps the author edited: the model checks they still do what the
// paragraph asks (status ok, with fresh assumptions) or says why not
// (status mismatch). The steps themselves are never rewritten.
auto review_grammar() -> string_view_type;
auto review_request(string_view_type p_paragraph,
                    const std::vector<string_type> &p_steps,
                    const std::vector<string_type> &p_others)
    -> std::vector<chat_message>;
auto parse_review(string_view_type p_reply) -> detail_result;

// Throws `error` if the reply can't be read.
auto parse_detail(string_view_type p_reply) -> detail_result;

// Translation: from steps to Ovis Core.

// GBNF grammar of well-formed Ovis Core output.
auto core_grammar() -> string_view_type;

// A stricter grammar used when repairing a translation: forms may only start
// with a keyword, a built-in, a C function or one of `p_names` (the functions
// the program defines), so the model cannot call something made up.
auto restricted_core_grammar(const std::vector<string_type> &p_names)
    -> string_type;

// The conversation asking the model to translate one paragraph, given its
// numbered steps. `p_context` lists declarations made by the rest of the
// program.
auto translation_request(string_view_type p_paragraph,
                         string_view_type p_steps,
                         const std::vector<string_type> &p_context)
    -> std::vector<chat_message>;

// Extends a translation request with a failed attempt and what was wrong
// with it, asking for a corrected version.
auto repair_request(std::vector<chat_message> p_request,
                    string_view_type p_previous_core,
                    const std::vector<string_type> &p_problems)
    -> std::vector<chat_message>;

// Strips Markdown fences and any prose around the code.
auto extract_core(string_view_type p_reply) -> string_type;

// Explanation: why a paragraph could not become working code, for its author.

auto explanation_grammar() -> string_view_type;

auto explanation_request(string_view_type p_paragraph,
                         string_view_type p_steps, string_view_type p_core,
                         const std::vector<string_type> &p_problems)
    -> std::vector<chat_message>;

// Returns the reason and the suggestion.
auto parse_explanation(string_view_type p_reply)
    -> std::pair<string_type, string_type>;

} // namespace ovis::english

#endif // OVIS_ENGLISH_PROMPT_HPP
