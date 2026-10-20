#ifndef OVIS_CORE_MEMORY_CHECK_HPP
#define OVIS_CORE_MEMORY_CHECK_HPP

#include <vector>

#include <ovis/core/checker.hpp>

namespace ovis::core {

// The strict memory rules of docs/proposals/memory.md §2 and §4, for a
// program that has been elaborated and checked:
//  - a new text, list or record may only be made as the value a statement
//    stores (a variable, a field, an item, a result);
//  - a function gives back only new values or only existing ones;
//  - a variable is not given a new value while it may still hold one the
//    program never freed;
//  - what a `with new memory:` block makes is not stored outside it, and is
//    not read after it ends.
// Units before `p_first_unit` (the prelude) are analysed but not reported.
auto check_memory(const program &p_program, std::size_t p_first_unit = 0)
    -> std::vector<diagnostic>;

} // namespace ovis::core

#endif // OVIS_CORE_MEMORY_CHECK_HPP
