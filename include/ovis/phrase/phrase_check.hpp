#ifndef OVIS_PHRASE_PHRASE_CHECK_HPP
#define OVIS_PHRASE_PHRASE_CHECK_HPP

#include <filesystem>
#include <iosfwd>
#include <vector>

#include <ovis/def.hpp>

// Whether a program's own phrases make the built-in questions worse: each
// case of `eval/questions.txt` (compiled into `ovis`) is read with and
// without them, by the ranking alone (no model). `ovis check-phrases PROGRAM`
// and `ovis_eval questions --with PROGRAM`.
namespace ovis::phrase {

// A small program with one unclear span, and the reading a person means
// ("none" when no reading fits).
struct phrase_case {
  string_type expected;
  string_type program;
};

// Cases written as in `eval/questions.txt`: `expect: READING`, the program,
// a blank line; `#` lines before an `expect:` are comments.
auto parse_phrase_cases(string_view_type p_text) -> std::vector<phrase_case>;

// `eval/questions.txt` as it was when `ovis` was built.
auto embedded_question_cases_text() -> string_view_type;

// What makes a program's phrases work on other lines: its helper files
// whole (they only define phrases and data), and the `to …:` blocks and
// `use` lines of `main.ovis` or a single file.
auto read_phrase_definitions(const std::filesystem::path &p_path) -> string_type;

// Reads every case with and without `p_definitions` and reports to `p_out`
// the ones that got worse (the right reading lost first place, left the
// first question, or the line now reads one way without asking) or better.
// Returns 0, 1 when a case got worse, 2 when the phrases don't read alone.
auto check_phrases(const std::vector<phrase_case> &p_cases,
                   const string_type &p_definitions, std::ostream &p_out) -> int;

} // namespace ovis::phrase

#endif // OVIS_PHRASE_PHRASE_CHECK_HPP
