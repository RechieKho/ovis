#ifndef OVIS_CORE_WARNINGS_HPP
#define OVIS_CORE_WARNINGS_HPP

#include <vector>

#include <ovis/core/checker.hpp>

namespace ovis::core {

// Things that build but are probably not what was meant. For a program that
// has been elaborated and checked: a line that calls a phrase for its result
// and drops it, when the phrase changes nothing it is given (no list, record,
// pointer or memory among its arguments), so the line does nothing
// (docs/proposals/references.md §9). Units before `p_first_unit` (the
// prelude) are not reported.
auto warn(const program &p_program, std::size_t p_first_unit = 0)
    -> std::vector<diagnostic>;

} // namespace ovis::core

#endif // OVIS_CORE_WARNINGS_HPP
