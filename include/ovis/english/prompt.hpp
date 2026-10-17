#ifndef OVIS_ENGLISH_PROMPT_HPP
#define OVIS_ENGLISH_PROMPT_HPP

#include <utility>
#include <vector>

#include <ovis/english/language_model.hpp>
#include <ovis/english/lockfile.hpp>

namespace ovis::english {

// Detail pass: from the author's paragraph to concrete numbered steps, or a
// verdict that it can't be done.

// GBNF grammar of the detail pass's reply format. Without `p_allow_note`,
// the model can't call the paragraph a note.
auto detail_grammar(bool p_allow_note = true) -> string_view_type;

// Whether a sentence of the paragraph starts with a command ("Say hello.",
// "Finally tell whether 97 is prime."). Such a paragraph asks for something,
// so it is never a note; small models otherwise call short commands notes.
auto starts_with_command(string_view_type p_paragraph) -> bool;

// Whether the paragraph has Python-like code in it: a block header such as
// "def f(x):" or "for x in xs:", a call written as "print(x)", or "x = 1".
auto looks_like_code(string_view_type p_paragraph) -> bool;

// Neither a command nor code: only such a paragraph may be called a note.
auto may_be_note(string_view_type p_paragraph) -> bool;

// `p_others` describes what the rest of the program does (its steps).
// `p_previous_steps` are steps written for an earlier version of the
// paragraph: the model keeps what still fits and drops the rest.
// `p_notes` is added before the paragraph (helper-file rules, C functions).
auto detail_request(string_view_type p_paragraph,
                    const std::vector<string_type> &p_others,
                    const std::vector<string_type> &p_previous_steps = {},
                    string_view_type p_notes = {})
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
