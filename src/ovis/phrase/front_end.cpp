#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>

#include <ovis/core/checker.hpp>
#include <ovis/core/c_import.hpp>
#include <ovis/core/elaborate.hpp>
#include <ovis/core/modules.hpp>
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

// A `let` for a variable first set inside a block, before its value is
// known: declared with its type, or `?` for the elaborator to infer.
auto declaration(const string_type &p_name, const value_kind &p_type)
    -> string_type {
  return "(let " + core_name(p_name) + " " +
         (p_type.known() ? p_type.core() : string_type("?")) + ")";
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
                 std::size_t p_end, const vocabulary &p_vocabulary)
    -> std::optional<value_kind> {
  if (p_end - p_begin > 2 && p_tokens[p_begin].is_word("list") &&
      p_tokens[p_begin + 1].is_word("of")) {
    auto element = stated_type(p_tokens, p_begin + 2, p_end, p_vocabulary);
    if (!element || element->depth > 0)
      return std::nullopt;
    return element->with_depth(1);
  }
  for (const auto &[length, type] :
       type_words(p_tokens, p_begin, p_end, &p_vocabulary))
    if (p_begin + length == p_end)
      return type;
  return std::nullopt;
}

// Reads `to WORDS (PARAMETER[: TYPE]) WORDS ... [as NAME]:` into a user
// function and its templates (a statement and an expression, so the phrase
// works both ways). Throws `error` saying what is wrong.
auto read_header(const std::vector<token> &p_tokens,
                 const vocabulary &p_vocabulary) -> user_function {
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
    if (item.is_symbol(",")) {
      pattern += " ,";
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
      type = stated_type(p_tokens, index + 3, close, p_vocabulary);
      if (!type)
        throw error("'" + name + "' needs a type Ovis knows: a whole number, "
                    "a decimal, text, a truth value, a record or a list of "
                    "one.");
    }
    result.parameters.push_back(name);
    result.types.push_back(type);
    // Templates have no record types: a record is `any` to the type filter.
    const auto slot_type =
        !type ? string_type("any")
        : type->base == "record"
            ? "any" + string_type(type->depth > 0 ? "[]" : "")
            : type->core();
    pattern += " {" + name + ": " + slot_type + "}";
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
// What the files of a program share while they are read.
struct program_context {
  english::language_model *model;
  const front_end_options &options;
  front_end_timing timing;
  question_asker asker;
  vocabulary words;
  // Top-level variables of the files read so far, visible to the next.
  std::map<string_type, value_kind> globals;
  // The templates lines were read with, as "lists.phr:12".
  std::set<string_type> templates_used;
  // The program's phrases by their Ovis Core name, as their headers say
  // them, for reports.
  std::map<string_type, string_type> phrase_names;
  // Phrase names, with where each is defined ("in tax.ovis on line 2").
  std::map<string_type, string_type> defined;
  // Records' names, the same way.
  std::map<string_type, string_type> records_defined;

  program_context(english::language_model *p_model,
                  const front_end_options &p_options)
      : model(p_model), options(p_options), asker(p_model, p_options, timing) {
    words.templates = builtin_templates();
    add_libc(words);
  }
};

class program_reader {
  std::vector<source_line> lines;
  std::vector<working_line> working;
  program_context &shared;
  english::language_model *model;
  const front_end_options &options;
  // The file's lockfile from the last build.
  const lockfile &previous;
  // For this file: its problems, source and lockfile.
  front_end_result &result;
  front_end_timing &timing;
  question_asker &asker;
  vocabulary &words;
  // How reports name the file: "pricing/tax.ovis", and as the start of a
  // report, "pricing/tax.ovis, "; both empty for a single file.
  string_type file_name;
  string_type file_prefix;
  std::map<string_type, value_kind> scope;
  // Variables of other files: assigning one makes this file's own.
  std::set<string_type> foreign;
  // C headers this file uses, with their hashes.
  std::map<string_type, string_type> headers;
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
  bool model_used = false;
  std::size_t base_depth = 0;
  std::set<string_type> locals;
  // The lines that declare records (`a book has:`), and each record's Ovis
  // Core by the line it is declared on.
  std::vector<std::size_t> record_lines;
  std::map<std::size_t, string_type> record_forms;

  // "in tax.ovis on line 2", for reports about a second definition.
  auto where(const source_line &p_line) const -> string_type {
    return (file_name.empty() ? "" : "in " + file_name + " ") + "on line " +
           std::to_string(p_line.line);
  }

  // A field's line: `title`, `title (text)`, `price (decimal, 0)` or
  // `price (0)`. Throws `error` saying what is wrong.
  auto read_field(const std::vector<token> &p_tokens) -> record_field {
    const auto malformed = [] {
      return error("Write a field as its name, then in brackets its type, a "
                   "default, or both: price (decimal, 0).");
    };
    if (p_tokens.empty() || p_tokens[0].kind != token::kind_type::word)
      throw malformed();
    record_field field{.name = p_tokens[0].text};
    if (p_tokens.size() == 1)
      return field;
    if (p_tokens.size() < 4 || !p_tokens[1].is_symbol("(") ||
        !p_tokens.back().is_symbol(")"))
      throw malformed();
    const auto end = p_tokens.size() - 1;
    auto comma = end;
    int level = 0;
    for (std::size_t index = 2; index < end && comma == end; ++index) {
      const auto &item = p_tokens[index];
      level += item.is_symbol("(") || item.is_symbol("[");
      level -= item.is_symbol(")") || item.is_symbol("]");
      if (level == 0 && item.is_symbol(","))
        comma = index;
    }
    auto value_begin = std::size_t(2);
    if (comma < end) {
      const auto type = stated_type(p_tokens, 2, comma, words);
      if (!type)
        throw error("'" + field.name + "' needs a type Ovis knows: a whole "
                    "number, a decimal, text, a truth value, a record or a "
                    "list of one.");
      field.type = *type;
      value_begin = comma + 1;
    } else if (const auto type = stated_type(p_tokens, 2, end, words)) {
      field.type = *type;
      return field;
    }
    // The default, read on its own: it may use no variables.
    const std::vector<token> value(p_tokens.begin() +
                                       static_cast<std::ptrdiff_t>(value_begin),
                                   p_tokens.begin() +
                                       static_cast<std::ptrdiff_t>(end));
    if (value.empty())
      throw malformed();
    const std::map<string_type, value_kind> no_names;
    line_parser parser(value, no_names, words, false);
    const auto &readings = parser.expressions(0, value.size());
    if (readings.size() != 1)
      throw error(readings.empty()
                      ? "The default of '" + field.name +
                            "' can't be read: write a type or a value such as "
                            "0 or \"none\"."
                      : "The default of '" + field.name +
                            "' reads several ways; put brackets around it.");
    if (!assignable(field.type, readings[0].type))
      throw error("The default of '" + field.name + "' is " +
                  readings[0].type.english() + ", but '" + field.name +
                  "' is " + field.type.english() + ".");
    if (!field.type.known())
      field.type = readings[0].type;
    field.default_core = readings[0].core;
    return field;
  }

  auto problem(const source_line &p_line, const string_type &p_message)
      -> void {
    result.problems.push_back(file_prefix + "line " +
                              std::to_string(p_line.line) + ": " + p_message);
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
    // A hole where a field's name goes wins over the same hole read
    // otherwise: its candidates are the record's fields.
    string_type field_record;
    for (const auto &reading : readings_of(finder, p_shape)) {
      if (reading.holes != 1)
        continue;
      const auto size = reading.hole_end - reading.hole_begin;
      if (size < result.end - result.begin ||
          (size == result.end - result.begin &&
           reading.hole_begin == result.begin && field_record.empty() &&
           !reading.hole_record.empty())) {
        result.begin = reading.hole_begin;
        result.end = reading.hole_end;
        field_record = reading.hole_record;
        statement = false;
      }
    }
    // Readings must fit where the span is: the line has to read with the
    // span replaced (a number where `rounded to 2 decimals` needs one).
    const auto fits_at = [&](std::size_t p_begin, std::size_t p_end,
                             const string_type &p_reading) {
      auto trial = p_line;
      if (!apply_decision(trial, "\"" + span_text(p_line, p_begin, p_end) +
                                     "\" is " + p_reading))
        return false;
      const auto shape = shape_of(trial.tokens);
      line_parser parser(trial.tokens, scope, words, false);
      return shape.problem.empty() && !readings_of(parser, shape).empty();
    };
    if (!field_record.empty()) {
      std::vector<string_type> names;
      for (const auto &field : words.records.at(field_record).fields)
        names.push_back(field.name);
      result.kind = question_kind::unknown_name;
      result.record = field_record;
      result.candidates = word_candidates(p_line.tokens, result.begin, p_shape,
                                          scope, names, words);
      names.clear();
      for (const auto &item : result.candidates)
        names.push_back(item.reading);
      if (const auto closest =
              core::closest_name(p_line.tokens[result.begin].text, names))
        result.rule = static_cast<std::size_t>(
            std::ranges::find(names, *closest) - names.begin());
      return result;
    }
    // A record's name where a value goes, `the book`: one of the variables
    // holding that record.
    if (const auto [record, length] =
            words.record_at(p_line.tokens, result.begin, result.end);
        record != nullptr && length == result.end - result.begin) {
      if (result.begin > p_shape.begin &&
          is_article(p_line.tokens[result.begin - 1]))
        --result.begin;
      result.kind = question_kind::reference;
      result.record = record->name;
      for (const auto &name : recent())
        if (scope.at(name) == record->type() &&
            fits_at(result.begin, result.end, name))
          result.candidates.push_back({name});
      if (result.candidates.size() == 1)
        result.rule = 0;
      return result;
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
    const auto fits = [&](const string_type &p_reading) {
      return fits_at(result.begin, result.end, p_reading);
    };
    result.candidates = unknown_candidates(p_line.tokens, result.begin,
                                           result.end, statement,
                                           p_shape.takes_block, scope,
                                           recent(), words, fits);
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
          p_shape.kind == line_shape::kind_type::use_c ||
          p_shape.kind == line_shape::kind_type::record ||
          (p_shape.kind == line_shape::kind_type::give_back &&
           p_shape.begin == p_shape.end))
        return parse{};

      line_parser parser(p_line.tokens, scope, words, false);
      auto readings = readings_of(parser, p_shape);
      if (readings.size() == 1)
        return readings.front();

      ++timing.questions;
      auto asked = question_of(p_line, p_shape, readings);
      const auto span = span_text(p_line, asked.begin, asked.end);
      if (asked.candidates.empty()) {
        if (!asked.record.empty()) {
          const auto type = words.records.at(asked.record).type();
          if (asked.kind == question_kind::reference) {
            problem(source, "No variable here holds " + type.english() +
                                ", so \"" + span + "\" means nothing yet.");
          } else {
            string_type fields;
            for (const auto &field : words.records.at(asked.record).fields)
              fields += (fields.empty() ? "" : ", ") + field.name;
            auto owner = type.english();
            owner[0] = static_cast<char_type>(std::toupper(owner[0]));
            problem(source, owner + " has no field called '" + span +
                                "'. Its fields are: " + fields + ".");
          }
        } else if (asked.kind == question_kind::unknown_name ||
                   asked.kind == question_kind::reference)
          problem(source, nothing_called(p_line.tokens[asked.begin].text));
        else
          problem(source, "No phrase Ovis knows reads \"" + span + "\".");
        return std::nullopt;
      }
      std::optional<string_type> chosen;
      std::vector<string_type> readings_offered;
      for (const auto &item : asked.candidates)
        readings_offered.push_back(item.reading);
      const auto known = known_names(p_line.tokens, scope);
      const auto key = question_key(p_line.text, span, known, readings_offered);
      const auto *const locked = previous.find(key);
      if (locked != nullptr &&
          std::ranges::find(readings_offered, locked->reading) !=
              readings_offered.end()) {
        chosen = locked->reading;
        ++timing.by_lockfile;
        if (result.lock.find(key) == nullptr)
          result.lock.readings.push_back(*locked);
      } else if (asked.rule) {
        chosen = asked.candidates[*asked.rule].reading;
        ++timing.by_rule;
      } else if (const auto shortcut = ranking_shortcut(asked.candidates)) {
        chosen = asked.candidates[*shortcut].reading;
        ++timing.by_ranking;
      } else {
        // Only now, when the model is asked anyway: builds the lockfile,
        // a rule or the shortcut settles never load it.
        const bool by_embedding = options.choose_by_embedding &&
                                  asked.kind == question_kind::unknown_phrase;
        if (by_embedding || (options.order_ties_by_embedding &&
                             asked.kind == question_kind::unknown_phrase))
          asker.order_ties(span, asked.candidates);
        if (by_embedding)
          chosen = asker.unavailable.empty()
                       ? std::optional(asked.candidates.front().reading)
                       : std::nullopt;
        else
          chosen = asker.choose(p_line.text, known, span, asked.candidates);
        if (chosen) {
          ++timing.by_model;
          if (result.lock.find(key) == nullptr)
            result.lock.readings.push_back({key, span, *chosen});
          model_used = true;
        }
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
    if (value)
      for (const auto *used : value->uses)
        shared.templates_used.insert(used->file + ":" +
                                     std::to_string(used->line));
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
      if ((in_function && locals.insert(shape.name).second) ||
          (!in_function && foreign.erase(shape.name) > 0))
        existing = scope.end();
      if (existing == scope.end()) {
        scope[shape.name] = value->type;
        if (p_depth > base_depth) {
          hoisted.push_back(declaration(shape.name, value->type));
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
      // The loop's variable lives only in the loop, as in Ovis Core: after
      // it the name means what it meant before, if anything.
      const auto outer = scope.find(shape.name) == scope.end()
                             ? std::nullopt
                             : std::optional(scope[shape.name]);
      scope[shape.name] =
          value->type.known() ? value->type.element() : value_kind{};
      remember(shape.name);
      auto form = "(each " + core_name(shape.name) + " " + value->core +
                  joined(read_body(p_index, p_depth)) + ")";
      if (outer)
        scope[shape.name] = *outer;
      else
        scope.erase(shape.name);
      return form;
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
    case record: {
      skip_body();
      if (p_depth > 0 || in_function) {
        problem(source, "Declare records ('a NAME has:') at the top level, "
                        "not inside a block.");
        return {};
      }
      const auto found = record_forms.find(source.line);
      return found == record_forms.end() ? string_type() : found->second;
    }
    case use_c:
      if (p_depth > 0 || in_function) {
        problem(source, "Write 'use' lines at the top level.");
        return {};
      }
      return "(use-c " + quote(shape.name) + " " + quote(shape.library) + ")";
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
  program_reader(const phrase_file &p_file, program_context &p_shared,
                 front_end_result &p_result)
      : shared(p_shared), model(p_shared.model), options(p_shared.options),
        previous(p_file.lock), result(p_result), timing(p_shared.timing),
        asker(p_shared.asker), words(p_shared.words),
        file_name(p_file.name),
        file_prefix(p_file.name.empty() ? string_type() : p_file.name + ", ") {
    const string_view_type p_source = p_file.text;
    const auto start = clock_type::now();
    lines = split_lines(p_source);
    timing.lex_ms += milliseconds_since(start);
    timing.lines += lines.size();

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
          ++timing.by_source;
        }
        else
          item.decisions_changed = true;
      }
      working.push_back(std::move(item));
    }

    // Records are known in every file, before anything else is read: a
    // phrase's parameter or another record's field may be one.
    for (std::size_t index = 0; index < lines.size(); ++index) {
      const auto &line = lines[index];
      const auto shape = shape_of(line.tokens);
      if (line.depth != 0 || shape.kind != line_shape::kind_type::record ||
          !shape.problem.empty())
        continue;
      const auto as_type = type_words(line.tokens, shape.begin, shape.end);
      if (core_name(shape.name) != shape.name ||
          std::ranges::any_of(as_type, [&](const auto &p_match) {
            return p_match.first == shape.end - shape.begin;
          })) {
        problem(line, "'" + span_text(working[index], shape.begin, shape.end) +
                          "' is already a type Ovis has: call the record "
                          "something else.");
        continue;
      }
      if (const auto [defined_at, added] =
              shared.records_defined.try_emplace(shape.name, where(line));
          !added) {
        problem(line, "A record called '" +
                          span_text(working[index], shape.begin, shape.end) +
                          "' is already declared " + defined_at->second + ".");
        continue;
      }
      record_info record{.name = shape.name};
      for (auto at = shape.begin; at < shape.end; ++at)
        record.words.push_back(lower(line.tokens[at].text));
      words.records[shape.name] = std::move(record);
      record_lines.push_back(index);
    }
  }

  // Phrases the program defines, and C functions it uses, are known on
  // every line, before and after their definition, in every file.
  auto declare() -> void {
    for (const auto &line : lines) {
      if (line.depth != 0)
        continue;
      const auto shape = shape_of(line.tokens);
      if (shape.kind == line_shape::kind_type::use_c && shape.problem.empty())
        import_header(line, shape);
      if (shape.kind != line_shape::kind_type::define)
        continue;
      try {
        auto function = read_header(line.tokens, words);
        if (const auto [defined_at, added] =
                shared.defined.try_emplace(function.name, where(line));
            !added) {
          problem(line, "A phrase called '" + function.name +
                            "' is already defined " + defined_at->second +
                            ". Name one of them with 'as NAME' before the ':'.");
          continue;
        }
        std::ranges::copy(function.templates,
                          std::back_inserter(words.templates));
        words.functions[function.name] = {
            .core = function.core, .arity = function.parameters.size()};
        // Reports name it as its header says it: "pad (text) to (width)".
        const auto header = std::ranges::find(working, &line, &working_line::source);
        if (header != working.end()) {
          auto text = header->text.substr(header->text.find_first_of(" \t") + 1);
          while (!text.empty() && (text.back() == ':' || text.back() == ' '))
            text.pop_back();
          shared.phrase_names[function.core] = text;
        }
        functions.emplace(line.line, std::move(function));
      } catch (const error &p_error) {
        problem(line, p_error.what());
      }
    }
  }

  // Each record's fields, once every file's records are known (a field may
  // hold another record).
  auto read_records() -> void {
    for (const auto index : record_lines) {
      const auto &line = lines[index];
      auto &record = words.records.at(shape_of(line.tokens).name);
      string_type form = "(record " + record.name;
      for (auto at = index + 1; at < lines.size() && lines[at].depth > 0; ++at) {
        const auto &field_line = lines[at];
        if (field_line.tokens.back().is_symbol(":")) {
          problem(field_line, "A field's line opens no block: remove the ':'.");
          continue;
        }
        if (field_line.depth > 1)
          continue;
        try {
          auto field = read_field(field_line.tokens);
          if (std::ranges::any_of(record.fields, [&](const record_field &p_other) {
                return lower(p_other.name) == lower(field.name);
              })) {
            problem(field_line, "'" + field.name + "' is listed twice.");
            continue;
          }
          form += " (" + field.name +
                  (field.type.known() ? " " + field.type.core() : "") + ")";
          record.fields.push_back(std::move(field));
        } catch (const error &p_error) {
          problem(field_line, p_error.what());
        }
      }
      record_forms[line.line] = form + ")";
    }
  }

  // Makes a header's functions and constants callable everywhere.
  auto import_header(const source_line &p_line, const line_shape &p_shape)
      -> void {
    if (!core::has_c_import_support()) {
      problem(p_line, "This build of Ovis can't read C headers: it was built "
                      "without libclang.");
      return;
    }
    try {
      const auto &library = core::import_c_library(p_shape.name, p_shape.library,
                                                   options.c_imports);
      for (const auto &function : library.functions)
        words.functions.try_emplace(
            function.name,
            callable{.core = function.name,
                     .result = kind_of(core::ovis_type_of(function.result)),
                     .arity = function.is_variadic
                                  ? std::nullopt
                                  : std::optional(function.parameters.size())});
      for (const auto &constant : library.constants)
        words.constants.try_emplace(constant.name, kind_of(constant));
      headers[p_shape.name] = library.header_hash;
      if (const auto found = previous.c_headers.find(p_shape.name);
          found != previous.c_headers.end() &&
          found->second != library.header_hash)
        result.warnings.push_back(
            file_prefix + "line " + std::to_string(p_line.line) + ": " +
            p_shape.name + " changed since the last build; check the program "
                           "still does what it should.");
    } catch (const error &p_error) {
      problem(p_line, p_error.what());
    }
  }

  // Who made the lockfile's readings: this build's model if it answered,
  // else whoever made the previous ones.
  auto finish_lockfile() -> void {
    auto &lock = result.lock;
    lock.c_headers = headers;
    // Readings the source's `Read as:` lines now carry stay, so rebuilding
    // changes neither file.
    std::set<string_type> written;
    for (const auto &line : working)
      for (const auto &note : line.source->annotations)
        if (note.kind == annotation::kind_type::read_as)
          written.insert(note.text);
    for (const auto &entry : previous.readings)
      if (written.contains("\"" + entry.span + "\" is " + entry.reading) &&
          lock.find(entry.key) == nullptr)
        lock.readings.push_back(entry);
    if (model_used) {
      lock.model = model->name();
      lock.model_fingerprint = model->fingerprint();
    } else if (!lock.readings.empty()) {
      lock.model = previous.model;
      lock.model_fingerprint = previous.model_fingerprint;
    }
    // In a fixed order, whatever order the questions came in.
    std::ranges::sort(lock.readings, {}, &reading_entry::key);
  }

  // Top-level lines as Ovis Core, with the source line each starts at.
  auto read() -> std::vector<std::pair<std::size_t, string_type>> {
    std::vector<std::pair<std::size_t, string_type>> units;
    scope = shared.globals;
    for (const auto &[name, type] : scope)
      foreign.insert(name);
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
    shared.globals = scope;
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

namespace {

// The text of a source line, trimmed, to quote in a report.
auto source_line_text(string_view_type p_source, std::size_t p_line)
    -> string_type {
  std::size_t begin = 0;
  for (std::size_t number = 1; number < p_line && begin != string_view_type::npos;
       ++number) {
    begin = p_source.find('\n', begin);
    if (begin != string_view_type::npos)
      ++begin;
  }
  if (begin == string_view_type::npos)
    return {};
  auto line = p_source.substr(begin, p_source.find('\n', begin) - begin);
  while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front())))
    line.remove_prefix(1);
  while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back())))
    line.remove_suffix(1);
  return string_type(line);
}

// A checker problem in the author's words: the names Ovis Core gave to
// phrases, built-in operations and renamed variables, as the program says
// them. Types are already in English.
auto in_authors_words(const string_type &p_message,
                      const std::map<string_type, string_type> &p_phrases)
    -> string_type {
  static const std::map<string_view_type, string_view_type> operations = {
      {"len", "length of"},
      {"get", "item … of"},
      {"append", "add … to"},
      {"to_str", "as text"},
      {"substring", "part of"},
      {"char_at", "character … of"},
      {"read_int", "ask for a whole number"},
      {"read_float", "ask for a decimal"},
      {"read_line", "ask for a line"},
      {"random", "a random number from … to"},
      {"print", "show"}};
  string_type result;
  std::size_t at = 0;
  while (at < p_message.size()) {
    const auto open = p_message.find('\'', at);
    const auto close =
        open == string_type::npos ? open : p_message.find('\'', open + 1);
    if (close == string_type::npos) {
      result += p_message.substr(at);
      break;
    }
    result += p_message.substr(at, open - at);
    auto name = p_message.substr(open + 1, close - open - 1);
    // Only a name in quotes: an apostrophe in "doesn't" pairs with nothing.
    if (name.empty() || !std::ranges::all_of(name, [](char_type p_character) {
          return std::isalnum(static_cast<unsigned char>(p_character)) ||
                 p_character == '_' || p_character == '.';
        })) {
      result += '\'';
      at = open + 1;
      continue;
    }
    if (const auto phrase = p_phrases.find(name); phrase != p_phrases.end())
      name = phrase->second;
    else if (name.starts_with("prelude_"))
      name = "a built-in phrase";
    else if (const auto operation = operations.find(name);
             operation != operations.end())
      name = string_type(operation->second);
    else if (name.size() > 1 && name.ends_with('_'))
      // A variable named like an Ovis Core word, renamed by `core_name`.
      name.pop_back();
    result += name == "a built-in phrase" ? name : "'" + name + "'";
    at = close + 1;
  }
  return result;
}

// Calls of generic functions, `prelude_NAME:RECORD:FIELD`, renamed
// `prelude_NAME__RECORD__FIELD`; each one called is copied from
// `builtin_generics()` into `p_copies`, once (copies may call others).
auto instantiate_generics(string_type &p_core, std::set<string_type> &p_done,
                          string_type &p_copies) -> void {
  static const std::regex call("(prelude_[A-Za-z0-9_]+):([A-Za-z0-9_]+):"
                               "([A-Za-z0-9_]+)");
  string_type renamed;
  auto rest = p_core.cbegin();
  for (std::sregex_iterator match(p_core.cbegin(), p_core.cend(), call), end;
       match != end; ++match) {
    const auto &found = *match;
    renamed.append(rest, found[0].first);
    rest = found[0].second;
    const auto name = found[1].str() + "__" + found[2].str() + "__" + found[3].str();
    renamed += name;
    const auto generic = builtin_generics().find(found[1].str());
    if (generic == builtin_generics().end() || !p_done.insert(name).second)
      continue;
    auto copy = generic->second;
    for (const auto &[placeholder, value] :
         {std::pair{string_type("$T"), found[2].str()},
          std::pair{string_type("$F"), found[3].str()}})
      for (auto at = copy.find(placeholder); at != string_type::npos;
           at = copy.find(placeholder, at + value.size()))
        copy.replace(at, placeholder.size(), value);
    instantiate_generics(copy, p_done, p_copies);
    p_copies += copy + "\n";
  }
  renamed.append(rest, p_core.cend());
  p_core = std::move(renamed);
}

} // namespace

auto load_phrase_program(const std::filesystem::path &p_path)
    -> std::vector<phrase_file> {
  namespace fs = std::filesystem;
  const auto load = [](const fs::path &p_file, string_type p_name,
                       string_type p_module) {
    std::ifstream input(p_file, std::ios::binary);
    if (!input)
      throw error("Cannot read " + p_file.string() + ".");
    std::stringstream text;
    text << input.rdbuf();
    return phrase_file{p_file, std::move(p_name), std::move(p_module),
                       text.str(), lockfile::load(lockfile::path_for(p_file))};
  };
  std::error_code code;
  const bool is_folder = fs::is_directory(p_path, code);
  if (!is_folder && p_path.filename() != "main.ovis")
    return {load(p_path, "", "")};

  const auto root = is_folder ? p_path : p_path.parent_path();
  const auto main = root / "main.ovis";
  if (!fs::exists(main, code))
    throw error("The folder " + root.string() +
                " has no main.ovis, the file that says what the program "
                "does.");
  std::vector<fs::path> helpers;
  for (auto entry = fs::recursive_directory_iterator(root, code);
       entry != fs::recursive_directory_iterator(); entry.increment(code)) {
    const auto &path = entry->path();
    if (entry->is_directory()) {
      // Hidden folders, and other programs (folders with their own
      // main.ovis), are not part of this program.
      if (path.filename().string().starts_with(".") ||
          fs::exists(path / "main.ovis", code))
        entry.disable_recursion_pending();
      continue;
    }
    if (path.extension() == ".ovis" && path != main)
      helpers.push_back(fs::relative(path, root));
  }
  std::ranges::sort(helpers);
  std::vector<phrase_file> result;
  for (const auto &relative : helpers) {
    auto module = relative;
    module.replace_extension();
    auto name = module.generic_string();
    std::ranges::replace(name, '/', '.');
    result.push_back(load(root / relative, relative.generic_string(), name));
  }
  result.push_back(load(main, "main.ovis", string_type(core::main_module)));
  return result;
}

auto compile_phrase_program(const std::vector<phrase_file> &p_files,
                            english::language_model *p_model,
                            const front_end_options &p_options)
    -> program_result {
  program_result result;
  const auto start = clock_type::now();
  program_context shared(p_model, p_options);
  std::vector<front_end_result> files(p_files.size());
  // Every file's phrases are known before any line is read.
  std::vector<std::optional<program_reader>> readers(p_files.size());
  for (std::size_t index = 0; index < p_files.size(); ++index) {
    try {
      readers[index].emplace(p_files[index], shared, files[index]);
    } catch (const lex_error &p_error) {
      files[index].problems.push_back(
          (p_files[index].name.empty() ? "" : p_files[index].name + ", ") +
          p_error.what());
    }
  }
  for (auto &reader : readers)
    if (reader)
      reader->declare();
  for (auto &reader : readers)
    if (reader)
      reader->read_records();
  // Ovis Core of each file, with the source line each unit starts at.
  std::vector<std::vector<std::pair<std::size_t, string_type>>> units(
      p_files.size());
  for (std::size_t index = 0; index < p_files.size(); ++index) {
    auto &file = files[index];
    file.source = p_files[index].text;
    file.lock = p_files[index].lock;
    if (!readers[index])
      continue;
    units[index] = readers[index]->read();
    readers[index]->finish_lockfile();
    file.source = readers[index]->written_source(p_files[index].text);
  }
  for (auto &file : files) {
    std::ranges::move(file.problems, std::back_inserter(result.problems));
    std::ranges::move(file.warnings, std::back_inserter(result.warnings));
    result.sources.push_back(std::move(file.source));
    result.locks.push_back(std::move(file.lock));
  }
  result.timing = shared.timing;
  result.templates_used = shared.templates_used;
  result.timing.parse_ms =
      milliseconds_since(start) - result.timing.lex_ms - result.timing.model_ms;
  if (!result.succeeded())
    return result;

  const auto check_start = clock_type::now();
  // For each unit: its file (none for the prelude) and source lines.
  struct origin {
    std::optional<std::size_t> file;
    std::vector<std::size_t> lines;
  };
  std::vector<origin> origins = {{}};
  std::set<string_type> instantiated;
  string_type copies;
  for (auto &file_units : units)
    for (auto &[line, text] : file_units)
      instantiate_generics(text, instantiated, copies);
  result.program.units.push_back(core::parse_unit(builtin_prelude() + copies));
  // In a program of several files every unit has a module, or modules
  // aren't resolved at all.
  if (!p_files.front().module.empty())
    result.program.units.back().module = "prelude";
  const auto prefix = [&](std::size_t p_file) {
    return p_files[p_file].name.empty() ? string_type()
                                        : p_files[p_file].name + ", ";
  };
  for (std::size_t index = 0; index < p_files.size(); ++index)
    for (const auto &[line, text] : units[index]) {
      try {
        result.program.units.push_back(core::parse_unit(text));
        result.program.units.back().module = p_files[index].module;
        origins.push_back({index, source_lines(text, line)});
      } catch (const error &p_error) {
        result.problems.push_back(prefix(index) + "line " +
                                  std::to_string(line) +
                                  ": Internal compiler error: " +
                                  p_error.what() + "\n    Ovis Core: " + text);
      }
    }
  const auto report = [&](const core::diagnostic &p_problem) {
    const auto &where = origins[p_problem.unit_index];
    if (!where.file) {
      result.problems.push_back("prelude: " + p_problem.message);
      return;
    }
    const auto &lines = where.lines;
    const auto line = p_problem.location.line < lines.size()
                          ? lines[p_problem.location.line]
                          : lines.front();
    result.problems.push_back(
        prefix(*where.file) + "line " + std::to_string(line) + ": " +
        in_authors_words(p_problem.message, shared.phrase_names) + "\n    " +
        source_line_text(p_files[*where.file].text, line));
  };
  if (result.succeeded()) {
    for (const auto &problem : core::resolve_modules(result.program))
      report(problem);
    for (const auto &problem :
         core::resolve_c_imports(result.program, p_options.c_imports))
      report(problem);
  }
  if (result.succeeded()) {
    auto notes = core::elaborate(result.program);
    for (const auto &problem : core::check(result.program, &notes))
      report(problem);
  }
  result.timing.check_ms = milliseconds_since(check_start);
  return result;
}

auto compile_phrases(string_view_type p_source, english::language_model *p_model,
                     const front_end_options &p_options) -> front_end_result {
  phrase_file file{.text = string_type(p_source)};
  if (p_options.previous != nullptr)
    file.lock = *p_options.previous;
  auto program = compile_phrase_program({file}, p_model, p_options);
  return {.program = std::move(program.program),
          .problems = std::move(program.problems),
          .warnings = std::move(program.warnings),
          .source = std::move(program.sources.front()),
          .lock = std::move(program.locks.front()),
          .timing = program.timing,
          .templates_used = std::move(program.templates_used)};
}

} // namespace ovis::phrase
