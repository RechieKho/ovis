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
// back to the translator as-is.
auto check(program &p_program) -> std::vector<diagnostic>;

// Signatures of the functions and globals a unit declares, in Ovis Core
// syntax. Used to tell the translator what the rest of the program offers.
auto describe_declarations(const unit &p_unit) -> std::vector<string_type>;

} // namespace ovis::core

#endif // OVIS_CORE_CHECKER_HPP
