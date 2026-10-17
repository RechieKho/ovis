#ifndef OVIS_CORE_MODULES_HPP
#define OVIS_CORE_MODULES_HPP

#include <vector>

#include <ovis/core/ast.hpp>
#include <ovis/core/checker.hpp>

namespace ovis::core {

// The module that runs: `main.ovis`. Every other module is a helper that
// only defines functions and data.
inline constexpr string_view_type main_module = "main";

// Joins the modules of a multi-file program (units carry their module name)
// into one namespace, before elaboration:
//
// - A name defined by one module keeps its name. A name defined by several
//   is renamed `module.name` in each.
// - References are rewritten: a module's own definitions come first, then
//   the one module that defines the name. `module.name`, or just the last
//   parts of the module (`tax.add_tax` for `pricing.tax`), picks one
//   explicitly. A name several other modules define is reported as
//   ambiguous.
// - Helper modules may only define: functions, C declarations, and
//   top-level `let`s whose values don't read input or call functions.
//
// Single-file programs (units without a module) are left alone.
auto resolve_modules(program &p_program) -> std::vector<diagnostic>;

} // namespace ovis::core

#endif // OVIS_CORE_MODULES_HPP
