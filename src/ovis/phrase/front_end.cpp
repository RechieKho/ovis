#include <algorithm>
#include <cctype>
#include <map>
#include <set>

#include <ovis/core/checker.hpp>
#include <ovis/core/elaborate.hpp>
#include <ovis/core/sexpr.hpp>
#include <ovis/phrase/front_end.hpp>
#include <ovis/phrase/lexer.hpp>
#include <ovis/phrase/templates.hpp>

#include "parser.hpp"
#include "questions.hpp"

namespace ovis::phrase {

namespace {

using namespace internal;

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
  // The span as whole words: "it" isn't the end of "split".
  const auto is_word_character = [](char_type p_character) {
    return std::isalnum(static_cast<unsigned char>(p_character)) ||
           p_character == '_';
  };
  const auto &line = p_line.text;
  auto at = line.find(span);
  while (!span.empty() && at != string_type::npos &&
         ((at > 0 && is_word_character(span.front()) &&
           is_word_character(line[at - 1])) ||
          (at + span.size() < line.size() && is_word_character(span.back()) &&
           is_word_character(line[at + span.size()]))))
    at = line.find(span, at + 1);
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

// A phrase the program defines: `to pad (text) to (width):`.
struct user_function {
  // As called, `pad_to(...)`, and in Ovis Core (different only where the
  // name is taken by Ovis Core).
  string_type name;
  string_type core;
  std::vector<string_type> parameters;
  // Stated types, as in `(width: whole number)`.
  std::vector<std::optional<value_kind>> types;
  std::vector<phrase_template> templates;
};

// A type written in words after a parameter's ':': `whole number`,
// `list of text`.
auto stated_type(const std::vector<token> &p_tokens, std::size_t p_begin,
                 std::size_t p_end) -> std::optional<value_kind> {
  if (p_end - p_begin > 2 && p_tokens[p_begin].is_word("list") &&
      p_tokens[p_begin + 1].is_word("of")) {
    auto element = stated_type(p_tokens, p_begin + 2, p_end);
    if (!element || element->depth > 0)
      return std::nullopt;
    return value_kind{element->base, 1};
  }
  for (const auto &[length, type] : type_words(p_tokens, p_begin, p_end))
    if (p_begin + length == p_end)
      return type;
  return std::nullopt;
}

// Reads `to WORDS (PARAMETER[: TYPE]) WORDS ... [as NAME]:` into a user
// function and its templates (a statement and an expression, so the phrase
// works both ways). Throws `error` saying what is wrong.
auto read_header(const std::vector<token> &p_tokens) -> user_function {
  user_function result;
  auto end = p_tokens.size() - 1;
  std::optional<string_type> named;
  if (end >= 3 && p_tokens[end - 2].is_word("as") &&
      p_tokens[end - 1].kind == token::kind_type::word) {
    named = p_tokens[end - 1].text;
    end -= 2;
  }
  string_type pattern;
  std::vector<string_type> words;
  for (std::size_t index = 1; index < end; ++index) {
    const auto &item = p_tokens[index];
    if (item.kind == token::kind_type::word) {
      for (const auto character : item.text)
        if (!std::isalpha(static_cast<unsigned char>(character)))
          throw error("A phrase's words are letters only: '" + item.text +
                      "' isn't one.");
      pattern += " " + lower(item.text);
      if (!is_article(item))
        words.push_back(lower(item.text));
      continue;
    }
    if (!item.is_symbol("("))
      throw error("A phrase is made of words and (parameters): '" +
                  item.text + "' is neither.");
    auto close = index + 1;
    while (close < end && !p_tokens[close].is_symbol(")"))
      ++close;
    if (close == end || close == index + 1 ||
        p_tokens[index + 1].kind != token::kind_type::word ||
        (close > index + 2 && !p_tokens[index + 2].is_symbol(":")))
      throw error("Write a parameter as (name) or (name: type), for "
                  "example (width: whole number).");
    const auto &name = p_tokens[index + 1].text;
    if (std::ranges::find(result.parameters, name) != result.parameters.end())
      throw error("Two parameters are called '" + name + "'.");
    std::optional<value_kind> type;
    if (close > index + 2) {
      type = stated_type(p_tokens, index + 3, close);
      if (!type)
        throw error("'" + name + "' needs a type Ovis knows: a whole number, "
                    "a decimal, text, a truth value or a list of one.");
    }
    result.parameters.push_back(name);
    result.types.push_back(type);
    pattern += " {" + name + ": " + (type ? type->core() : "any") + "}";
    index = close;
  }
  if (words.empty())
    throw error("A phrase needs at least one word besides its parameters.");
  if (named) {
    result.name = *named;
  } else {
    for (const auto &word : words)
      result.name += (result.name.empty() ? "" : "_") + word;
  }
  // A name Ovis Core already gives a meaning would call that instead.
  result.core = core_name(result.name);

  string_type call = "(" + result.core;
  for (const auto &parameter : result.parameters)
    call += " $" + parameter;
  call += ")";
  try {
    result.templates = read_templates("expr" + pattern + " : any => " + call +
                                          "\nstmt" + pattern + " => " + call +
                                          "\n",
                                      "");
  } catch (const error &p_error) {
    // "LINE: message" from the template reader.
    const string_type message = p_error.what();
    const auto colon = message.find(": ");
    throw error(colon == string_type::npos ? message : message.substr(colon + 2));
  }
  return result;
}

// Ovis Core is written with a comment line naming the source line each
// statement comes from, so problems the checker finds point at that line.
auto line_marker(std::size_t p_line) -> string_type {
  return "\n;@" + std::to_string(p_line) + "\n";
}

// For each line of a unit's Ovis Core, the source line it comes from.
auto source_lines(string_view_type p_core, std::size_t p_first)
    -> std::vector<std::size_t> {
  std::vector<std::size_t> result = {p_first};
  auto current = p_first;
  for (std::size_t begin = 0; begin <= p_core.size();) {
    auto end = p_core.find('\n', begin);
    if (end == string_view_type::npos)
      end = p_core.size();
    const auto text = p_core.substr(begin, end - begin);
    if (text.starts_with(";@"))
      current = std::stoul(string_type(text.substr(2)));
    result.push_back(current);
    begin = end + 1;
  }
  return result;
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
  vocabulary words;
  std::map<string_type, value_kind> scope;
  // `let`s for variables first set inside a block, put before the current
  // top-level line so the variable lives on after the block.
  std::vector<string_type> hoisted;
  std::size_t counters = 0;
  // The user functions, by the line of their header.
  std::map<std::size_t, user_function> functions;
  // While reading a function's body: its depth, and its parameters and
  // locals (an assignment to any other name makes a local, as in Python).
  bool in_function = false;
  // When each name was last set, for `it` and left-out arguments.
  std::map<string_type, std::size_t> set_at;
  std::size_t sets = 0;
  std::set<string_type> template_words;
  std::size_t base_depth = 0;
  std::set<string_type> locals;

  auto problem(const source_line &p_line, const string_type &p_message)
      -> void {
    result.problems.push_back("line " + std::to_string(p_line.line) + ": " +
                              p_message);
  }

  // Every word of every phrase, in lower case.
  auto phrase_words() -> const std::set<string_type> & {
    if (template_words.empty())
      for (const auto &item : words.templates)
        for (const auto &pattern : item.patterns)
          for (const auto &element : pattern) {
            template_words.insert(element.word);
            for (const auto &branch : element.branches)
              for (const auto &part : branch)
                template_words.insert(part.word);
          }
    return template_words;
  }

  // Names in scope, the most recently set first.
  auto recent() const -> std::vector<string_type> {
    std::vector<string_type> names;
    for (const auto &[name, type] : scope)
      names.push_back(name);
    std::ranges::stable_sort(names, [&](const string_type &p_left,
                                        const string_type &p_right) {
      const auto left = set_at.find(p_left);
      const auto right = set_at.find(p_right);
      return (left == set_at.end() ? 0 : left->second) >
             (right == set_at.end() ? 0 : right->second);
    });
    return names;
  }

  auto remember(const string_type &p_name) -> void { set_at[p_name] = ++sets; }

  auto nothing_called(const string_type &p_word) const -> string_type {
    std::vector<string_type> names;
    for (const auto &[name, type] : scope)
      names.push_back(name);
    const auto closest = core::closest_name(p_word, names);
    return "Nothing called '" + p_word + "' is known here" +
           (closest ? "; did you mean '" + *closest + "'?" : ".");
  }

  // What is unclear about a line that has `p_readings` (none or several),
  // with its candidates and the answer of a deterministic rule, if any.
  auto question_of(const working_line &p_line, const line_shape &p_shape,
                   const std::vector<parse> &p_readings) -> question {
    question result{.begin = p_shape.begin, .end = p_shape.end};
    if (p_readings.size() > 1) {
      result.kind = question_kind::ambiguous_parse;
      for (const auto &reading : p_readings)
        result.candidates.push_back({reading.reading});
      return result;
    }
    // Find the unclear part: the shortest span nothing reads, in a reading
    // of the rest.
    bool statement = p_shape.is_statement();
    line_parser finder(p_line.tokens, scope, words, true);
    for (const auto &reading : readings_of(finder, p_shape))
      if (reading.holes == 1 &&
          reading.hole_end - reading.hole_begin < result.end - result.begin) {
        result.begin = reading.hole_begin;
        result.end = reading.hole_end;
        statement = false;
      }
    // Several words are unclear: a misspelt name among them is fixed first
    // (`show totl + nmbr`), by the closest name in scope, whatever its type;
    // the next round checks it.
    if (result.end - result.begin > 1)
      for (auto index = result.begin; index < result.end; ++index) {
        const auto &item = p_line.tokens[index];
        if (item.kind != token::kind_type::word || scope.contains(item.text) ||
            phrase_words().contains(lower(item.text)))
          continue;
        const auto names = recent();
        if (const auto closest = core::closest_name(item.text, names))
          return {.kind = question_kind::unknown_name,
                  .begin = index,
                  .end = index + 1,
                  .candidates = {{*closest}},
                  .rule = 0};
      }
    const auto &first = p_line.tokens[result.begin];
    if (result.end - result.begin == 1 && first.kind == token::kind_type::word) {
      result.kind = is_pronoun(first.text) ? question_kind::reference
                                           : question_kind::unknown_name;
      result.candidates = name_candidates(p_line.tokens, result.begin, p_shape,
                                          scope, recent(), words);
      if (result.kind == question_kind::reference) {
        if (result.candidates.size() == 1)
          result.rule = 0;
      } else {
        std::vector<string_type> names;
        for (const auto &item : result.candidates)
          names.push_back(item.reading);
        if (const auto closest = core::closest_name(first.text, names))
          result.rule = static_cast<std::size_t>(
              std::ranges::find(names, *closest) - names.begin());
      }
      return result;
    }
    result.candidates =
        unknown_candidates(p_line.tokens, result.begin, result.end, statement,
                           p_shape.takes_block, scope, recent(), words);
    return result;
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
          p_shape.kind == line_shape::kind_type::skip ||
          p_shape.kind == line_shape::kind_type::define ||
          (p_shape.kind == line_shape::kind_type::give_back &&
           p_shape.begin == p_shape.end))
        return parse{};

      line_parser parser(p_line.tokens, scope, words, false);
      auto readings = readings_of(parser, p_shape);
      if (readings.size() == 1)
        return readings.front();

      ++result.timing.questions;
      auto asked = question_of(p_line, p_shape, readings);
      const auto span = span_text(p_line, asked.begin, asked.end);
      if (asked.candidates.empty()) {
        if (asked.kind == question_kind::unknown_name ||
            asked.kind == question_kind::reference)
          problem(source, nothing_called(p_line.tokens[asked.begin].text));
        else
          problem(source, "No phrase Ovis knows reads \"" + span + "\".");
        return std::nullopt;
      }
      std::optional<string_type> chosen;
      if (asked.rule) {
        chosen = asked.candidates[*asked.rule].reading;
        ++result.timing.by_rule;
      } else if (const auto shortcut = ranking_shortcut(asked.candidates)) {
        chosen = asked.candidates[*shortcut].reading;
        ++result.timing.by_ranking;
      } else {
        chosen = asker.choose(p_line.text, known_names(p_line.tokens, scope),
                              span, asked.candidates);
        if (chosen)
          ++result.timing.by_model;
      }
      if (!chosen) {
        string_type listed;
        for (std::size_t index = 0; index < asked.candidates.size() && index < 4;
             ++index)
          listed += "\n    " + asked.candidates[index].reading;
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
      const auto line = lines[p_index].line;
      auto form = read_statement(p_index, p_depth);
      if (!form.empty())
        forms.push_back(line_marker(line) + form);
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
      remember(shape.name);
      auto existing = scope.find(shape.name);
      if (in_function && locals.insert(shape.name).second)
        existing = scope.end();
      if (existing == scope.end()) {
        scope[shape.name] = value->type;
        if (p_depth > base_depth) {
          hoisted.push_back("(let " + core_name(shape.name) + " " +
                            default_value(value->type) + ")");
          return "(set " + core_name(shape.name) + " " + value->core + ")";
        }
        return "(let " + core_name(shape.name) + " " + value->core + ")";
      }
      if (!existing->second.known())
        existing->second = value->type;
      return "(set " + core_name(shape.name) + " " + value->core + ")";
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
      remember(shape.name);
      return "(each " + core_name(shape.name) + " " + value->core +
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
    case define:
      return read_function(source, p_index, p_depth);
    case give_back:
      if (!in_function) {
        problem(source, "'give back' only works inside a 'to …:' phrase.");
        return {};
      }
      return value->core.empty() ? "(return)" : "(return " + value->core + ")";
    }
    return {};
  }

  // A `to …:` line and its body, as an Ovis Core `fn`.
  auto read_function(const source_line &p_source, std::size_t &p_index,
                     std::size_t p_depth) -> string_type {
    const auto skip_body = [&] {
      while (p_index < lines.size() && lines[p_index].depth > p_depth)
        ++p_index;
    };
    if (p_depth > 0 || in_function) {
      problem(p_source, "Define phrases ('to …:') at the top level, not "
                        "inside a block.");
      skip_body();
      return {};
    }
    const auto found = functions.find(p_source.line);
    if (found == functions.end()) {
      // Its header couldn't be read (reported).
      skip_body();
      return {};
    }
    const auto &function = found->second;
    const auto saved_scope = scope;
    auto saved_hoisted = std::exchange(hoisted, {});
    in_function = true;
    base_depth = p_depth + 1;
    locals.clear();
    string_type parameters;
    for (std::size_t index = 0; index < function.parameters.size(); ++index) {
      const auto &name = function.parameters[index];
      const auto &type = function.types[index];
      scope[name] = type.value_or(value_kind{});
      locals.insert(name);
      remember(name);
      parameters += (index > 0 ? " " : "") +
                    (type ? "(" + core_name(name) + " " + type->core() + ")"
                          : core_name(name));
    }
    const auto body = joined(read_body(p_index, p_depth));
    string_type form = "(fn " + function.core + " (" + parameters + ")";
    for (const auto &let : hoisted)
      form += " " + let;
    form += body + ")";
    scope = saved_scope;
    hoisted = std::move(saved_hoisted);
    in_function = false;
    base_depth = 0;
    return form;
  }

public:
  program_reader(string_view_type p_source, english::language_model *p_model,
                 const front_end_options &p_options, front_end_result &p_result)
      : model(p_model), options(p_options), result(p_result),
        asker(p_model, p_options, p_result.timing) {
    words.templates = builtin_templates();
    add_libc(words);
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
        if (single_line && apply_decision(item, note.text)) {
          item.decisions.push_back(note.text);
          ++result.timing.by_source;
        }
        else
          item.decisions_changed = true;
      }
      working.push_back(std::move(item));
    }

    // Phrases the program defines are known on every line, before and
    // after their definition.
    std::map<string_type, std::size_t> defined;
    for (const auto &line : lines) {
      if (line.depth != 0 ||
          shape_of(line.tokens).kind != line_shape::kind_type::define)
        continue;
      try {
        auto function = read_header(line.tokens);
        if (const auto [previous, added] =
                defined.try_emplace(function.name, line.line);
            !added) {
          problem(line, "A phrase called '" + function.name +
                            "' is already defined on line " +
                            std::to_string(previous->second) +
                            ". Name one of them with 'as NAME' before the ':'.");
          continue;
        }
        std::ranges::copy(function.templates,
                          std::back_inserter(words.templates));
        words.functions[function.name] = {
            .core = function.core, .arity = function.parameters.size()};
        functions.emplace(line.line, std::move(function));
      } catch (const error &p_error) {
        problem(line, p_error.what());
      }
    }
  }

  // Top-level lines as Ovis Core, with the source line each starts at.
  auto read() -> std::vector<std::pair<std::size_t, string_type>> {
    std::vector<std::pair<std::size_t, string_type>> units;
    std::size_t index = 0;
    while (index < lines.size()) {
      const auto line = lines[index].line;
      auto form = read_statement(index, 0);
      string_type text = line_marker(line);
      for (const auto &let : hoisted)
        text += let + "\n";
      hoisted.clear();
      text += form;
      if (!form.empty())
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
  std::vector<std::vector<std::size_t>> unit_lines = {{}};
  result.program.units.push_back(core::parse_unit(builtin_prelude()));
  for (const auto &[line, text] : units) {
    try {
      result.program.units.push_back(core::parse_unit(text));
      unit_lines.push_back(source_lines(text, line));
    } catch (const error &p_error) {
      result.problems.push_back("line " + std::to_string(line) +
                                ": Internal compiler error: " + p_error.what() +
                                "\n    Ovis Core: " + text);
    }
  }
  if (result.succeeded()) {
    auto notes = core::elaborate(result.program);
    for (const auto &problem : core::check(result.program, &notes)) {
      const auto &lines = unit_lines[problem.unit_index];
      const auto line = lines.empty() ? 0
                        : problem.location.line < lines.size()
                            ? lines[problem.location.line]
                            : lines.front();
      result.problems.push_back(
          (line == 0 ? string_type("prelude") : "line " + std::to_string(line)) +
          ": " + problem.message);
    }
  }
  result.timing.check_ms = milliseconds_since(check_start);
  return result;
}

} // namespace ovis::phrase