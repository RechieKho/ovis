#include <algorithm>
#include <cctype>
#include <set>
#include <tuple>

#include <ovis/core/checker.hpp>

#include "parser.hpp"

namespace ovis::phrase::internal {

auto milliseconds_since(clock_type::time_point p_start) -> double {
  return std::chrono::duration<double, std::milli>(clock_type::now() - p_start)
      .count();
}

auto lower(string_view_type p_text) -> string_type {
  string_type result(p_text);
  for (auto &character : result)
    character = static_cast<char_type>(
        std::tolower(static_cast<unsigned char>(character)));
  return result;
}

// Words are equal ignoring case and a plural ending: "decimal" is
// "Decimals".
auto same_word(string_view_type p_left, string_view_type p_right) -> bool {
  const auto left = lower(p_left);
  const auto right = lower(p_right);
  if (left == right)
    return true;
  // Short words have no plurals here: "as" isn't more than one "a".
  const auto &shorter = left.size() < right.size() ? left : right;
  const auto &longer = left.size() < right.size() ? right : left;
  return shorter.size() >= 3 &&
         (shorter + "s" == longer || shorter + "es" == longer);
}

auto is_article(const token &p_token) -> bool {
  return p_token.is_word("a") || p_token.is_word("an") ||
         p_token.is_word("the");
}

auto is_article_word(string_view_type p_word) -> bool {
  return p_word == "a" || p_word == "an" || p_word == "the";
}

// Text as written in the source and in Ovis Core: quoted, with escapes.
auto quote(string_view_type p_text) -> string_type {
  string_type result = "\"";
  for (const auto character : p_text) {
    if (character == '"' || character == '\\')
      result += '\\';
    if (character == '\n')
      result += "\\n";
    else if (character == '\t')
      result += "\\t";
    else
      result += character;
  }
  return result + "\"";
}

auto value_kind::core() const -> string_type {
  if (base == "record") {
    auto result = "(record " + record + ")";
    for (std::size_t level = 0; level < depth; ++level)
      result = "(list " + result + ")";
    return result;
  }
  auto result = base == "?" ? string_type("int") : base;
  for (std::size_t level = 0; level < depth; ++level)
    result += "[]";
  return result;
}

auto assignable(const value_kind &p_wanted, const value_kind &p_value) -> bool {
  return !p_wanted.known() || !p_value.known() || p_wanted == p_value ||
         (p_wanted == value_kind{"float"} && p_value == value_kind{"int"}) ||
         // `nothing` where a record goes; Ovis Core checks it is `null`.
         (p_wanted.is_record() && p_value == value_kind{"ptr"});
}

auto vocabulary::common_trait(const std::vector<value_kind> &p_types) const
    -> std::optional<string_type> {
  for (const auto &[name, record] : records) {
    if (!record.is_trait)
      continue;
    if (std::ranges::all_of(p_types, [&](const value_kind &p_type) {
          return p_type.is_record() &&
                 (p_type.record == name ||
                  trait_impls.contains({name, p_type.record}));
        }))
      return name;
  }
  return std::nullopt;
}

auto record_info::find(string_view_type p_word) const -> const record_field * {
  for (const auto &field : fields)
    if (same_word(field.name, p_word))
      return &field;
  return nullptr;
}

auto vocabulary::record_at(const std::vector<token> &p_tokens,
                           std::size_t p_position, std::size_t p_end) const
    -> std::pair<const record_info *, std::size_t> {
  std::pair<const record_info *, std::size_t> best = {nullptr, 0};
  for (const auto &[name, record] : records) {
    const auto length = record.words.size();
    if (p_position + length > p_end || length <= best.second)
      continue;
    bool matches = true;
    for (std::size_t index = 0; index < length && matches; ++index)
      matches = p_tokens[p_position + index].kind == token::kind_type::word &&
                same_word(p_tokens[p_position + index].text,
                          record.words[index]);
    if (matches)
      best = {&record, length};
  }
  return best;
}

auto vocabulary::is_field(string_view_type p_word) const -> bool {
  return std::ranges::any_of(records, [&](const auto &p_record) {
    return p_record.second.find(p_word) != nullptr;
  });
}

auto vocabulary::implementation(string_view_type p_trait,
                                const value_kind &p_type) const
    -> const string_type * {
  if (!p_type.is_record())
    return nullptr;
  const auto trait = impls.find(string_type(p_trait));
  if (trait == impls.end())
    return nullptr;
  const auto found = trait->second.find(p_type.record);
  return found == trait->second.end() ? nullptr : &found->second;
}

auto vocabulary::implements(string_view_type p_trait,
                            const value_kind &p_type) const -> bool {
  if (!p_type.known() || p_trait == "any")
    return true;
  if (p_trait == "num")
    return p_type.is_number();
  // Anything but a C pointer can be shown: a record as it is written, or
  // by the `text of` its author gave it, and a list of such values.
  if (p_trait == "showable")
    return p_type.base != "ptr";
  if (p_type.depth > 0)
    return false;
  if (p_type.base == "int" || p_type.base == "float" || p_type.base == "str")
    return true;
  if (p_trait == "equatable" && p_type.base == "bool")
    return true;
  if (assume_record_traits && p_type.is_record() &&
      (p_trait == "comparable" || p_trait == "equatable"))
    return true;
  return implementation(p_trait, p_type) != nullptr;
}

auto trait_method_of(string_view_type p_name,
                     const std::vector<std::optional<value_kind>> &p_types)
    -> std::optional<trait_method> {
  static const std::map<string_view_type, std::pair<string_view_type, std::size_t>>
      methods = {{"comes_before", {"comparable", 2}},
                 {"is_same_as", {"equatable", 2}},
                 {"text_of", {"showable", 1}}};
  const auto found = methods.find(p_name);
  if (found == methods.end() || p_types.size() != found->second.second)
    return std::nullopt;
  for (const auto &type : p_types)
    if (!type || !type->is_record() || type->record != p_types.front()->record)
      return std::nullopt;
  return trait_method{string_type(found->second.first), p_types.front()->record};
}

auto value_kind::english() const -> string_type {
  if (base == "record") {
    auto type = core::value_type::record_of(record);
    return core::describe_in_english(depth == 0 ? type
                                                : core::value_type::list_of(type));
  }
  static const std::map<string_type, std::pair<string_type, string_type>>
      names = {{"int", {"a whole number", "whole numbers"}},
               {"float", {"a decimal", "decimals"}},
               {"str", {"text", "texts"}},
               {"bool", {"true or false", "truth values"}},
               {"ptr", {"a C pointer", "C pointers"}},
               {"void", {"nothing", "nothing"}},
               {"?", {"not known yet", "values"}}};
  const auto &[one, many] = names.at(base);
  if (depth == 0)
    return one;
  string_type result;
  for (std::size_t level = 1; level < depth; ++level)
    result += "a list of lists of ";
  return (result.empty() ? "a list of " : result) + many;
}

auto kind_of(const core::value_type &p_type) -> value_kind {
  if (p_type == core::value_type::nothing())
    return {"void"};
  if (!p_type.is_known())
    return {};
  if (p_type.base == core::base_type::record)
    return {"record", p_type.is_list ? std::size_t(1) : 0, p_type.record};
  auto name = core::value_type{p_type.base}.to_string();
  return {name, p_type.is_list ? std::size_t(1) : 0};
}

auto kind_of(const core::c_constant &p_constant) -> value_kind {
  switch (p_constant.kind) {
  case core::c_constant::kind_type::integer:
    return {"int"};
  case core::c_constant::kind_type::real:
    return {"float"};
  case core::c_constant::kind_type::null:
    return {"ptr"};
  case core::c_constant::kind_type::runtime:
    // stdin, stdout and stderr are FILE pointers; the rest are numbers.
    return {p_constant.name.starts_with("std") ? "ptr" : "int"};
  }
  return {};
}

auto core_name(const string_type &p_name) -> string_type {
  static const auto reserved = [] {
    std::set<string_type> names = {
        "int",  "integer", "i64",     "float",   "real", "double", "number",
        "f64",  "bool",    "boolean", "str",     "string", "text", "void",
        "nothing", "none", "ptr",     "pointer", "any",  "true",   "false",
        "main"};
    for (const auto keyword : core::core_keywords())
      names.emplace(keyword);
    for (const auto name : core::builtin_names())
      names.emplace(name);
    for (const auto &function : core::libc_functions())
      names.insert(function.name);
    return names;
  }();
  return reserved.contains(p_name) || p_name.starts_with("prelude_")
             ? p_name + "_"
             : p_name;
}

auto add_libc(vocabulary &p_vocabulary) -> void {
  for (const auto &function : core::libc_functions())
    p_vocabulary.functions.try_emplace(
        function.name,
        callable{.core = function.name,
                 .result = kind_of(core::ovis_type_of(function.result)),
                 .arity = function.is_variadic
                              ? std::nullopt
                              : std::optional(function.parameters.size())});
  // Ovis Core's own numeric operations, as C would have them.
  for (const auto &[name, result, arity] :
       std::initializer_list<std::tuple<string_view_type, value_kind, std::size_t>>{
           {"abs", {}, 1},
           {"min", {}, 2},
           {"max", {}, 2},
           {"sqrt", {"float"}, 1},
           {"pow", {"float"}, 2},
           {"floor", {"float"}, 1}})
    p_vocabulary.functions.try_emplace(
        string_type(name),
        callable{.core = string_type(name), .result = result, .arity = arity});
  for (const auto name : {"null", "NULL", "EOF", "RAND_MAX", "CLOCKS_PER_SEC",
                          "stdin", "stdout", "stderr", "pi", "M_PI"})
    if (const auto *constant = core::find_c_constant(name))
      p_vocabulary.constants.try_emplace(name, kind_of(*constant));
}

namespace {

// A child's reading inside its parent's: bracketed where the grouping
// would otherwise be unclear, so readings that differ only in grouping
// read differently.
auto nested_reading(const parse &p_child, bool p_parent_is_phrase)
    -> string_type {
  const auto bracket =
      p_parent_is_phrase ? p_child.shape == parse::shape_type::operation
                         : p_child.shape == parse::shape_type::phrase;
  return bracket ? "(" + p_child.reading + ")" : p_child.reading;
}

struct operator_info {
  int level;
  string_view_type core;
};

auto operator_at(const token &p_token) -> std::optional<operator_info> {
  if (p_token.kind == token::kind_type::word) {
    if (p_token.is_word("or"))
      return operator_info{or_level, "or"};
    if (p_token.is_word("and"))
      return operator_info{and_level, "and"};
    if (p_token.is_word("mod"))
      return operator_info{product_level, "%"};
    return std::nullopt;
  }
  if (p_token.kind != token::kind_type::symbol)
    return std::nullopt;
  static const std::map<string_view_type, operator_info> symbols = {
      {"=", {comparison_level, "="}},  {"==", {comparison_level, "="}},
      {"!=", {comparison_level, "!="}}, {"<", {comparison_level, "<"}},
      {">", {comparison_level, ">"}},   {"<=", {comparison_level, "<="}},
      {">=", {comparison_level, ">="}}, {"+", {sum_level, "+"}},
      {"-", {sum_level, "-"}},          {"*", {product_level, "*"}},
      {"/", {product_level, "/"}},      {"%", {product_level, "%"}}};
  const auto found = symbols.find(p_token.text);
  return found == symbols.end() ? std::nullopt
                                : std::optional(found->second);
}

// A record shown by the `text of` its author gave it, or a list of such
// records shown as `[a, b]`: the Ovis Core that gives the text.
auto shown_by_author(const vocabulary &p_vocabulary, const value_kind &p_type,
                     const string_type &p_core) -> std::optional<string_type> {
  if (const auto *const text = p_vocabulary.implementation("showable", p_type))
    return "(" + *text + " " + p_core + ")";
  if (p_type.depth == 1 && p_type.base == "record" &&
      p_vocabulary.implementation("showable", p_type.element()) != nullptr)
    return "(prelude_list_text:" + p_type.record + " " + p_core + ")";
  return std::nullopt;
}

auto operation_type(string_view_type p_operator, const value_kind &p_left,
                    const value_kind &p_right, const vocabulary &p_vocabulary)
    -> std::optional<value_kind> {
  const auto number_like = [](const value_kind &p_type) {
    return p_type.is_number() || !p_type.known();
  };
  if (p_operator == "and" || p_operator == "or") {
    const auto truth = [](const value_kind &p_type) {
      return !p_type.known() || (p_type.base == "bool" && p_type.depth == 0);
    };
    if (truth(p_left) && truth(p_right))
      return value_kind{"bool"};
    return std::nullopt;
  }
  if (p_operator == "=" || p_operator == "!=" || p_operator == "<" ||
      p_operator == ">" || p_operator == "<=" || p_operator == ">=") {
    const auto ordered = p_operator != "=" && p_operator != "!=";
    // Records compare by the phrases their author wrote.
    if (p_left == p_right &&
        p_vocabulary.implementation(ordered ? "comparable" : "equatable", p_left))
      return value_kind{"bool"};
    // Text and C pointers compare with `null`: `while line != null`.
    const auto pointer_like = [](const value_kind &p_type) {
      return p_type.depth == 0 && (p_type.base == "ptr" || p_type.base == "str");
    };
    if (!ordered && pointer_like(p_left) && pointer_like(p_right) &&
        (p_left.base == "ptr" || p_right.base == "ptr"))
      return value_kind{"bool"};
    if ((number_like(p_left) && number_like(p_right)) ||
        (p_left == p_right && (!ordered || p_left.base == "str")))
      return value_kind{"bool"};
    return std::nullopt;
  }
  // Adding anything to text joins them as text.
  const auto joinable = [&](const value_kind &p_type) {
    return (p_type.depth == 0 &&
            (p_type.base != "record" ||
             p_vocabulary.implementation("showable", p_type) != nullptr)) ||
           shown_by_author(p_vocabulary, p_type, {}).has_value();
  };
  if (p_operator == "+" &&
      ((p_left.base == "str" && p_left.depth == 0) ||
       (p_right.base == "str" && p_right.depth == 0)) &&
      joinable(p_left) && joinable(p_right))
    return value_kind{"str"};
  if (!number_like(p_left) || !number_like(p_right))
    return std::nullopt;
  if (p_operator == "/" || p_left.base == "float" || p_right.base == "float")
    return value_kind{"float"};
  if (p_left.base == "int" && p_right.base == "int")
    return value_kind{"int"};
  return value_kind{};
}

// The Ovis Core of `left OPERATOR right`: the operator, or for records the
// functions their authors wrote (`comes before`, `is the same as`, `text of`).
auto operation_core(string_view_type p_operator, const parse &p_left,
                    const parse &p_right, const vocabulary &p_vocabulary)
    -> string_type {
  const auto call = [](const string_type &p_function, const string_type &p_first,
                       const string_type &p_second) {
    return "(" + p_function + " " + p_first + " " + p_second + ")";
  };
  const auto &left = p_left.core;
  const auto &right = p_right.core;
  if (p_left.type == p_right.type && p_left.type.is_record()) {
    if (p_operator == "=" || p_operator == "!=")
      if (const auto *const same = p_vocabulary.implementation("equatable", p_left.type)) {
        const auto result = call(*same, left, right);
        return p_operator == "=" ? result : "(not " + result + ")";
      }
    if (const auto *const before = p_vocabulary.implementation("comparable", p_left.type)) {
      if (p_operator == "<")
        return call(*before, left, right);
      if (p_operator == ">")
        return call(*before, right, left);
      if (p_operator == "<=")
        return "(not " + call(*before, right, left) + ")";
      if (p_operator == ">=")
        return "(not " + call(*before, left, right) + ")";
    }
  }
  const auto shown = [&](const parse &p_part) {
    if (p_operator == "+")
      if (const auto text =
              shown_by_author(p_vocabulary, p_part.type, p_part.core))
        return *text;
    return p_part.core;
  };
  return call(string_type(p_operator), shown(p_left), shown(p_right));
}

} // namespace

// Type names written in words, for slots of type `type`.
auto type_words(const std::vector<token> &p_tokens, std::size_t p_position,
                std::size_t p_end, const vocabulary *p_vocabulary)
    -> std::vector<std::pair<std::size_t, value_kind>> {
  static const std::vector<std::pair<std::vector<string_view_type>, string_view_type>>
      names = {{{"whole", "number"}, "int"},   {{"integer"}, "int"},
               {{"decimal", "number"}, "float"}, {{"decimal"}, "float"},
               {{"text"}, "str"},               {{"word"}, "str"},
               {{"truth", "value"}, "bool"}};
  std::vector<std::pair<std::size_t, value_kind>> result;
  for (const auto &[words, base] : names) {
    if (p_position + words.size() > p_end)
      continue;
    bool matches = true;
    for (std::size_t index = 0; index < words.size(); ++index)
      matches = matches && p_tokens[p_position + index].kind ==
                               token::kind_type::word &&
                same_word(p_tokens[p_position + index].text, words[index]);
    if (matches)
      result.emplace_back(words.size(), value_kind{string_type(base)});
  }
  if (p_vocabulary != nullptr)
    if (const auto [record, length] =
            p_vocabulary->record_at(p_tokens, p_position, p_end);
        record != nullptr)
      result.emplace_back(length, record->type());
  return result;
}

namespace {

auto accepts(const type_name &p_wanted, const value_kind &p_value,
             std::map<string_type, value_kind> &p_variables,
             const phrase_template &p_template, const slot_fills &p_fills,
             const vocabulary &p_vocabulary) -> bool {
  if (!p_value.known())
    return true;
  if (p_wanted.kind == type_name::kind_type::variable) {
    if (p_value.depth < p_wanted.list_depth)
      return false;
    const auto element = p_value.with_depth(p_value.depth - p_wanted.list_depth);
    if (!p_wanted.bound.empty() && !p_vocabulary.implements(p_wanted.bound, element))
      return false;
    const auto [bound, added] = p_variables.try_emplace(p_wanted.name, element);
    return added || !bound->second.known() || bound->second == element;
  }
  if (p_wanted.kind == type_name::kind_type::type_slot) {
    const auto index = static_cast<std::size_t>(
        std::ranges::find(p_template.slots, p_wanted.name, &slot::name) -
        p_template.slots.begin());
    const auto &chosen = p_fills.types[index];
    return !chosen || p_value == chosen->with_depth(p_wanted.list_depth);
  }
  if (p_wanted.name == "any")
    return p_wanted.list_depth == 0 || p_value.depth == p_wanted.list_depth;
  if (p_wanted.name == "showable")
    return p_vocabulary.implements("showable", p_value);
  if (is_record_trait_name(p_wanted.name))
    return p_value.depth == p_wanted.list_depth &&
           p_vocabulary.implements(p_wanted.name, p_value);
  if (p_value.depth != p_wanted.list_depth)
    return false;
  const auto &base = p_wanted.name;
  // A whole number may stand for a decimal, but a list of whole numbers
  // isn't a list of decimals.
  return base == p_value.base ||
         (base == "num" && (p_value.base == "int" || p_value.base == "float")) ||
         (base == "float" && p_value.base == "int" && p_value.depth == 0);
}

auto result_type(const phrase_template &p_template,
                 const std::map<string_type, value_kind> &p_variables,
                 const slot_fills &p_fills) -> value_kind {
  const auto &wanted = *p_template.result;
  switch (wanted.kind) {
  case type_name::kind_type::builtin:
    if (wanted.name == "num" || wanted.name == "any")
      return {};
    return {wanted.name, wanted.list_depth};
  case type_name::kind_type::variable: {
    const auto bound = p_variables.find(wanted.name);
    if (bound == p_variables.end() || !bound->second.known())
      return {};
    return bound->second.with_depth(bound->second.depth + wanted.list_depth);
  }
  case type_name::kind_type::type_slot: {
    const auto index = static_cast<std::size_t>(
        std::ranges::find(p_template.slots, wanted.name, &slot::name) -
        p_template.slots.begin());
    const auto &chosen = p_fills.types[index];
    return chosen ? chosen->with_depth(wanted.list_depth) : value_kind{};
  }
  case type_name::kind_type::field:
    break;
  }
  return {};
}

// The template's Ovis Core with the slots filled in; for records, its
// `for records:` Core. A trait-typed slot written after a `_` or a `:`
// stands for its type's name, so `contains_$xs` is `contains_int` for
// whole numbers and `comes_before__$a` the function of a record `book`.
auto expand(const phrase_template &p_template, const slot_fills &p_fills,
            bool p_record_mode) -> string_type {
  string_type result;
  const auto &core = p_record_mode ? p_template.record_core : p_template.core;
  for (std::size_t index = 0; index < core.size();) {
    if (core[index] != '$') {
      result += core[index++];
      continue;
    }
    const auto after_name = index > 0 && (core[index - 1] == '_' || core[index - 1] == ':');
    auto end = index + 1;
    while (end < core.size() &&
           (std::isalnum(static_cast<unsigned char>(core[end])) ||
            core[end] == '_'))
      ++end;
    const auto name = core.substr(index + 1, end - index - 1);
    if (core.compare(end, 3, "...") == 0)
      end += 3;
    index = end;
    const auto slot = static_cast<std::size_t>(
        std::ranges::find(p_template.slots, name, &slot::name) -
        p_template.slots.begin());
    if (p_fills.types[slot]) {
      result += p_fills.types[slot]->core();
      continue;
    }
    const auto &type = p_template.slots[slot].type;
    if (after_name && is_trait_slot(type) && !p_fills.values[slot].empty()) {
      const auto &value = p_fills.values[slot].front().type;
      const auto element = value.with_depth(value.depth - type.list_depth);
      result += element.is_record() ? element.record : element.base;
      continue;
    }
    for (std::size_t item = 0; item < p_fills.values[slot].size(); ++item)
      result += (item > 0 ? " " : "") + p_fills.values[slot][item].core;
  }
  return result;
}

// The template's reading: its canonical words (first choices, no optional
// words) with the slots' readings.
auto reading_of(const phrase_template &p_template, const pattern &p_pattern,
                const slot_fills &p_fills) -> string_type {
  string_type result;
  const auto add = [&](const string_type &p_text) {
    result += (result.empty() ? "" : " ") + p_text;
  };
  for (std::size_t index = 0; index < p_pattern.size(); ++index) {
    const auto &element = p_pattern[index];
    switch (element.kind) {
    case pattern_element::kind_type::word:
      add(element.word);
      break;
    case pattern_element::kind_type::choice:
      for (const auto &word : element.branches[0])
        add(word.word);
      break;
    case pattern_element::kind_type::optional:
      break;
    case pattern_element::kind_type::slot: {
      if (const auto &type = p_fills.types[element.slot]) {
        add(type->english().substr(type->english().starts_with("a ") ? 2 : 0));
        break;
      }
      // A phrase with a slot at its edge, in a slot at this one's edge,
      // could also group the other way: `add tax to (x with 2 decimals)`
      // and `(add tax to x) with 2 decimals` must read differently.
      // Commas and the end of a statement already delimit the rest.
      const auto at_edge =
          (index == 0 || index + 1 == p_pattern.size()) &&
          p_template.kind == phrase_template::kind_type::expression &&
          !p_template.slots[element.slot].is_variadic;
      string_type values;
      for (const auto &value : p_fills.values[element.slot])
        values += (values.empty() ? "" : ", ") +
                  ((at_edge && value.shape == parse::shape_type::phrase &&
                    value.open_edge) ||
                           (p_template.slots[element.slot].is_variadic &&
                            value.reading.find(", ") != string_type::npos)
                       ? "(" + value.reading + ")"
                       : nested_reading(value, true));
      add(values);
      break;
    }
    }
  }
  return result;
}

} // namespace

// The parse a template gives with these fills, if their types fit.
auto build_phrase(const phrase_template &p_template, const pattern &p_pattern,
                  const slot_fills &p_fills, const vocabulary &p_vocabulary)
    -> std::optional<parse> {
  std::map<string_type, value_kind> variables;
  for (std::size_t index = 0; index < p_template.slots.size(); ++index)
    for (const auto &value : p_fills.values[index])
      if (p_template.slots[index].type.kind != type_name::kind_type::field &&
          !accepts(p_template.slots[index].type, value.type, variables,
                   p_template, p_fills, p_vocabulary))
        return std::nullopt;

  // Slots typed by a trait that hold records choose the template's record
  // Ovis Core; they must all hold one record, as `a comes before b` does.
  bool record_mode = false;
  {
    std::optional<string_type> chosen;
    bool other = false;
    for (std::size_t index = 0; index < p_template.slots.size(); ++index) {
      const auto &type = p_template.slots[index].type;
      if (!is_trait_slot(type))
        continue;
      for (const auto &value : p_fills.values[index]) {
        if (!value.type.known() || value.type.depth < type.list_depth)
          continue;
        const auto element =
            value.type.with_depth(value.type.depth - type.list_depth);
        if (!element.is_record()) {
          other = other || element.known();
          continue;
        }
        if (type.name == "showable")
          continue;
        if (chosen && *chosen != element.record)
          return std::nullopt;
        chosen = element.record;
      }
    }
    if (chosen && (other || p_template.record_core.empty()))
      return std::nullopt;
    record_mode = chosen.has_value();
    if (p_template.records_only && !record_mode)
      return std::nullopt;
  }

  // A field slot holds a word: a field of the record its type names, once
  // the other slots say which record that is. It expands to RECORD:FIELD,
  // for a generic prelude function (phrases/*.generic).
  auto fills = p_fills;
  // A record shown by the `text of` its author gave it is that text.
  for (std::size_t index = 0; index < p_template.slots.size(); ++index)
    if (p_template.slots[index].type.name == "showable")
      for (auto &value : fills.values[index])
        if (const auto text =
                shown_by_author(p_vocabulary, value.type, value.core)) {
          value.core = *text;
          value.type = {"str"};
        }
  std::map<string_type, value_kind> field_types;
  string_type hole_record;
  for (std::size_t index = 0; index < p_template.slots.size(); ++index) {
    const auto &type = p_template.slots[index].type;
    if (type.kind != type_name::kind_type::field || fills.values[index].empty())
      continue;
    std::optional<value_kind> owner;
    if (std::isupper(static_cast<unsigned char>(type.record.front()))) {
      if (const auto bound = variables.find(type.record); bound != variables.end())
        owner = bound->second;
    } else {
      const auto slot = static_cast<std::size_t>(
          std::ranges::find(p_template.slots, type.record, &slot::name) -
          p_template.slots.begin());
      owner = p_fills.types[slot];
    }
    if (!owner || !owner->is_record())
      return std::nullopt;
    const auto &record = p_vocabulary.records.at(owner->record);
    auto &value = fills.values[index].front();
    if (value.holes > 0) {
      hole_record = record.name;
      continue;
    }
    const auto *const field = record.find(value.reading);
    if (field == nullptr)
      return std::nullopt;
    if (type.name != "any" &&
        !accepts({.name = type.name}, field->type, variables, p_template,
                 p_fills, p_vocabulary))
      return std::nullopt;
    value.core = record.name + ":" + field->name;
    field_types[p_template.slots[index].name] = field->type;
  }

  parse result{.shape = parse::shape_type::phrase};
  result.open_edge =
      p_pattern.front().kind == pattern_element::kind_type::slot ||
      p_pattern.back().kind == pattern_element::kind_type::slot;
  result.core = expand(p_template, fills, record_mode);
  result.reading = reading_of(p_template, p_pattern, fills);
  if (p_template.result) {
    const auto field = field_types.find(p_template.result->name);
    result.type = p_template.result->kind == type_name::kind_type::type_slot &&
                          field != field_types.end()
                      ? field->second
                      : result_type(p_template, variables, fills);
  }
  // A phrase that starts or ends with a slot binds like what fills it, so
  // `square root of x + 1` reads both ways and becomes a question.
  const auto edge_level = [&](const pattern_element &p_element) {
    if (p_element.kind != pattern_element::kind_type::slot ||
        p_fills.values[p_element.slot].empty())
      return int(primary_level);
    int level = primary_level;
    for (const auto &value : p_fills.values[p_element.slot])
      level = std::min(level, value.level);
    return level;
  };
  result.level = std::min(edge_level(p_pattern.front()),
                          edge_level(p_pattern.back()));
  result.uses.push_back(&p_template);
  for (const auto &values : p_fills.values)
    for (const auto &value : values)
      result.use_all(value);
  for (const auto &values : p_fills.values)
    for (const auto &value : values)
      result.take_holes(value);
  if (!hole_record.empty())
    result.hole_record = hole_record;
  return result;
}

struct line_parser::state {
  const std::vector<token> &tokens;
  const std::map<string_type, value_kind> &scope;
  const vocabulary &words;
  bool allow_holes;
  // Bracket nesting of each token (brackets count as the outer level), and
  // each bracket's partner.
  std::vector<std::size_t> depth;
  std::vector<std::size_t> partner;
  std::map<std::pair<std::size_t, std::size_t>, std::vector<parse>> chart;

  static constexpr std::size_t max_readings = 12;

  auto balanced(std::size_t p_begin, std::size_t p_end) const -> bool {
    for (auto index = p_begin; index < p_end; ++index)
      if (partner[index] != string_type::npos &&
          (partner[index] < p_begin || partner[index] >= p_end))
        return false;
    return true;
  }

  auto word_at(std::size_t p_position, std::size_t p_end,
               string_view_type p_word) const -> bool {
    if (p_position >= p_end)
      return false;
    // A user phrase's header may have commas: `the biggest of (a), (b) and
    // (c)`.
    if (p_word == ",")
      return tokens[p_position].is_symbol(",");
    return tokens[p_position].kind == token::kind_type::word &&
           same_word(tokens[p_position].text, p_word);
  }

  auto match_words(const pattern &p_words, std::size_t p_position,
                   std::size_t p_end) const -> std::optional<std::size_t> {
    for (const auto &word : p_words) {
      if (!word_at(p_position, p_end, word.word))
        return std::nullopt;
      ++p_position;
    }
    return p_position;
  }

  auto atom(const token &p_token) const -> std::optional<parse> {
    switch (p_token.kind) {
    case token::kind_type::number:
      return parse{.core = p_token.text,
                   .reading = p_token.text,
                   .type = {p_token.text.find('.') == string_type::npos
                                ? "int"
                                : "float"}};
    case token::kind_type::text:
      return parse{.core = quote(p_token.text),
                   .reading = quote(p_token.text),
                   .type = {"str"}};
    case token::kind_type::word:
      if (p_token.is_word("true") || p_token.is_word("false"))
        return parse{.core = lower(p_token.text),
                     .reading = lower(p_token.text),
                     .type = {"bool"}};
      if (const auto found = scope.find(p_token.text); found != scope.end())
        return parse{.core = core_name(p_token.text),
                     .reading = p_token.text,
                     .type = found->second};
      // No record: Ovis Core's `null`, which a record's place may hold.
      if (p_token.is_word("nothing"))
        return parse{.core = "null", .reading = "nothing", .type = {"ptr"}};
      if (const auto found = words.constants.find(p_token.text);
          found != words.constants.end())
        return parse{.core = p_token.text,
                     .reading = p_token.text,
                     .type = found->second};
      return std::nullopt;
    case token::kind_type::symbol:
      return std::nullopt;
    }
    return std::nullopt;
  }

  // `name(arguments)`: a call of a user or C function. A function giving
  // nothing back is only a statement.
  auto calls(std::size_t p_begin, std::size_t p_end, bool p_statement,
             std::vector<parse> &p_results) -> void {
    if (p_end - p_begin < 3 || tokens[p_begin].kind != token::kind_type::word ||
        !tokens[p_begin + 1].is_symbol("(") || partner[p_begin + 1] != p_end - 1)
      return;
    const auto found = words.functions.find(tokens[p_begin].text);
    if (found == words.functions.end() ||
        (!p_statement && found->second.result.base == "void"))
      return;
    const auto &function = found->second;
    const auto choices = p_begin + 2 < p_end - 1
                             ? combinations(items(p_begin + 2, p_end - 1))
                             : std::vector<std::vector<parse>>{{}};
    for (const auto &arguments : choices) {
      if (function.arity && arguments.size() != *function.arity)
        continue;
      parse call{.core = "(" + function.core,
                 .reading = tokens[p_begin].text + "(",
                 .type = function.result};
      for (std::size_t index = 0; index < arguments.size(); ++index) {
        const auto &argument = arguments[index];
        call.core += " " + argument.core;
        call.reading += (index > 0 ? ", " : "") + argument.reading;
        call.use_all(argument);
        call.take_holes(argument);
      }
      call.core += ")";
      call.reading += ")";
      if (call.holes <= 1)
        p_results.push_back(std::move(call));
    }
  }

  // The comma-separated items of a span at the span's own nesting level.
  auto items(std::size_t p_begin, std::size_t p_end)
      -> std::vector<std::vector<parse>> {
    std::vector<std::vector<parse>> result;
    auto start = p_begin;
    for (auto index = p_begin; index <= p_end; ++index)
      if (index == p_end ||
          (tokens[index].is_symbol(",") && depth[index] == depth[p_begin])) {
        result.push_back(start < index ? expressions(start, index)
                                       : std::vector<parse>{});
        start = index + 1;
      }
    return result;
  }

  // The items of a variadic slot: split at every comma, and also with
  // neighbouring pieces kept together, as a phrase may have commas itself
  // (`show the biggest of 4, 9 and 2`). Up to 4 commas are tried both ways.
  auto groupings(std::size_t p_begin, std::size_t p_end)
      -> std::vector<std::vector<std::vector<parse>>> {
    std::vector<std::size_t> commas;
    for (auto index = p_begin; index < p_end; ++index)
      if (tokens[index].is_symbol(",") && depth[index] == depth[p_begin])
        commas.push_back(index);
    if (commas.size() > 4) {
      // Too many to try every way: split at every comma, and also keep a
      // record's fields together (`a book with title "A", pages 1, a book
      // with …`): a comma before a field's name after a `with`.
      std::vector<std::vector<parse>> records;
      auto start = p_begin;
      bool after_with = false;
      for (auto index = p_begin; index <= p_end; ++index) {
        if (index < p_end && depth[index] == depth[p_begin] &&
            tokens[index].is_word("with"))
          after_with = true;
        const auto split =
            index == p_end ||
            (tokens[index].is_symbol(",") && depth[index] == depth[p_begin] &&
             !(after_with && index + 1 < p_end &&
               words.is_field(tokens[index + 1].text)));
        if (!split)
          continue;
        records.push_back(start < index ? expressions(start, index)
                                        : std::vector<parse>{});
        start = index + 1;
        after_with = false;
      }
      return {items(p_begin, p_end), std::move(records)};
    }
    std::vector<std::vector<std::vector<parse>>> result;
    for (std::size_t mask = (std::size_t(1) << commas.size()); mask-- > 0;) {
      std::vector<std::vector<parse>> pieces;
      auto start = p_begin;
      for (std::size_t index = 0; index <= commas.size(); ++index) {
        if (index < commas.size() && (mask & (std::size_t(1) << index)) == 0)
          continue;
        const auto stop = index < commas.size() ? commas[index] : p_end;
        pieces.push_back(start < stop ? expressions(start, stop)
                                      : std::vector<parse>{});
        start = stop + 1;
      }
      result.push_back(std::move(pieces));
    }
    return result;
  }

  // Every way to pick one reading per item, up to a few.
  static auto combinations(const std::vector<std::vector<parse>> &p_items)
      -> std::vector<std::vector<parse>> {
    std::vector<std::vector<parse>> result = {{}};
    for (const auto &item : p_items) {
      std::vector<std::vector<parse>> next;
      for (const auto &partial : result)
        for (const auto &choice : item) {
          if (next.size() >= 4)
            break;
          next.push_back(partial);
          next.back().push_back(choice);
        }
      result = std::move(next);
    }
    return result;
  }

  // The fewest tokens the elements from `p_from` on can match.
  static auto minimum_length(const pattern &p_pattern, std::size_t p_from)
      -> std::size_t {
    std::size_t total = 0;
    for (auto index = p_from; index < p_pattern.size(); ++index) {
      const auto &element = p_pattern[index];
      switch (element.kind) {
      case pattern_element::kind_type::word:
        total += is_article_word(element.word) ? 0 : 1;
        break;
      case pattern_element::kind_type::slot:
        total += 1;
        break;
      case pattern_element::kind_type::optional:
        break;
      case pattern_element::kind_type::choice: {
        auto shortest = element.branches[0].size();
        for (const auto &branch : element.branches)
          shortest = std::min(shortest, branch.size());
        total += shortest;
        break;
      }
      }
    }
    return total;
  }

  auto match(const phrase_template &p_template, const pattern &p_pattern,
             std::size_t p_element, std::size_t p_position, std::size_t p_end,
             slot_fills &p_fills, std::vector<slot_fills> &p_complete) -> void {
    if (p_complete.size() >= 16)
      return;
    // An article the pattern doesn't ask for may be there anyway.
    if (p_position < p_end && is_article(tokens[p_position]) &&
        !(p_element < p_pattern.size() &&
          p_pattern[p_element].kind == pattern_element::kind_type::word &&
          word_at(p_position, p_end, p_pattern[p_element].word)))
      match(p_template, p_pattern, p_element, p_position + 1, p_end, p_fills,
            p_complete);
    if (p_element == p_pattern.size()) {
      if (p_position == p_end)
        p_complete.push_back(p_fills);
      return;
    }
    const auto &element = p_pattern[p_element];
    const auto next = [&](std::size_t p_after) {
      match(p_template, p_pattern, p_element + 1, p_after, p_end, p_fills,
            p_complete);
    };
    switch (element.kind) {
    case pattern_element::kind_type::word:
      if (word_at(p_position, p_end, element.word))
        next(p_position + 1);
      // And an article the pattern asks for may be left out.
      else if (is_article_word(element.word))
        next(p_position);
      break;
    case pattern_element::kind_type::optional:
      if (const auto after = match_words(element.branches[0], p_position, p_end))
        next(*after);
      next(p_position);
      break;
    case pattern_element::kind_type::choice:
      for (const auto &branch : element.branches)
        if (const auto after = match_words(branch, p_position, p_end))
          next(*after);
      break;
    case pattern_element::kind_type::slot: {
      const auto &slot = p_template.slots[element.slot];
      auto &values = p_fills.values[element.slot];
      const auto holes_so_far = [&] {
        std::size_t count = 0;
        for (const auto &filled : p_fills.values)
          for (const auto &value : filled)
            count += value.holes;
        return count;
      };
      if (slot.type.name == "type" &&
          slot.type.kind == type_name::kind_type::builtin) {
        for (const auto &[length, type] :
             type_words(tokens, p_position, p_end, &words)) {
          p_fills.types[element.slot] = type;
          next(p_position + length);
        }
        p_fills.types[element.slot].reset();
        break;
      }
      // A field's name: one word, checked against the record once the
      // other slots are filled (`build_phrase`). While looking for what is
      // unclear, a word no record has may be a hole there.
      if (slot.type.kind == type_name::kind_type::field) {
        if (p_position < p_end &&
            tokens[p_position].kind == token::kind_type::word) {
          if (words.is_field(tokens[p_position].text)) {
            values = {parse{.core = tokens[p_position].text,
                            .reading = tokens[p_position].text}};
            next(p_position + 1);
          } else if (allow_holes && holes_so_far() == 0) {
            values = {hole(p_position, p_position + 1)};
            next(p_position + 1);
          }
          values.clear();
        }
        break;
      }
      // Leave room for the words the rest of the pattern needs.
      const auto needed = minimum_length(p_pattern, p_element + 1);
      for (auto end = p_position + 1; end + needed <= p_end; ++end) {
        if (!balanced(p_position, end))
          continue;
        if (slot.is_variadic) {
          for (const auto &grouping : groupings(p_position, end))
            for (auto &choice : combinations(grouping)) {
              values = std::move(choice);
              if (!values.empty() && holes_so_far() <= 1)
                next(end);
            }
        } else {
          for (const auto &value : expressions(p_position, end)) {
            values = {value};
            if (holes_so_far() <= 1)
              next(end);
          }
        }
        values.clear();
      }
      break;
    }
    }
  }

  auto add_phrases(phrase_template::kind_type p_kind, bool p_block,
                   std::size_t p_begin, std::size_t p_end,
                   std::vector<parse> &p_results) {
    for (const auto &item : words.templates) {
      if (item.kind != p_kind ||
          (p_kind == phrase_template::kind_type::statement &&
           item.takes_block != p_block))
        continue;
      for (const auto &pattern : item.patterns) {
        slot_fills fills;
        fills.values.resize(item.slots.size());
        fills.types.resize(item.slots.size());
        std::vector<slot_fills> complete;
        match(item, pattern, 0, p_begin, p_end, fills, complete);
        for (const auto &found : complete)
          if (auto built = build_phrase(item, item.patterns[0], found, words))
            p_results.push_back(std::move(*built));
      }
    }
  }

  // Readings that read the same can't be told apart by the author, so the
  // first one (in template order) stands for them.
  static auto unique(std::vector<parse> &p_results) -> void {
    std::set<string_type> cores;
    std::set<string_type> readings;
    std::erase_if(p_results, [&](const parse &p_parse) {
      const auto new_core = cores.insert(p_parse.core).second;
      const auto new_reading = readings.insert(p_parse.reading).second;
      return !new_core || !new_reading;
    });
  }

  state(const std::vector<token> &p_tokens,
        const std::map<string_type, value_kind> &p_scope,
        const vocabulary &p_vocabulary, bool p_allow_holes)
      : tokens(p_tokens), scope(p_scope), words(p_vocabulary),
        allow_holes(p_allow_holes),
        depth(p_tokens.size(), 0), partner(p_tokens.size(), string_type::npos) {
    std::vector<std::size_t> open;
    std::size_t level = 0;
    for (std::size_t index = 0; index < tokens.size(); ++index) {
      if (tokens[index].is_symbol("(") || tokens[index].is_symbol("[")) {
        depth[index] = level++;
        open.push_back(index);
      } else if (tokens[index].is_symbol(")") || tokens[index].is_symbol("]")) {
        depth[index] = --level;
        partner[index] = open.back();
        partner[open.back()] = index;
        open.pop_back();
      } else {
        depth[index] = level;
      }
    }
  }

  static auto hole(std::size_t p_begin, std::size_t p_end) -> parse {
    return {.shape = parse::shape_type::hole,
            .core = "HOLE",
            .reading = "…",
            .holes = 1,
            .hole_begin = p_begin,
            .hole_end = p_end};
  }

  // A field of a record: `price of b`, `b's price`, `b.price`.
  auto field_accesses(std::size_t p_begin, std::size_t p_end,
                      std::vector<parse> &p_results) -> void {
    if (p_end - p_begin < 3)
      return;
    if (tokens[p_begin].kind == token::kind_type::word &&
        tokens[p_begin + 1].is_word("of"))
      for (const auto &object : expressions(p_begin + 2, p_end))
        add_field(object, p_begin, "of", p_results);
    if (tokens[p_end - 1].kind == token::kind_type::word &&
        (tokens[p_end - 2].is_symbol("'s") || tokens[p_end - 2].is_symbol(".")))
      for (const auto &object : expressions(p_begin, p_end - 2))
        if (object.level == primary_level)
          add_field(object, p_end - 1, tokens[p_end - 2].text, p_results);
  }

  auto add_field(const parse &p_object, std::size_t p_at,
                 string_view_type p_style, std::vector<parse> &p_results)
      -> void {
    const auto &word = tokens[p_at].text;
    string_type name = word;
    value_kind type;
    parse result{.level = primary_level};
    if (p_object.type.known()) {
      if (!p_object.type.is_record())
        return;
      const auto &record = words.records.at(p_object.type.record);
      const auto *const field = record.find(word);
      if (field == nullptr) {
        // Not one of its fields: while looking for what is unclear, the
        // word is, and the record's fields are what it may be.
        if (!allow_holes || p_object.holes > 0)
          return;
        result.take_holes(hole(p_at, p_at + 1));
        result.hole_record = record.name;
        name = "HOLE";
      } else {
        name = field->name;
        type = field->type;
      }
    } else {
      // Not known yet (a phrase's parameter): any record with the field,
      // and its type if only one has it.
      std::vector<const record_field *> found;
      for (const auto &[record_name, record] : words.records)
        if (const auto *const field = record.find(word))
          found.push_back(field);
      if (found.empty())
        return;
      name = found.front()->name;
      if (found.size() == 1)
        type = found.front()->type;
    }
    result.core = "(field " + p_object.core + " " + name + ")";
    result.type = type;
    if (p_style == "of") {
      // Like a phrase ending in a slot: `price of b + 1` groups both ways.
      result.shape = parse::shape_type::phrase;
      result.open_edge = true;
      result.level = std::min(int(primary_level), p_object.level);
      result.reading = word + " of " + nested_reading(p_object, true);
    } else {
      result.reading = p_object.reading + (p_style == "." ? "." : "'s ") + word;
    }
    result.take_holes(p_object);
    result.use_all(p_object);
    p_results.push_back(std::move(result));
  }

  // `a book with title "Dune", pages 412`: a new record. Fields left out
  // get their defaults; one without a default is the checker's to report.
  auto new_records(std::size_t p_begin, std::size_t p_end,
                   std::vector<parse> &p_results) -> void {
    const auto [record, length] = words.record_at(tokens, p_begin, p_end);
    const auto with = p_begin + length;
    if (record == nullptr || with + 3 > p_end || !tokens[with].is_word("with"))
      return;
    // Items are split at commas, and at `and` before one of its fields.
    std::vector<std::pair<std::size_t, std::size_t>> spans;
    auto start = with + 1;
    for (auto index = start; index <= p_end; ++index)
      if (index == p_end ||
          (depth[index] == depth[p_begin] &&
           (tokens[index].is_symbol(",") ||
            (tokens[index].is_word("and") && index + 1 < p_end &&
             record->find(tokens[index + 1].text) != nullptr)))) {
        spans.emplace_back(start, index);
        start = index + 1;
      }
    std::vector<std::vector<parse>> items;
    for (auto [begin, end] : spans) {
      if (begin < end && tokens[begin].is_word("and"))
        ++begin;
      if (end - begin < 2 || tokens[begin].kind != token::kind_type::word)
        return;
      const auto *const field = record->find(tokens[begin].text);
      if (field == nullptr && !allow_holes)
        return;
      std::vector<parse> choices;
      for (const auto &value : expressions(begin + 1, end)) {
        if (field != nullptr && !assignable(field->type, value.type))
          continue;
        const auto bracket = value.reading.find(", ") != string_type::npos;
        parse item{.core = "(" + (field ? field->name : string_type("HOLE")) +
                           " " + value.core + ")",
                   .reading = tokens[begin].text + " " +
                              (bracket ? "(" + value.reading + ")"
                                       : nested_reading(value, true)),
                   .type = field ? field->type : value_kind{},
                   .level = value.level};
        if (field == nullptr) {
          if (value.holes > 0)
            continue;
          item.take_holes(hole(begin, begin + 1));
          item.hole_record = record->name;
        }
        item.take_holes(value);
        item.use_all(value);
        choices.push_back(std::move(item));
      }
      if (choices.empty())
        return;
      items.push_back(std::move(choices));
    }
    const auto article =
        core::describe_in_english(core::value_type::record_of(record->name));
    for (const auto &chosen : combinations(items)) {
      parse result{.shape = parse::shape_type::phrase,
                   .core = "(new " + record->name,
                   .reading = article + " with",
                   .type = record->type(),
                   .open_edge = true};
      std::set<string_type> given;
      bool is_repeated = false;
      for (std::size_t index = 0; index < chosen.size(); ++index) {
        const auto &item = chosen[index];
        result.core += " " + item.core;
        result.reading += (index > 0 ? ", " : " ") + item.reading;
        result.level = item.level;
        result.take_holes(item);
        result.use_all(item);
        is_repeated |=
            !given.insert(item.core.substr(1, item.core.find(' ') - 1)).second;
      }
      // A field is given once: that also keeps the unknown field of a line
      // from being offered the fields already there.
      if (result.holes > 1 || is_repeated)
        continue;
      for (const auto &field : record->fields)
        if (!given.contains(field.name) && field.default_core)
          result.core += " (" + field.name + " " + *field.default_core + ")";
      result.core += ")";
      p_results.push_back(std::move(result));
    }
  }

  // `price of b = 9.5`: sets a field.
  auto set_fields(std::size_t p_begin, std::size_t p_end,
                  std::vector<parse> &p_results) -> void {
    for (auto split = p_begin + 1; split + 1 < p_end; ++split) {
      if (depth[split] != depth[p_begin] || !tokens[split].is_symbol("="))
        continue;
      for (const auto &target : expressions(p_begin, split)) {
        if (!target.core.starts_with("(field "))
          continue;
        for (const auto &value : expressions(split + 1, p_end)) {
          if (!assignable(target.type, value.type) ||
              target.holes + value.holes > 1)
            continue;
          parse result{.core = "(set-field " +
                               target.core.substr(7, target.core.size() - 8) +
                               " " + value.core + ")",
                       .reading = target.reading + " = " + value.reading};
          result.take_holes(target);
          result.take_holes(value);
          result.use_all(target);
          result.use_all(value);
          p_results.push_back(std::move(result));
        }
      }
    }
  }

  auto expressions(std::size_t p_begin, std::size_t p_end)
      -> const std::vector<parse> & {
    const auto key = std::pair(p_begin, p_end);
    if (const auto found = chart.find(key); found != chart.end())
      return found->second;
    // A span being read doesn't contain itself: an empty entry stands until
    // it is done.
    chart[key];
    std::vector<parse> results;
    if (p_begin >= p_end || !balanced(p_begin, p_end))
      return chart[key] = results;

    if (is_article(tokens[p_begin]) && p_end - p_begin > 1)
      std::ranges::copy(expressions(p_begin + 1, p_end),
                        std::back_inserter(results));
    if (p_end - p_begin == 1)
      if (auto value = atom(tokens[p_begin]))
        results.push_back(std::move(*value));

    // `ref x` passes a variable's address to a C function, for output
    // parameters; only calls take it.
    if (p_end - p_begin == 2 && tokens[p_begin].is_word("ref") &&
        p_begin > 0 && (tokens[p_begin - 1].is_symbol("(") ||
                        tokens[p_begin - 1].is_symbol(","))) {
      if (const auto found = scope.find(tokens[p_begin + 1].text);
          found != scope.end())
        results.push_back({.core = "(ref " + core_name(found->first) + ")",
                           .reading = "ref " + found->first,
                           .type = {"ptr"}});
    }

    // (…) and […]
    if (partner[p_begin] == p_end - 1 && tokens[p_begin].is_symbol("("))
      for (auto inner : expressions(p_begin + 1, p_end - 1)) {
        inner.reading = "(" + inner.reading + ")";
        inner.shape = parse::shape_type::atom;
        inner.level = primary_level;
        results.push_back(std::move(inner));
      }
    if (partner[p_begin] == p_end - 1 && tokens[p_begin].is_symbol("[") &&
        p_end - p_begin > 2)
      // Grouped as a variadic slot's items: an item may have commas (`[a
      // book with title "Emma", pages 474]`).
      for (const auto &grouping : groupings(p_begin + 1, p_end - 1))
      for (const auto &elements : combinations(grouping)) {
        if (elements.empty())
          continue;
        parse list{.core = "(list", .reading = "["};
        auto element = elements[0].type;
        bool fits = true;
        for (std::size_t index = 0; index < elements.size(); ++index) {
          const auto &item = elements[index];
          list.core += " " + item.core;
          list.reading += (index > 0 ? ", " : "") + item.reading;
          list.take_holes(item);
          list.use_all(item);
          if (element.is_number() && item.type.is_number() &&
              item.type.base == "float")
            element = item.type;
          else if (item.type != element && item.type.known() &&
                   !(element.is_number() && item.type.is_number()))
            fits = false;
        }
        list.core += ")";
        list.reading += "]";
        // Different records that are all one trait: a list of that trait.
        if (!fits && element.is_record()) {
          std::vector<value_kind> kinds;
          for (const auto &item : elements)
            kinds.push_back(item.type);
          if (const auto trait = words.common_trait(kinds)) {
            element = words.records.at(*trait).type();
            fits = true;
          }
        }
        list.type = element.known() ? element.with_depth(element.depth + 1)
                                    : value_kind{};
        if (fits)
          results.push_back(std::move(list));
      }

    // not …, - …
    if (tokens[p_begin].is_word("not"))
      for (const auto &operand : expressions(p_begin + 1, p_end))
        if (operand.level >= not_level &&
            operation_type("and", operand.type, operand.type, words))
          results.push_back({.shape = parse::shape_type::operation,
                             .core = "(not " + operand.core + ")",
                             .reading = "not " + nested_reading(operand, false),
                             .type = {"bool"},
                             .level = not_level,
                             .holes = operand.holes,
                             .hole_begin = operand.hole_begin,
                             .hole_end = operand.hole_end,
                             .hole_record = operand.hole_record,
                             .uses = operand.uses});
    if (tokens[p_begin].is_symbol("-"))
      for (const auto &operand : expressions(p_begin + 1, p_end))
        if (operand.level >= negation_level &&
            (operand.type.is_number() || !operand.type.known()))
          results.push_back({.shape = parse::shape_type::operation,
                             .core = "(- " + operand.core + ")",
                             .reading = "-" + nested_reading(operand, false),
                             .type = operand.type,
                             .level = negation_level,
                             .holes = operand.holes,
                             .hole_begin = operand.hole_begin,
                             .hole_end = operand.hole_end,
                             .hole_record = operand.hole_record,
                             .uses = operand.uses});

    // left OPERATOR right, left-associative.
    for (auto split = p_begin + 1; split + 1 < p_end; ++split) {
      if (depth[split] != depth[p_begin])
        continue;
      const auto info = operator_at(tokens[split]);
      if (!info)
        continue;
      for (const auto &left : expressions(p_begin, split)) {
        if (left.level < info->level)
          continue;
        for (const auto &right : expressions(split + 1, p_end)) {
          if (right.level <= info->level || left.holes + right.holes > 1)
            continue;
          const auto type =
              operation_type(info->core, left.type, right.type, words);
          if (!type)
            continue;
          results.push_back(
              {.shape = parse::shape_type::operation,
               .core = operation_core(info->core, left, right, words),
               .reading = nested_reading(left, false) + " " +
                          tokens[split].text + " " +
                          nested_reading(right, false),
               .type = *type,
               .level = info->level});
          results.back().take_holes(right);
          results.back().take_holes(left);
          results.back().use_all(left);
          results.back().use_all(right);
        }
      }
    }

    calls(p_begin, p_end, false, results);
    field_accesses(p_begin, p_end, results);
    new_records(p_begin, p_end, results);
    add_phrases(phrase_template::kind_type::expression, false, p_begin, p_end,
                results);

    unique(results);
    if (results.size() > max_readings)
      results.resize(max_readings);
    if (results.empty() && allow_holes)
      results.push_back(hole(p_begin, p_end));
    return chart[key] = std::move(results);
  }

  // Readings of the whole span as a statement phrase.
  auto statements(std::size_t p_begin, std::size_t p_end, bool p_block)
      -> std::vector<parse> {
    std::vector<parse> results;
    if (!p_block) {
      calls(p_begin, p_end, true, results);
      set_fields(p_begin, p_end, results);
    }
    add_phrases(phrase_template::kind_type::statement, p_block, p_begin, p_end,
                results);
    unique(results);
    return results;
  }
};

line_parser::line_parser(const std::vector<token> &p_tokens,
                         const std::map<string_type, value_kind> &p_scope,
                         const vocabulary &p_vocabulary, bool p_allow_holes)
    : impl(std::make_unique<state>(p_tokens, p_scope, p_vocabulary,
                                   p_allow_holes)) {}

line_parser::~line_parser() = default;

auto line_parser::expressions(std::size_t p_begin, std::size_t p_end)
    -> const std::vector<parse> & {
  return impl->expressions(p_begin, p_end);
}

auto line_parser::statements(std::size_t p_begin, std::size_t p_end,
                             bool p_block) -> std::vector<parse> {
  return impl->statements(p_begin, p_end, p_block);
}

auto shape_of(const std::vector<token> &p_tokens) -> line_shape {
  using enum line_shape::kind_type;
  const auto size = p_tokens.size();
  const auto block = size > 0 && p_tokens.back().is_symbol(":");
  const auto word = [&](std::size_t p_index, string_view_type p_word) {
    return p_index < size && p_tokens[p_index].is_word(p_word);
  };
  const auto with_block = [&](line_shape::kind_type p_kind, std::size_t p_begin,
                              string_view_type p_example) {
    line_shape shape{.kind = p_kind, .begin = p_begin, .end = size - 1,
                     .takes_block = true};
    if (!block)
      shape.problem = "End the line with ':' and indent what it covers "
                      "under it, as in: " + string_type(p_example);
    else if (p_begin >= size - 1 && p_kind != else_)
      shape.problem = "Say what this line depends on before the ':'.";
    return shape;
  };

  if (word(0, "if"))
    return with_block(if_, 1, "if x > 0:");
  if (word(0, "elif"))
    return with_block(else_if, 1, "otherwise if x > 0:");
  if ((word(0, "otherwise") || word(0, "else")) && word(1, "if"))
    return with_block(else_if, 2, "otherwise if x > 0:");
  if (word(0, "otherwise") || word(0, "else")) {
    auto shape = with_block(else_, 1, "otherwise:");
    if (block && size != 2)
      shape.problem = "Write 'otherwise if …:' or just 'otherwise:'.";
    return shape;
  }
  if (word(0, "while"))
    return with_block(while_, 1, "while x > 0:");
  if (word(0, "for")) {
    const auto first = word(1, "each") ? std::size_t(2) : std::size_t(1);
    auto shape = with_block(each, first + 2, "for each score in scores:");
    if (first >= size || p_tokens[first].kind != token::kind_type::word ||
        !word(first + 1, "in"))
      shape.problem = "Write a loop as 'for each NAME in LIST:'.";
    else
      shape.name = p_tokens[first].text;
    return shape;
  }
  if (word(0, "repeat")) {
    auto shape = with_block(repeat, 1, "repeat 3 times:");
    if (size < 3 || !word(size - 2, "times"))
      shape.problem = "Write 'repeat N times:'.";
    shape.end = size >= 2 ? size - 2 : 0;
    return shape;
  }
  if (word(0, "to") && block)
    return {.kind = define, .begin = 1, .end = size - 1, .takes_block = true};
  // `a book has:`, `a shape can:`, `a circle is a shape:`
  const auto names_of = [&](std::size_t p_begin, std::size_t p_end,
                            string_type &p_name, string_view_type p_example) {
    string_type problem;
    for (auto index = p_begin; index < p_end; ++index) {
      const auto &item = p_tokens[index];
      if (item.kind != token::kind_type::word ||
          !std::ranges::all_of(item.text, [](char_type p_character) {
            return std::isalpha(static_cast<unsigned char>(p_character)) != 0;
          }))
        problem = "A name is words of letters, as in: " + string_type(p_example);
      p_name += (p_name.empty() ? "" : "_") + lower(p_tokens[index].text);
    }
    return problem;
  };
  if (block && size >= 4 && (word(0, "a") || word(0, "an")) &&
      (word(size - 2, "has") || word(size - 2, "can"))) {
    line_shape shape{.kind = word(size - 2, "has") ? record : trait_decl,
                     .begin = 1, .end = size - 2, .takes_block = true};
    shape.problem = names_of(shape.begin, shape.end, shape.name,
                             shape.kind == record ? "an order item has:"
                                                  : "a shape can:");
    return shape;
  }
  if (block && size >= 6 && (word(0, "a") || word(0, "an"))) {
    for (std::size_t is = 2; is + 3 < size; ++is) {
      if (!word(is, "is") || !(word(is + 1, "a") || word(is + 1, "an")))
        continue;
      line_shape shape{.kind = impl_decl, .begin = 1, .end = is,
                       .takes_block = true};
      shape.trait_begin = is + 2;
      shape.trait_end = size - 1;
      shape.problem = names_of(shape.begin, shape.end, shape.name,
                               "a circle is a shape:");
      if (shape.problem.empty())
        shape.problem = names_of(shape.trait_begin, shape.trait_end,
                                 shape.trait_name, "a circle is a shape:");
      return shape;
    }
  }
  // `use … "header.h" [… "library"]`: any words around the texts.
  const auto is_header = [](const token &p_token) {
    return p_token.kind == token::kind_type::text && p_token.text.ends_with(".h");
  };
  if (word(0, "use") && std::ranges::any_of(p_tokens, is_header)) {
    line_shape shape{.kind = use_c};
    std::vector<string_type> texts;
    for (const auto &item : p_tokens)
      if (item.kind == token::kind_type::text)
        texts.push_back(item.text);
    if (texts.size() > 2 || !texts[0].ends_with(".h") || block)
      shape.problem = "Write 'use \"header.h\"', or 'use \"header.h\" from "
                      "\"library\"' to name the library to link.";
    shape.name = texts[0];
    shape.library = texts.size() == 2 ? texts[1] : string_type();
    return shape;
  }
  // `at the end, close file`, `at the end:`; `defer` is the same for readers
  // who know Zig or Go.
  if ((word(0, "at") && word(1, "the") && word(2, "end")) ||
      (word(0, "defer") && size > 1 &&
       !p_tokens[1].is_symbol("="))) {
    auto first = word(0, "defer") ? std::size_t(1) : std::size_t(3);
    if (first < size && p_tokens[first].is_symbol(","))
      ++first;
    const auto words = size - (block ? 1 : 0);
    if (block && first >= words)
      return {.kind = defer_block, .begin = first, .end = first,
              .takes_block = true};
    line_shape shape{.kind = defer_line, .begin = first, .end = size};
    if (block)
      shape.problem = "Write 'at the end:' and indent the statements under "
                      "it, or 'at the end, STATEMENT' on one line.";
    else if (first >= size)
      shape.problem = "Say what to do at the end, as in: at the end, "
                      "close file";
    return shape;
  }
  if (word(0, "give") && word(1, "back"))
    return {.kind = give_back, .begin = 2, .end = size};
  if ((word(0, "stop") || word(0, "skip")) && size == 1)
    return {.kind = word(0, "stop") ? stop : skip};
  if (size >= 3 && p_tokens[0].kind == token::kind_type::word &&
      p_tokens[1].is_symbol("=")) {
    line_shape shape{.kind = assign, .name = p_tokens[0].text, .begin = 2,
                     .end = size};
    if (block)
      shape.problem = "An assignment can't open a block: remove the ':'.";
    return shape;
  }
  return {.kind = phrase, .begin = 0, .end = block ? size - 1 : size,
          .takes_block = block};
}

auto readings_of(line_parser &p_parser, const line_shape &p_shape)
    -> std::vector<parse> {
  return p_shape.is_statement()
             ? p_parser.statements(p_shape.begin, p_shape.end,
                                   p_shape.takes_block)
             : p_parser.expressions(p_shape.begin, p_shape.end);
}

} // namespace ovis::phrase::internal
