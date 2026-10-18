#include <algorithm>
#include <cctype>
#include <set>
#include <tuple>

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

auto value_kind::english() const -> string_type {
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

auto operation_type(string_view_type p_operator, const value_kind &p_left,
                    const value_kind &p_right) -> std::optional<value_kind> {
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
  if (p_operator == "+" &&
      ((p_left.base == "str" && p_left.depth == 0) ||
       (p_right.base == "str" && p_right.depth == 0)) &&
      p_left.depth == 0 && p_right.depth == 0)
    return value_kind{"str"};
  if (!number_like(p_left) || !number_like(p_right))
    return std::nullopt;
  if (p_operator == "/" || p_left.base == "float" || p_right.base == "float")
    return value_kind{"float"};
  if (p_left.base == "int" && p_right.base == "int")
    return value_kind{"int"};
  return value_kind{};
}

} // namespace

// Type names written in words, for slots of type `type`.
auto type_words(const std::vector<token> &p_tokens, std::size_t p_position,
                std::size_t p_end)
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
  return result;
}

namespace {

auto accepts(const type_name &p_wanted, const value_kind &p_value,
             std::map<string_type, value_kind> &p_variables,
             const phrase_template &p_template, const slot_fills &p_fills)
    -> bool {
  if (!p_value.known())
    return true;
  if (p_wanted.kind == type_name::kind_type::variable) {
    if (p_value.depth < p_wanted.list_depth)
      return false;
    const value_kind element{p_value.base, p_value.depth - p_wanted.list_depth};
    const auto [bound, added] = p_variables.try_emplace(p_wanted.name, element);
    return added || !bound->second.known() || bound->second == element;
  }
  if (p_wanted.kind == type_name::kind_type::type_slot) {
    const auto index = static_cast<std::size_t>(
        std::ranges::find(p_template.slots, p_wanted.name, &slot::name) -
        p_template.slots.begin());
    const auto &chosen = p_fills.types[index];
    return !chosen ||
           p_value == value_kind{chosen->base, p_wanted.list_depth};
  }
  if (p_wanted.name == "any")
    return p_wanted.list_depth == 0 || p_value.depth == p_wanted.list_depth;
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
    return {bound->second.base, bound->second.depth + wanted.list_depth};
  }
  case type_name::kind_type::type_slot: {
    const auto index = static_cast<std::size_t>(
        std::ranges::find(p_template.slots, wanted.name, &slot::name) -
        p_template.slots.begin());
    const auto &chosen = p_fills.types[index];
    return chosen ? value_kind{chosen->base, wanted.list_depth} : value_kind{};
  }
  }
  return {};
}

// The template's Ovis Core with the slots filled in.
auto expand(const phrase_template &p_template, const slot_fills &p_fills)
    -> string_type {
  string_type result;
  const auto &core = p_template.core;
  for (std::size_t index = 0; index < core.size();) {
    if (core[index] != '$') {
      result += core[index++];
      continue;
    }
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
                  const slot_fills &p_fills) -> std::optional<parse> {
  std::map<string_type, value_kind> variables;
  for (std::size_t index = 0; index < p_template.slots.size(); ++index)
    for (const auto &value : p_fills.values[index])
      if (!accepts(p_template.slots[index].type, value.type, variables,
                   p_template, p_fills))
        return std::nullopt;

  parse result{.shape = parse::shape_type::phrase};
  result.open_edge =
      p_pattern.front().kind == pattern_element::kind_type::slot ||
      p_pattern.back().kind == pattern_element::kind_type::slot;
  result.core = expand(p_template, p_fills);
  result.reading = reading_of(p_template, p_pattern, p_fills);
  if (p_template.result)
    result.type = result_type(p_template, variables, p_fills);
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
      if (value.holes > 0) {
        result.holes += value.holes;
        result.hole_begin = value.hole_begin;
        result.hole_end = value.hole_end;
      }
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
        if (argument.holes > 0) {
          call.holes += argument.holes;
          call.hole_begin = argument.hole_begin;
          call.hole_end = argument.hole_end;
        }
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
    if (commas.size() > 4)
      return {items(p_begin, p_end)};
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
             type_words(tokens, p_position, p_end)) {
          p_fills.types[element.slot] = type;
          next(p_position + length);
        }
        p_fills.types[element.slot].reset();
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
          if (auto built = build_phrase(item, item.patterns[0], found))
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
      for (const auto &elements : combinations(items(p_begin + 1, p_end - 1))) {
        if (elements.empty())
          continue;
        parse list{.core = "(list", .reading = "["};
        auto element = elements[0].type;
        bool fits = true;
        for (std::size_t index = 0; index < elements.size(); ++index) {
          const auto &item = elements[index];
          list.core += " " + item.core;
          list.reading += (index > 0 ? ", " : "") + item.reading;
          list.holes += item.holes;
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
        list.type = element.known() ? value_kind{element.base, element.depth + 1}
                                    : value_kind{};
        if (fits)
          results.push_back(std::move(list));
      }

    // not …, - …
    if (tokens[p_begin].is_word("not"))
      for (const auto &operand : expressions(p_begin + 1, p_end))
        if (operand.level >= not_level &&
            operation_type("and", operand.type, operand.type))
          results.push_back({.shape = parse::shape_type::operation,
                             .core = "(not " + operand.core + ")",
                             .reading = "not " + nested_reading(operand, false),
                             .type = {"bool"},
                             .level = not_level,
                             .holes = operand.holes,
                             .hole_begin = operand.hole_begin,
                             .hole_end = operand.hole_end,
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
          const auto type = operation_type(info->core, left.type, right.type);
          if (!type)
            continue;
          results.push_back(
              {.shape = parse::shape_type::operation,
               .core = "(" + string_type(info->core) + " " + left.core + " " +
                       right.core + ")",
               .reading = nested_reading(left, false) + " " +
                          tokens[split].text + " " +
                          nested_reading(right, false),
               .type = *type,
               .level = info->level,
               .holes = left.holes + right.holes,
               .hole_begin = left.holes > 0 ? left.hole_begin : right.hole_begin,
               .hole_end = left.holes > 0 ? left.hole_end : right.hole_end});
          results.back().use_all(left);
          results.back().use_all(right);
        }
      }
    }

    calls(p_begin, p_end, false, results);
    add_phrases(phrase_template::kind_type::expression, false, p_begin, p_end,
                results);

    unique(results);
    if (results.size() > max_readings)
      results.resize(max_readings);
    if (results.empty() && allow_holes)
      results.push_back({.shape = parse::shape_type::hole,
                         .core = "HOLE",
                         .reading = "…",
                         .holes = 1,
                         .hole_begin = p_begin,
                         .hole_end = p_end});
    return chart[key] = std::move(results);
  }

  // Readings of the whole span as a statement phrase.
  auto statements(std::size_t p_begin, std::size_t p_end, bool p_block)
      -> std::vector<parse> {
    std::vector<parse> results;
    if (!p_block)
      calls(p_begin, p_end, true, results);
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
