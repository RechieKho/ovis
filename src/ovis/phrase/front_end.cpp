#include <algorithm>
#include <cctype>
#include <chrono>
#include <map>
#include <numeric>
#include <set>

#include <ovis/core/checker.hpp>
#include <ovis/core/elaborate.hpp>
#include <ovis/core/sexpr.hpp>
#include <ovis/phrase/front_end.hpp>
#include <ovis/phrase/lexer.hpp>
#include <ovis/phrase/templates.hpp>

namespace ovis::phrase {

namespace {

using clock_type = std::chrono::steady_clock;

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
  return left == right || left + "s" == right || right + "s" == left ||
         left + "es" == right || right + "es" == left;
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

// A value's type while parsing: a base type and how many list levels.
struct value_kind {
  // int, float, str, bool, or ? while it isn't known.
  string_type base = "?";
  std::size_t depth = 0;

  auto known() const -> bool { return base != "?"; }
  auto is_number() const -> bool {
    return depth == 0 && (base == "int" || base == "float");
  }
  auto element() const -> value_kind { return {base, depth - 1}; }
  auto core() const -> string_type {
    auto result = base == "?" ? string_type("int") : base;
    for (std::size_t level = 0; level < depth; ++level)
      result += "[]";
    return result;
  }
  auto english() const -> string_type {
    static const std::map<string_type, std::pair<string_type, string_type>>
        names = {{"int", {"a whole number", "whole numbers"}},
                 {"float", {"a decimal", "decimals"}},
                 {"str", {"text", "texts"}},
                 {"bool", {"true or false", "truth values"}},
                 {"?", {"not known yet", "values"}}};
    const auto &[one, many] = names.at(base);
    if (depth == 0)
      return one;
    string_type result;
    for (std::size_t level = 1; level < depth; ++level)
      result += "a list of lists of ";
    return (result.empty() ? "a list of " : result) + many;
  }
  friend auto operator==(const value_kind &, const value_kind &)
      -> bool = default;
};

// Operator precedence, loosest first.
enum precedence : int {
  or_level = 1,
  and_level,
  not_level,
  comparison_level,
  sum_level,
  product_level,
  negation_level,
  primary_level
};

// One reading of a span of tokens.
struct parse {
  enum class shape_type { atom, operation, phrase, hole };

  shape_type shape = shape_type::atom;
  string_type core;
  // The reading as source text: what a `Read as:` line would say.
  string_type reading;
  value_kind type;
  int level = primary_level;
  // Spans nothing could read, allowed only when looking for one (and then
  // at most one per reading).
  std::size_t holes = 0;
  std::size_t hole_begin = 0;
  std::size_t hole_end = 0;
};

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

// What each slot of a template matched; a variadic slot holds several.
struct slot_fills {
  std::vector<std::vector<parse>> values;
  // For a slot of type `type`.
  std::vector<std::optional<value_kind>> types;
};

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
  for (const auto &element : p_pattern) {
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
      string_type values;
      for (const auto &value : p_fills.values[element.slot])
        values += (values.empty() ? "" : ", ") + nested_reading(value, true);
      add(values);
      break;
    }
    }
  }
  return result;
}

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
  for (const auto &values : p_fills.values)
    for (const auto &value : values)
      if (value.holes > 0) {
        result.holes += value.holes;
        result.hole_begin = value.hole_begin;
        result.hole_end = value.hole_end;
      }
  return result;
}

// Every reading of every span of one line, computed once each (a chart
// parser). In the second pass (`allow_holes`) a span nothing reads may be a
// hole, to find which part of the line is unclear.
class line_parser {
  const std::vector<token> &tokens;
  const std::map<string_type, value_kind> &scope;
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
    return p_position < p_end &&
           tokens[p_position].kind == token::kind_type::word &&
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
        return parse{.core = p_token.text,
                     .reading = p_token.text,
                     .type = found->second};
      return std::nullopt;
    case token::kind_type::symbol:
      return std::nullopt;
    }
    return std::nullopt;
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
          for (auto &choice : combinations(items(p_position, end))) {
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
    for (const auto &item : builtin_templates()) {
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

public:
  line_parser(const std::vector<token> &p_tokens,
              const std::map<string_type, value_kind> &p_scope,
              bool p_allow_holes)
      : tokens(p_tokens), scope(p_scope), allow_holes(p_allow_holes),
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
                             .hole_end = operand.hole_end});
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
                             .hole_end = operand.hole_end});

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
        }
      }
    }

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
    add_phrases(phrase_template::kind_type::statement, p_block, p_begin, p_end,
                results);
    unique(results);
    return results;
  }
};

// What kind of line it is, decided by its first words, and the span that
// must be read as an expression or a statement phrase.
struct line_shape {
  enum class kind_type {
    assign,
    phrase,
    if_,
    else_if,
    else_,
    while_,
    each,
    repeat,
    stop,
    skip
  };

  kind_type kind = kind_type::phrase;
  // The variable assigned or looped over.
  string_type name;
  std::size_t begin = 0;
  std::size_t end = 0;
  bool takes_block = false;
  // Why the line can't be read, if it can't.
  string_type problem;

  auto is_statement() const -> bool { return kind == kind_type::phrase; }
};

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

// How alike two words are, for ranking which template an unknown phrase
// might mean: whole words, then a shared start, then shared trigrams.
auto word_similarity(string_view_type p_left, string_view_type p_right)
    -> double {
  if (same_word(p_left, p_right))
    return 3;
  const auto left = lower(p_left);
  const auto right = lower(p_right);
  std::size_t prefix = 0;
  while (prefix < left.size() && prefix < right.size() &&
         left[prefix] == right[prefix])
    ++prefix;
  if (prefix >= 4)
    return 2;
  std::set<string_type> left_trigrams;
  std::set<string_type> right_trigrams;
  for (std::size_t index = 0; index + 3 <= left.size(); ++index)
    left_trigrams.insert(left.substr(index, 3));
  for (std::size_t index = 0; index + 3 <= right.size(); ++index)
    right_trigrams.insert(right.substr(index, 3));
  std::size_t shared = 0;
  for (const auto &trigram : left_trigrams)
    shared += right_trigrams.contains(trigram);
  const auto all = left_trigrams.size() + right_trigrams.size() - shared;
  return all > 0 && double(shared) / double(all) >= 0.4 ? 1 : 0;
}

auto is_function_word(string_view_type p_word) -> bool {
  static const std::set<string_view_type> words = {
      "of", "to", "in", "from", "by", "for", "with", "and", "a",
      "an", "the", "is", "at", "on", "as", "into", "it"};
  return words.contains(lower(p_word));
}

struct candidate {
  string_type reading;
  double score = 0;
};

// Readings an unknown span might have: templates whose slots take the
// span's arguments (names, numbers, texts, bracketed groups), in some order
// the types allow, ranked by how alike their words are to the span's
// leftover words.
auto unknown_candidates(const std::vector<token> &p_tokens, std::size_t p_begin,
                        std::size_t p_end, bool p_statement, bool p_block,
                        const std::map<string_type, value_kind> &p_scope)
    -> std::vector<candidate> {
  line_parser parser(p_tokens, p_scope, false);
  std::vector<parse> arguments;
  std::vector<string_type> verb;
  for (auto index = p_begin; index < p_end; ++index) {
    const auto &item = p_tokens[index];
    if (is_article(item) || item.is_symbol(","))
      continue;
    if (item.is_symbol("(") || item.is_symbol("[")) {
      auto close = index;
      for (int level = 0; close < p_end; ++close) {
        level += p_tokens[close].is_symbol("(") || p_tokens[close].is_symbol("[");
        level -= p_tokens[close].is_symbol(")") || p_tokens[close].is_symbol("]");
        if (level == 0)
          break;
      }
      const auto &inside = parser.expressions(index, close + 1);
      if (!inside.empty())
        arguments.push_back(inside.front());
      index = close;
      continue;
    }
    const auto &single = parser.expressions(index, index + 1);
    if (!single.empty())
      arguments.push_back(single.front());
    else if (item.kind == token::kind_type::word)
      verb.push_back(item.text);
  }

  std::vector<candidate> result;
  std::set<string_type> seen;
  for (const auto &item : builtin_templates()) {
    if ((item.kind == phrase_template::kind_type::statement) != p_statement ||
        (p_statement && item.takes_block != p_block))
      continue;
    const auto &pattern = item.patterns[0];
    std::vector<std::size_t> slots;
    for (const auto &element : pattern)
      if (element.kind == pattern_element::kind_type::slot)
        slots.push_back(element.slot);
    if (std::ranges::any_of(slots, [&](std::size_t p_slot) {
          return item.slots[p_slot].type.name == "type";
        }))
      continue;
    const auto variadic = !slots.empty() && item.slots[slots.back()].is_variadic;
    if (arguments.size() < slots.size() ||
        (!variadic && arguments.size() != slots.size()) || arguments.size() > 4)
      continue;

    double score = 0;
    for (const auto &word : verb) {
      double best = 0;
      for (const auto &alternative : item.patterns)
        for (const auto &element : alternative) {
          std::vector<string_type> words;
          if (element.kind == pattern_element::kind_type::word)
            words.push_back(element.word);
          for (const auto &branch : element.branches)
            for (const auto &part : branch)
              words.push_back(part.word);
          for (const auto &candidate_word : words)
            best = std::max(best, word_similarity(word, candidate_word) *
                                      (is_function_word(word) ? 0.2 : 1.0));
        }
      score += best;
    }

    std::vector<std::size_t> order(arguments.size());
    std::iota(order.begin(), order.end(), 0);
    do {
      slot_fills fills;
      fills.values.resize(item.slots.size());
      fills.types.resize(item.slots.size());
      for (std::size_t index = 0; index < order.size(); ++index) {
        const auto slot = slots[std::min(index, slots.size() - 1)];
        fills.values[slot].push_back(arguments[order[index]]);
      }
      if (const auto built = build_phrase(item, pattern, fills))
        if (seen.insert(built->reading).second)
          // The order as written comes first among equals.
          result.push_back({built->reading,
                            score - (std::ranges::is_sorted(order) ? 0 : 0.1)});
    } while (std::ranges::next_permutation(order).found);
  }
  std::ranges::stable_sort(result, std::ranges::greater{}, &candidate::score);
  if (result.size() > 21)
    result.resize(21);
  return result;
}

constexpr string_view_type question_instructions =
    "You help a compiler read a program written in plain English. Some words "
    "in a line are not in the compiler's vocabulary. You are shown the line "
    "and numbered phrases the compiler knows. Answer with the number of the "
    "phrase that means the same as the unknown words, as an English speaker "
    "would understand them. If none means the same, answer with the number "
    "of \"none of these\".";

// The accepted reading, or nothing when the model isn't sure or picks none.
struct model_answer {
  std::optional<std::size_t> choice;
  std::vector<double> probabilities;
};

class question_asker {
  english::language_model *model;
  const front_end_options &options;
  front_end_timing &timing;

public:
  // Why the model can no longer be asked, once it can't.
  string_type unavailable;

  question_asker(english::language_model *p_model,
                 const front_end_options &p_options, front_end_timing &p_timing)
      : model(p_model), options(p_options), timing(p_timing) {}

  // Asks about at most 7 readings plus "none of these".
  auto ask(const string_type &p_line, const string_type &p_known,
           const string_type &p_span, const std::vector<candidate> &p_options)
      -> model_answer {
    if (model == nullptr)
      unavailable = "there is no model (--offline)";
    // The first question also loads the model, which says nothing about
    // how long questions take, so it doesn't count.
    else if (timing.model_ms - timing.first_question_ms >
             options.model_budget_seconds * 1000)
      unavailable = "the model used up its time (--model-budget)";
    if (!unavailable.empty())
      return {};

    auto text = "Line: " + p_line + "\n";
    if (!p_known.empty())
      text += "Known: " + p_known + "\n";
    text += "Which phrase means the same as \"" + p_span + "\"?\n";
    for (std::size_t index = 0; index < p_options.size(); ++index)
      text += std::to_string(index + 1) + ". " + p_options[index].reading + "\n";
    text += std::to_string(p_options.size() + 1) + ". none of these\nAnswer:";

    const auto start = clock_type::now();
    model_answer answer;
    try {
      answer.probabilities = model->choose(
          {{"system", string_type(question_instructions)}, {"user", text}},
          p_options.size() + 1);
    } catch (const error &p_error) {
      unavailable = p_error.what();
      return {};
    }
    const auto elapsed = milliseconds_since(start);
    if (timing.asked == 0)
      timing.first_question_ms = elapsed;
    timing.model_ms += elapsed;
    ++timing.asked;

    std::vector<std::size_t> ranked(answer.probabilities.size());
    std::iota(ranked.begin(), ranked.end(), 0);
    std::ranges::stable_sort(ranked, [&](std::size_t p_left, std::size_t p_right) {
      return answer.probabilities[p_left] > answer.probabilities[p_right];
    });
    const auto best = ranked[0];
    const auto margin =
        answer.probabilities[best] - answer.probabilities[ranked[1]];
    if (best < p_options.size() && answer.probabilities[best] >= 0.6 &&
        margin >= 0.3)
      answer.choice = best;
    if (options.log) {
      string_type summary = "asked about \"" + p_span + "\":";
      for (std::size_t index = 0; index < ranked.size() && index < 3; ++index) {
        const auto option = ranked[index];
        summary += " " +
                   (option < p_options.size() ? p_options[option].reading
                                              : string_type("none")) +
                   " " +
                   std::to_string(int(answer.probabilities[option] * 100 + 0.5)) +
                   "%";
      }
      options.log(summary + " (" + std::to_string(int(elapsed)) + " ms)");
    }
    return answer;
  }

  // More readings than fit in one question: each group of 7 picks its best,
  // then the winners are asked about together.
  auto choose(const string_type &p_line, const string_type &p_known,
              const string_type &p_span, std::vector<candidate> p_options)
      -> std::optional<string_type> {
    while (p_options.size() > 7) {
      std::vector<candidate> winners;
      for (std::size_t begin = 0; begin < p_options.size(); begin += 7) {
        std::vector<candidate> group(
            p_options.begin() + static_cast<std::ptrdiff_t>(begin),
            p_options.begin() +
                static_cast<std::ptrdiff_t>(std::min(begin + 7, p_options.size())));
        const auto answer = ask(p_line, p_known, p_span, group);
        if (!unavailable.empty())
          return std::nullopt;
        // A group's favourite goes on even when it isn't sure yet.
        const auto best = static_cast<std::size_t>(
            std::ranges::max_element(answer.probabilities) -
            answer.probabilities.begin());
        if (best < group.size())
          winners.push_back(group[best]);
      }
      p_options = std::move(winners);
    }
    if (p_options.empty())
      return std::nullopt;
    const auto answer = ask(p_line, p_known, p_span, p_options);
    if (!answer.choice)
      return std::nullopt;
    return p_options[*answer.choice].reading;
  }
};

// One code line while it is being read: its text from its first token, so
// `Read as:` decisions can be put in and the line read again.
struct working_line {
  const source_line *source = nullptr;
  string_type text;
  std::vector<token> tokens;
  // `"SPAN" is READING`, in the order they apply.
  std::vector<string_type> decisions;
  bool decisions_changed = false;
};

auto span_text(const working_line &p_line, std::size_t p_begin,
               std::size_t p_end) -> string_type {
  const auto base = p_line.tokens.front().column;
  const auto from = p_line.tokens[p_begin].column - base;
  const auto to = p_line.tokens[p_end - 1].end_column - base;
  return p_line.text.substr(from, to - from);
}

// Puts a decision into the line: the span's text is replaced by the
// reading. False if the span isn't in the line (the line was edited).
auto apply_decision(working_line &p_line, string_view_type p_decision) -> bool {
  if (!p_decision.starts_with('"'))
    return false;
  const auto separator = p_decision.find("\" is ", 1);
  if (separator == string_view_type::npos)
    return false;
  const auto span = p_decision.substr(1, separator - 1);
  const auto reading = p_decision.substr(separator + 5);
  const auto at = p_line.text.find(span);
  if (span.empty() || at == string_type::npos)
    return false;
  auto text = p_line.text;
  text.replace(at, span.size(), reading);
  try {
    p_line.tokens = tokenize(text, p_line.source->line);
  } catch (const lex_error &) {
    return false;
  }
  p_line.text = std::move(text);
  return true;
}

// Names in the line that are in scope, with their types, for the model.
auto known_names(const std::vector<token> &p_tokens,
                 const std::map<string_type, value_kind> &p_scope)
    -> string_type {
  string_type result;
  std::set<string_type> seen;
  for (const auto &item : p_tokens) {
    if (item.kind != token::kind_type::word)
      continue;
    const auto found = p_scope.find(item.text);
    if (found == p_scope.end() || !seen.insert(item.text).second)
      continue;
    result += (result.empty() ? "" : " ") + item.text + " is " +
              found->second.english() + ".";
  }
  return result;
}

auto default_value(const value_kind &p_type) -> string_type {
  if (p_type.depth > 0)
    return "(array " + p_type.element().core() + " 0)";
  if (p_type.base == "float")
    return "0.0";
  if (p_type.base == "str")
    return "\"\"";
  if (p_type.base == "bool")
    return "false";
  return "0";
}

// Reads the program line by line, keeping the scope, and lowers it to Ovis
// Core: one unit per top-level line.
class program_reader {
  std::vector<source_line> lines;
  std::vector<working_line> working;
  english::language_model *model;
  const front_end_options &options;
  front_end_result &result;
  question_asker asker;
  std::map<string_type, value_kind> scope;
  // `let`s for variables first set inside a block, put before the current
  // top-level line so the variable lives on after the block.
  std::vector<string_type> hoisted;
  std::size_t counters = 0;

  auto problem(const source_line &p_line, const string_type &p_message)
      -> void {
    result.problems.push_back("line " + std::to_string(p_line.line) + ": " +
                              p_message);
  }

  // The one reading of the line's span, settling unclear spans with the
  // model. Nothing if it couldn't be settled (reported).
  auto settle(working_line &p_line, line_shape &p_shape)
      -> std::optional<parse> {
    const auto &source = *p_line.source;
    for (int round = 0; round < 4; ++round) {
      p_shape = shape_of(p_line.tokens);
      if (!p_shape.problem.empty()) {
        problem(source, p_shape.problem);
        return std::nullopt;
      }
      if (p_shape.kind == line_shape::kind_type::else_ ||
          p_shape.kind == line_shape::kind_type::stop ||
          p_shape.kind == line_shape::kind_type::skip)
        return parse{};

      line_parser parser(p_line.tokens, scope, false);
      auto readings = readings_of(parser, p_shape);
      if (readings.size() == 1)
        return readings.front();

      ++result.timing.questions;
      std::size_t begin = p_shape.begin;
      std::size_t end = p_shape.end;
      std::vector<candidate> offered;
      bool statement = p_shape.is_statement();
      if (readings.size() > 1) {
        for (const auto &reading : readings)
          offered.push_back({reading.reading});
      } else {
        // Find the unclear part: the shortest span nothing reads, in a
        // reading of the rest.
        line_parser finder(p_line.tokens, scope, true);
        for (const auto &reading : readings_of(finder, p_shape))
          if (reading.holes == 1 &&
              reading.hole_end - reading.hole_begin < end - begin) {
            begin = reading.hole_begin;
            end = reading.hole_end;
            statement = false;
          }
        if (end - begin == 1 &&
            p_line.tokens[begin].kind == token::kind_type::word) {
          std::vector<string_type> names;
          for (const auto &[name, type] : scope)
            names.push_back(name);
          const auto closest = core::closest_name(p_line.tokens[begin].text, names);
          problem(source, "Nothing called '" + p_line.tokens[begin].text +
                              "' is known here" +
                              (closest ? "; did you mean '" + *closest + "'?"
                                       : "."));
          return std::nullopt;
        }
        offered = unknown_candidates(p_line.tokens, begin, end, statement,
                                     p_shape.takes_block, scope);
      }

      const auto span = span_text(p_line, begin, end);
      if (offered.empty()) {
        problem(source, "No phrase Ovis knows reads \"" + span + "\".");
        return std::nullopt;
      }
      const auto chosen = asker.choose(p_line.text, known_names(p_line.tokens, scope),
                                       span, offered);
      if (!chosen) {
        string_type listed;
        for (std::size_t index = 0; index < offered.size() && index < 4; ++index)
          listed += "\n    " + offered[index].reading;
        problem(source,
                "\"" + span + "\" is unclear" +
                    (asker.unavailable.empty()
                         ? ""
                         : " and couldn't be asked about: " + asker.unavailable) +
                    ". Did you mean one of these? Write it in the source:" +
                    listed);
        return std::nullopt;
      }
      const auto decision = "\"" + span + "\" is " + *chosen;
      if (!apply_decision(p_line, decision)) {
        problem(source, "Internal compiler error: the reading \"" + *chosen +
                            "\" doesn't fit the line.");
        return std::nullopt;
      }
      p_line.decisions.push_back(decision);
      p_line.decisions_changed = true;
    }
    problem(source, "This line stays unclear after several readings; "
                    "rewrite it more simply.");
    return std::nullopt;
  }

  // The Ovis Core of the lines from `p_index` at `p_depth`, up to the end of
  // that block. Moves `p_index` past them.
  auto read_block(std::size_t &p_index, std::size_t p_depth)
      -> std::vector<string_type> {
    std::vector<string_type> forms;
    while (p_index < lines.size() && lines[p_index].depth == p_depth) {
      auto form = read_statement(p_index, p_depth);
      if (!form.empty())
        forms.push_back(std::move(form));
    }
    return forms;
  }

  auto joined(const std::vector<string_type> &p_forms) -> string_type {
    string_type result;
    for (const auto &form : p_forms)
      result += " " + form;
    return result;
  }

  // The body under a line that opens a block (always there: the lexer
  // makes sure of it).
  auto read_body(std::size_t &p_index, std::size_t p_depth)
      -> std::vector<string_type> {
    return read_block(p_index, p_depth + 1);
  }

  auto read_statement(std::size_t &p_index, std::size_t p_depth)
      -> string_type {
    using enum line_shape::kind_type;
    auto &line = working[p_index];
    const auto &source = lines[p_index];
    ++p_index;
    line_shape shape;
    const auto value = settle(line, shape);
    const auto skip_body = [&] {
      while (p_index < lines.size() && lines[p_index].depth > p_depth)
        ++p_index;
    };
    if (!value) {
      // Known from here on, so one unclear line isn't reported again on
      // every line using what it sets.
      if (shape.kind == assign || shape.kind == each)
        scope.try_emplace(shape.name);
      skip_body();
      return {};
    }

    switch (shape.kind) {
    case assign: {
      const auto existing = scope.find(shape.name);
      if (existing == scope.end()) {
        scope[shape.name] = value->type;
        if (p_depth > 0) {
          hoisted.push_back("(let " + shape.name + " " +
                            default_value(value->type) + ")");
          return "(set " + shape.name + " " + value->core + ")";
        }
        return "(let " + shape.name + " " + value->core + ")";
      }
      if (!existing->second.known())
        existing->second = value->type;
      return "(set " + shape.name + " " + value->core + ")";
    }
    case phrase: {
      auto core = value->core;
      if (shape.takes_block) {
        const auto body = joined(read_body(p_index, p_depth));
        const auto at = core.find("BLOCK");
        core.replace(at, 5, body.empty() ? "(pass)" : body.substr(1));
      }
      return core;
    }
    case if_: {
      auto result_form = "(if " + value->core + " (then" +
                         joined(read_body(p_index, p_depth)) + ")";
      // otherwise if … / otherwise: nest as else branches.
      std::string closing = ")";
      while (p_index < lines.size() && lines[p_index].depth == p_depth) {
        const auto next = shape_of(working[p_index].tokens);
        if (next.kind == else_if) {
          auto &next_line = working[p_index];
          ++p_index;
          line_shape next_shape;
          const auto condition = settle(next_line, next_shape);
          const auto body = joined(read_body(p_index, p_depth));
          if (!condition)
            continue;
          result_form += " (else (if " + condition->core + " (then" + body + ")";
          closing += "))";
        } else if (next.kind == else_) {
          ++p_index;
          result_form +=
              " (else" + joined(read_body(p_index, p_depth)) + ")";
          break;
        } else {
          break;
        }
      }
      return result_form + closing;
    }
    case else_if:
    case else_:
      problem(source, "'otherwise' must follow an 'if' at the same depth.");
      skip_body();
      return {};
    case while_:
      return "(while " + value->core + joined(read_body(p_index, p_depth)) +
             ")";
    case each: {
      if (value->type.known() && value->type.depth == 0) {
        problem(source, "'" + span_text(line, shape.begin, shape.end) +
                            "' is " + value->type.english() +
                            ", not a list to go through.");
        skip_body();
        return {};
      }
      scope[shape.name] =
          value->type.known() ? value->type.element() : value_kind{};
      return "(each " + shape.name + " " + value->core +
             joined(read_body(p_index, p_depth)) + ")";
    }
    case repeat: {
      const auto counter = "repeat_" + std::to_string(++counters);
      return "(for " + counter + " 1 " + value->core +
             joined(read_body(p_index, p_depth)) + ")";
    }
    case stop:
      return "(break)";
    case skip:
      return "(continue)";
    }
    return {};
  }

public:
  program_reader(string_view_type p_source, english::language_model *p_model,
                 const front_end_options &p_options, front_end_result &p_result)
      : model(p_model), options(p_options), result(p_result),
        asker(p_model, p_options, p_result.timing) {
    const auto start = clock_type::now();
    lines = split_lines(p_source);
    result.timing.lex_ms = milliseconds_since(start);
    result.timing.lines = lines.size();

    // Each line's text from its first token, for putting decisions in.
    std::vector<string_view_type> physical;
    for (std::size_t begin = 0; begin <= p_source.size();) {
      auto end = p_source.find('\n', begin);
      if (end == string_view_type::npos)
        end = p_source.size();
      physical.push_back(p_source.substr(begin, end - begin));
      begin = end + 1;
    }
    for (const auto &line : lines) {
      working_line item{.source = &line, .tokens = line.tokens};
      const auto &text = physical[line.line - 1];
      item.text = string_type(text.substr(line.tokens.front().column - 1));
      if (item.text.ends_with('\r'))
        item.text.pop_back();
      const auto single_line = line.tokens.back().line == line.line;
      for (const auto &note : line.annotations) {
        if (note.kind != annotation::kind_type::read_as)
          continue;
        // A decision whose words are no longer in the line is dropped: the
        // line was edited, so it is read afresh.
        if (single_line && apply_decision(item, note.text))
          item.decisions.push_back(note.text);
        else
          item.decisions_changed = true;
      }
      working.push_back(std::move(item));
    }
  }

  // Top-level lines as Ovis Core, with the source line each starts at.
  auto read() -> std::vector<std::pair<std::size_t, string_type>> {
    std::vector<std::pair<std::size_t, string_type>> units;
    std::size_t index = 0;
    while (index < lines.size()) {
      const auto line = lines[index].line;
      auto form = read_statement(index, 0);
      string_type text;
      for (const auto &let : hoisted)
        text += let + "\n";
      hoisted.clear();
      text += form;
      if (!text.empty())
        units.emplace_back(line, std::move(text));
    }
    return units;
  }

  // The source with each changed line's `Read as:` lines rewritten.
  auto written_source(string_view_type p_source) const -> string_type {
    std::map<std::size_t, const working_line *> changed;
    std::set<std::size_t> dropped;
    for (const auto &line : working) {
      if (!line.decisions_changed)
        continue;
      changed[std::ranges::max(line.source->tokens, {}, &token::line).line] = &line;
      for (const auto &note : line.source->annotations)
        if (note.kind == annotation::kind_type::read_as)
          dropped.insert(note.line);
    }
    if (changed.empty())
      return string_type(p_source);

    string_type output;
    std::size_t number = 0;
    for (std::size_t begin = 0; begin <= p_source.size(); ++number) {
      auto end = p_source.find('\n', begin);
      const auto last = end == string_view_type::npos;
      if (last)
        end = p_source.size();
      const auto text = p_source.substr(begin, end - begin);
      begin = end + 1;
      if (!dropped.contains(number + 1))
        output += string_type(text) + (last ? "" : "\n");
      if (const auto found = changed.find(number + 1); found != changed.end()) {
        const auto indent = found->second->source->tokens.front().column - 1;
        if (last && !output.ends_with('\n') && !found->second->decisions.empty())
          output += "\n";
        for (const auto &decision : found->second->decisions)
          output += string_type(indent + 4, ' ') + "Read as: " + decision + "\n";
      }
      if (last)
        break;
    }
    return output;
  }
};

} // namespace

auto compile_phrases(string_view_type p_source, english::language_model *p_model,
                     const front_end_options &p_options) -> front_end_result {
  front_end_result result;
  const auto start = clock_type::now();
  std::vector<std::pair<std::size_t, string_type>> units;
  std::optional<program_reader> reader;
  try {
    reader.emplace(p_source, p_model, p_options, result);
    units = reader->read();
  } catch (const lex_error &p_error) {
    result.problems.emplace_back(p_error.what());
    result.source = string_type(p_source);
    return result;
  }
  result.source = reader->written_source(p_source);
  result.timing.parse_ms =
      milliseconds_since(start) - result.timing.lex_ms - result.timing.model_ms;
  if (!result.succeeded())
    return result;

  const auto check_start = clock_type::now();
  std::vector<std::size_t> unit_lines = {0};
  result.program.units.push_back(core::parse_unit(builtin_prelude()));
  for (const auto &[line, text] : units) {
    try {
      result.program.units.push_back(core::parse_unit(text));
      unit_lines.push_back(line);
    } catch (const error &p_error) {
      result.problems.push_back("line " + std::to_string(line) +
                                ": Internal compiler error: " + p_error.what() +
                                "\n    Ovis Core: " + text);
    }
  }
  if (result.succeeded()) {
    auto notes = core::elaborate(result.program);
    for (const auto &problem : core::check(result.program, &notes)) {
      const auto line = unit_lines[problem.unit_index];
      result.problems.push_back(
          (line == 0 ? string_type("prelude") : "line " + std::to_string(line)) +
          ": " + problem.message);
    }
  }
  result.timing.check_ms = milliseconds_since(check_start);
  return result;
}

} // namespace ovis::phrase
