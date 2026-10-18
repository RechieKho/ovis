#ifndef OVIS_CORE_ELABORATE_HPP
#define OVIS_CORE_ELABORATE_HPP

#include <vector>

#include <ovis/core/ast.hpp>
#include <ovis/core/checker.hpp>

namespace ovis::core {

// Fills in what the translation left vague, deterministically, before the
// program is checked:
//
// - infers every type that was left out (parameters, return types, variables,
//   empty lists) from how things are used across the whole program, and
//   defaults whatever nothing constrains to int;
// - reads a misspelled name as the single known name it is closest to;
// - makes a function without `return` return its last value;
// - treats a number used as a condition as "is not zero".
//
// - gives a value used only through one trait's phrases that trait as its
//   type.
//
// Returns one note per decision, so they can be shown to the user. What it
// cannot decide (a value used with the phrases of several traits) goes to
// `p_problems`, if given.
auto elaborate(program &p_program,
               std::vector<diagnostic> *p_problems = nullptr)
    -> std::vector<diagnostic>;

// The single candidate `p_name` most likely meant, if there is one: equal
// ignoring case and `_`/`-`, or within a small edit distance.
auto closest_name(string_view_type p_name,
                  const std::vector<string_type> &p_candidates)
    -> std::optional<string_type>;

} // namespace ovis::core

#endif // OVIS_CORE_ELABORATE_HPP
