#include <map>
#include <set>

#include <ovis/core/modules.hpp>

namespace ovis::core {

namespace {

class resolver {
  program &target;
  std::vector<diagnostic> diagnostics;
  // name → modules defining it.
  std::map<string_type, std::set<string_type>> definitions;
  std::set<string_type> modules;
  std::size_t unit_index = 0;
  string_type module;
  std::vector<std::set<string_type>> locals;

  auto report(source_location p_location, string_type p_message) -> void {
    diagnostics.push_back({unit_index, p_location, std::move(p_message)});
  }

  auto global_name(const string_type &p_module, const string_type &p_name) const
      -> string_type {
    return definitions.at(p_name).size() == 1 ? p_name
                                              : p_module + "." + p_name;
  }

  auto is_local(const string_type &p_name) const -> bool {
    for (const auto &scope : locals)
      if (scope.contains(p_name))
        return true;
    return false;
  }

  // `pricing.tax` is named by `pricing.tax`, `tax`, ...: any tail of its
  // dotted path.
  auto modules_named(string_view_type p_prefix) const
      -> std::vector<string_type> {
    std::vector<string_type> result;
    for (const auto &candidate : modules)
      if (candidate == p_prefix ||
          (candidate.ends_with(p_prefix) &&
           candidate[candidate.size() - p_prefix.size() - 1] == '.'))
        result.push_back(candidate);
    return result;
  }

  auto resolve(string_type &p_name, source_location p_location) -> void {
    if (is_local(p_name))
      return;

    // Qualified: `module.name`.
    if (const auto dot = p_name.rfind('.'); dot != string_type::npos) {
      const auto base = p_name.substr(dot + 1);
      const auto prefix = string_view_type(p_name).substr(0, dot);
      const auto found = definitions.find(base);
      if (found == definitions.end())
        return;
      std::vector<string_type> matches;
      for (const auto &candidate : modules_named(prefix))
        if (found->second.contains(candidate))
          matches.push_back(candidate);
      if (matches.size() == 1)
        p_name = global_name(matches.front(), base);
      else if (matches.size() > 1)
        report(p_location, "`" + p_name + "` could be in " + matches[0] +
                               " or " + matches[1] +
                               "; write the module's full name.");
      return;
    }

    const auto found = definitions.find(p_name);
    if (found == definitions.end())
      return;
    const auto &owners = found->second;
    if (owners.contains(module)) {
      p_name = global_name(module, p_name);
      return;
    }
    if (owners.size() == 1) {
      p_name = global_name(*owners.begin(), p_name);
      return;
    }
    string_type choices;
    for (const auto &owner : owners)
      choices += (choices.empty() ? "" : " or ") + owner + "." + p_name;
    report(p_location, "`" + p_name + "` is defined in more than one file; "
                                      "say which: " +
                           choices + ".");
  }

  auto walk(expression &p_expression) -> void {
    if (p_expression.kind == expression::kind_type::variable)
      resolve(p_expression.text, p_expression.location);
    if (p_expression.kind == expression::kind_type::call &&
        !is_builtin(p_expression.text))
      resolve(p_expression.text, p_expression.location);
    for (auto &argument : p_expression.arguments)
      walk(argument);
  }

  auto walk_block(std::vector<statement> &p_body) -> void {
    locals.emplace_back();
    for (auto &statement : p_body)
      walk(statement, false);
    locals.pop_back();
  }

  auto walk(statement &p_statement, bool p_is_top_level) -> void {
    using kind = statement::kind_type;
    for (auto &value : p_statement.values)
      walk(value);
    switch (p_statement.kind) {
    case kind::let:
      if (p_is_top_level)
        p_statement.name = global_name(module, p_statement.name);
      else
        locals.back().insert(p_statement.name);
      break;
    case kind::set:
    case kind::put:
      resolve(p_statement.name, p_statement.location);
      break;
    case kind::for_:
    case kind::each:
      locals.push_back({p_statement.name});
      walk_block(p_statement.body);
      locals.pop_back();
      return;
    default:
      break;
    }
    walk_block(p_statement.body);
    walk_block(p_statement.else_body);
  }

  // Whether a helper module's top-level value can be computed before the
  // program starts: no input, no randomness, no function calls.
  auto is_definition_value(const expression &p_expression) const -> bool {
    if (p_expression.kind == expression::kind_type::call) {
      const auto &name = p_expression.text;
      if (!is_builtin(name) || name.starts_with("read_") || name == "random")
        return false;
    }
    for (const auto &argument : p_expression.arguments)
      if (!is_definition_value(argument))
        return false;
    return true;
  }

  auto check_helper(const unit &p_unit) -> void {
    for (const auto &statement : p_unit.statements) {
      using kind = statement::kind_type;
      if (statement.kind == kind::pass)
        continue;
      if (statement.kind == kind::let &&
          (statement.values.empty() ||
           is_definition_value(statement.values[0])))
        continue;
      report(statement.location,
             "This is in " + p_unit.module +
                 ", a helper file, which can only define functions and data; "
                 "only main.ovis does things. Move this to main.ovis, or make "
                 "it define something.");
      return;
    }
  }

public:
  explicit resolver(program &p_program) : target(p_program) {}

  auto run() -> std::vector<diagnostic> {
    for (const auto &unit : target.units) {
      if (unit.module.empty())
        return {};
      modules.insert(unit.module);
      for (const auto &function : unit.functions)
        definitions[function.name].insert(unit.module);
      for (const auto &statement : unit.statements)
        if (statement.kind == statement::kind_type::let)
          definitions[statement.name].insert(unit.module);
    }

    for (unit_index = 0; unit_index < target.units.size(); ++unit_index) {
      auto &unit = target.units[unit_index];
      module = unit.module;
      if (module != main_module)
        check_helper(unit);

      locals = {{}};
      for (auto &statement : unit.statements)
        walk(statement, true);

      for (auto &function : unit.functions) {
        locals = {{}};
        for (const auto &parameter : function.parameters)
          locals.back().insert(parameter.name);
        for (auto &statement : function.body)
          walk(statement, false);
        function.name = global_name(module, function.name);
      }
    }
    return std::move(diagnostics);
  }
};

} // namespace

auto resolve_modules(program &p_program) -> std::vector<diagnostic> {
  return resolver(p_program).run();
}

} // namespace ovis::core
