#ifndef OVIS_CORE_CHECKER_HPP
#define OVIS_CORE_CHECKER_HPP

#include <vector>

#include <ovis/core/ast.hpp>

namespace ovis::core {

struct diagnostic {
  // Index of the unit (paragraph) the problem is in.
  std::size_t unit_index = 0;
  source_location location;
  string_type message;
};

// Resolves names and types across the whole program and annotates every
// expression with its type. The messages are written so they can be handed
// back to the translator as-is. Deterministic choices the checker makes (such
// as converting a printf argument) are added to `p_notes`.
auto check(program &p_program, std::vector<diagnostic> *p_notes = nullptr)
    -> std::vector<diagnostic>;

} // namespace ovis::core

#endif // OVIS_CORE_CHECKER_HPP
