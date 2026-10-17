#ifndef OVIS_ENGLISH_PROMPT_HPP
#define OVIS_ENGLISH_PROMPT_HPP

#include <vector>

#include <ovis/english/language_model.hpp>

namespace ovis::english {

// GBNF grammar of well-formed Ovis Core output.
auto core_grammar() -> string_view_type;

// The conversation asking the model to translate one paragraph.
// `p_context` lists declarations made by the rest of the program.
auto translation_request(string_view_type p_paragraph,
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

} // namespace ovis::english

#endif // OVIS_ENGLISH_PROMPT_HPP
