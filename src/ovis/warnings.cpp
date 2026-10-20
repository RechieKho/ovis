#include <algorithm>
#include <functional>
#include <map>

#include <ovis/core/warnings.hpp>

namespace ovis::core {

namespace {

// Whether a phrase given this could change it for its caller.
auto can_be_changed(const value_type &p_type) -> bool {
  return p_type.is_list || p_type.is_record() ||
         p_type.base == base_type::pointer || p_type.base == base_type::memory;
}

} // namespace

auto warn(const program &p_program, std::size_t p_first_unit)
    -> std::vector<diagnostic> {
  std::map<string_type, const function *> defined;
  for (std::size_t index = p_first_unit; index < p_program.units.size(); ++index)
    for (const auto &function : p_program.units[index].functions)
      defined[function.name] = &function;

  std::vector<diagnostic> result;
  std::size_t unit_index = 0;
  const std::function<void(const std::vector<statement> &)> visit =
      [&](const std::vector<statement> &p_body) {
        for (const auto &statement : p_body) {
          if (statement.kind == statement::kind_type::evaluate &&
              !statement.values.empty() &&
              statement.values[0].kind == expression::kind_type::call) {
            const auto &call = statement.values[0];
            const auto found = defined.find(call.text);
            if (found != defined.end() &&
                found->second->return_type != value_type::nothing() &&
                std::ranges::none_of(call.arguments, [](const expression &p_argument) {
                  return can_be_changed(p_argument.type);
                }))
              result.push_back(
                  {unit_index, statement.location,
                   "'" + call.text + "' gives back a value and changes "
                   "nothing it is given, so this line does nothing. Use what "
                   "it gives back, for example `x = …`."});
          }
          visit(statement.body);
          visit(statement.else_body);
        }
      };
  for (unit_index = p_first_unit; unit_index < p_program.units.size();
       ++unit_index) {
    const auto &unit = p_program.units[unit_index];
    for (const auto &function : unit.functions)
      visit(function.body);
    visit(unit.statements);
  }
  return result;
}

} // namespace ovis::core
