#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <ovis/phrase/lexer.hpp>
#include <ovis/phrase/templates.hpp>

using ovis::phrase::token;
using Catch::Matchers::ContainsSubstring;

namespace {

// Tokens written as kind letters and text: w:show t:Hello n:2 s:,
auto describe(const ovis::phrase::source_line &p_line) -> std::string {
  std::string result;
  for (const auto &item : p_line.tokens) {
    if (!result.empty())
      result += ' ';
    switch (item.kind) {
    case token::kind_type::word:
      result += "w:";
      break;
    case token::kind_type::number:
      result += "n:";
      break;
    case token::kind_type::text:
      result += "t:";
      break;
    case token::kind_type::symbol:
      result += "s:";
      break;
    }
    result += item.text;
  }
  return result;
}

auto lex_failure(std::string_view p_source) -> std::string {
  try {
    ovis::phrase::split_lines(p_source);
  } catch (const ovis::phrase::lex_error &p_error) {
    return p_error.what();
  }
  FAIL("No error for: " << p_source);
  return {};
}

auto template_failure(std::string_view p_text) -> std::string {
  try {
    ovis::phrase::read_templates(p_text, "test.phr");
  } catch (const ovis::error &p_error) {
    return p_error.what();
  }
  FAIL("No error for: " << p_text);
  return {};
}

} // namespace

TEST_CASE("Split a program into lines of tokens.", "[phrase][lexer]") {
  const auto lines = ovis::phrase::split_lines(
      "# Scores\n"
      "scores = [21.5, 23,\n"
      "    19.8]   # continued inside brackets\n"
      "\n"
      "average = mean of scores\n"
      "    Read as: average of scores\n"
      "for each score in scores:\n"
      "    if score > 20:\n"
      "        show \"Warm: \", score, 'degrees'\n"
      "            Compiler:   score is a decimal  \n"
      "show the book's title, b.pages, don't\n"
      "x != 2nd\n");
  REQUIRE(lines.size() == 7);

  REQUIRE(lines[0].line == 2);
  REQUIRE(describe(lines[0]) ==
          "w:scores s:= s:[ n:21.5 s:, n:23 s:, n:19.8 s:]");
  REQUIRE(lines[1].annotations.size() == 1);
  REQUIRE(lines[1].annotations[0].kind ==
          ovis::phrase::annotation::kind_type::read_as);
  REQUIRE(lines[1].annotations[0].text == "average of scores");
  REQUIRE(lines[1].annotations[0].line == 6);

  REQUIRE(lines[2].opens_block());
  REQUIRE(lines[2].depth == 0);
  REQUIRE(lines[3].depth == 1);
  REQUIRE(lines[4].depth == 2);
  REQUIRE(describe(lines[4]) ==
          "w:show t:Warm:  s:, w:score s:, t:degrees");
  // A note under a deeper block belongs to the line just above it.
  REQUIRE(lines[4].annotations.at(0).text == "score is a decimal");

  REQUIRE(lines[5].depth == 0);
  REQUIRE(describe(lines[5]) ==
          "w:show w:the w:book s:'s w:title s:, w:b s:. w:pages s:, w:don't");
  REQUIRE(lines[5].tokens[0].column == 1);
  REQUIRE(lines[5].tokens[0].is_word("SHOW"));
  REQUIRE(describe(lines[6]) == "w:x s:!= w:2nd");
  REQUIRE(lines[6].tokens[2].column == 6);
}

TEST_CASE("Read typographic quotes and apostrophes.", "[phrase][lexer]") {
  const auto lines = ovis::phrase::split_lines(
      "show \u201cIt\u2019s warm\u201d, the week\u2019s days\n");
  REQUIRE(describe(lines[0]) ==
          "w:show t:It\u2019s warm s:, w:the w:week s:'s w:days");
}

TEST_CASE("Number words, operators and the last line.", "[phrase][lexer]") {
  const auto lines = ovis::phrase::split_lines("x != 2nd <= 3.5 - 1\ny = 2.");
  REQUIRE(describe(lines[0]) == "w:x s:!= w:2nd s:<= n:3.5 s:- n:1");
  // "2." is the number 2 and a full stop; the parser decides about the stop.
  REQUIRE(describe(lines[1]) == "w:y s:= n:2 s:.");
}

TEST_CASE("Report layout mistakes with their place.", "[phrase][lexer]") {
  REQUIRE_THAT(lex_failure("if x:\n\tshow x\n"),
               ContainsSubstring("line 2, column 1: Indent with spaces"));
  REQUIRE_THAT(lex_failure("  show x\n"),
               ContainsSubstring("first line can't be indented"));
  REQUIRE_THAT(lex_failure("show x\n    show y\n"),
               ContainsSubstring("line 1 doesn't end with ':'"));
  REQUIRE_THAT(lex_failure("if x:\nshow y\n"),
               ContainsSubstring("Indent the block under line 1"));
  REQUIRE_THAT(lex_failure("if x:\n    show y\n  show z\n"),
               ContainsSubstring("line 3, column 3: This line doesn't line up"));
  REQUIRE_THAT(lex_failure("show x\nif y:\n"),
               ContainsSubstring("nothing is indented under it"));
  REQUIRE_THAT(lex_failure("show \"hi\n"),
               ContainsSubstring("column 6: This text has no closing quote"));
  REQUIRE_THAT(lex_failure("show (x\n\nshow y\n"),
               ContainsSubstring("line 1, column 6: This '(' is never closed"));
  REQUIRE_THAT(lex_failure("show x)\n"),
               ContainsSubstring("This ')' closes nothing"));
  REQUIRE_THAT(lex_failure("show x & y\n"),
               ContainsSubstring("'&' means nothing"));
  REQUIRE_THAT(lex_failure("show \"a\\qb\"\n"),
               ContainsSubstring("Unknown escape \\q"));
  REQUIRE_THAT(lex_failure("Read as: x\n"),
               ContainsSubstring("must be indented under the line"));
  REQUIRE_THAT(lex_failure("show x\nCompiler: x\n"),
               ContainsSubstring("must be indented under the line"));
}

TEST_CASE("Read phrase templates.", "[phrase][templates]") {
  const auto templates = ovis::phrase::read_templates(
      "# lists\n"
      "expr  average of {xs: num[]}  : float  => (/ (float (sum $xs)) (float "
      "(len $xs)))\n"
      "      alias: mean of {xs}\n"
      "\n"
      "expr  empty list of {t: type} : t[] => (array $t 0)\n"
      "expr  {x: float} with {n: int} decimal[s] : str => (fixed $x $n)\n"
      "stmt  (show|print out) [the] {items: any, ...} => (print $items...)\n"
      "stmt  keep doing {n: int} times: BLOCK => (for _ 1 $n BLOCK)\n",
      "lists.phr");
  REQUIRE(templates.size() == 5);

  const auto &average = templates[0];
  REQUIRE(average.kind == ovis::phrase::phrase_template::kind_type::expression);
  REQUIRE(average.file == "lists.phr");
  REQUIRE(average.line == 2);
  REQUIRE(average.patterns.size() == 2);
  REQUIRE(average.slots.size() == 1);
  REQUIRE(average.slots[0].type.to_string() == "num[]");
  REQUIRE(average.result->to_string() == "float");
  REQUIRE(average.core == "(/ (float (sum $xs)) (float (len $xs)))");
  REQUIRE(to_string(average, average.patterns[0]) == "average of {xs: num[]}");
  REQUIRE(to_string(average, average.patterns[1]) == "mean of {xs: num[]}");

  REQUIRE(templates[1].result->kind ==
          ovis::phrase::type_name::kind_type::type_slot);
  REQUIRE(to_string(templates[2], templates[2].patterns[0]) ==
          "{x: float} with {n: int} decimal[s]");
  REQUIRE(to_string(templates[3], templates[3].patterns[0]) ==
          "(show|print out) [the] {items: any, ...}");
  REQUIRE(templates[3].slots[0].is_variadic);
  REQUIRE(!templates[3].result);
  REQUIRE(templates[4].takes_block);
  REQUIRE(templates[4].slots[0].type.name == "int");
}

TEST_CASE("Report mistakes in phrase templates.", "[phrase][templates]") {
  const auto failure = [](std::string_view p_text) {
    return template_failure(p_text);
  };
  REQUIRE_THAT(failure("\n\nexpr size of {xs: T[]} => (len $xs)"),
               ContainsSubstring("test.phr:3: An expr needs a result type"));
  REQUIRE_THAT(failure("stmt sort {xs: T[]} : int => (sort $xs)"),
               ContainsSubstring("A stmt gives back nothing"));
  REQUIRE_THAT(failure("expr loop {n: int}: BLOCK => (for _ 1 $n BLOCK)"),
               ContainsSubstring("Only a stmt can take a block"));
  REQUIRE_THAT(failure("expr size of {xs} : int => (len $xs)"),
               ContainsSubstring("Give the slot 'xs' a type"));
  REQUIRE_THAT(failure("expr size of {xs: list} : int => (len $xs)"),
               ContainsSubstring("Unknown type 'list'"));
  REQUIRE_THAT(failure("expr pick {a: T} or {b: T} : U => (max $a $b)"),
               ContainsSubstring("U doesn't appear in any slot"));
  REQUIRE_THAT(failure("expr size of {xs: T[]} : int => (len $ys)"),
               ContainsSubstring("uses $ys, but the pattern has no such slot"));
  REQUIRE_THAT(failure("expr pick {a: int} or {b: int} : int => $a"),
               ContainsSubstring("'b' is never used"));
  REQUIRE_THAT(failure("expr size of {xs: T[]} : int => (len $xs) (len $xs)"),
               ContainsSubstring("must be one form"));
  REQUIRE_THAT(failure("stmt show {xs: any, ...} => (print $xs)"),
               ContainsSubstring("write $xs..."));
  REQUIRE_THAT(failure("stmt show {xs: any} => (print $xs...)"),
               ContainsSubstring("Only a variadic slot can be spliced"));
  REQUIRE_THAT(failure("stmt loop {n: int} => (for _ 1 $n BLOCK)"),
               ContainsSubstring("doesn't end with ': BLOCK'"));
  REQUIRE_THAT(failure("stmt loop {n: int}: BLOCK => (for _ 1 $n)"),
               ContainsSubstring("never uses BLOCK"));
  REQUIRE_THAT(failure("expr x {a: int} {b: int} : int => (+ $a $b)"),
               ContainsSubstring("Put a word between two slots"));
  REQUIRE_THAT(failure("expr {a: int} : int => $a"),
               ContainsSubstring("at least one word"));
  REQUIRE_THAT(failure("expr x {a: int} y {a: int} : int => $a"),
               ContainsSubstring("Two slots are called 'a'"));
  REQUIRE_THAT(failure("expr size of {xs: T[]} : int => (len $xs)\n"
                       "  alias: count {ys}"),
               ContainsSubstring("test.phr:2: The alias has a slot 'ys'"));
  REQUIRE_THAT(failure("expr pick {a: int} or {b: int} : int => (max $a $b)\n"
                       "  alias: choose {a}"),
               ContainsSubstring("The alias leaves out the slot 'b'"));
  REQUIRE_THAT(failure("expr size of {xs: T[]} : int => (len $xs)\n"
                       "  alias: count {xs: T[]}"),
               ContainsSubstring("Types go on the main pattern"));
  REQUIRE_THAT(failure("  alias: count {xs}"),
               ContainsSubstring("Only 'alias:' lines are indented"));
  REQUIRE_THAT(failure("fn size of {xs: T[]} : int => (len $xs)"),
               ContainsSubstring("starts with 'expr' or 'stmt'"));
  REQUIRE_THAT(failure("expr size (of) {xs: T[]} : int => (len $xs)"),
               ContainsSubstring("has one choice"));
  REQUIRE_THAT(failure("expr size of {xs: T[]} : int => (len $xs"),
               ContainsSubstring("doesn't read"));
}

TEST_CASE("The built-in phrase library reads.", "[phrase][templates]") {
  REQUIRE(!ovis::phrase::builtin_phrase_files().empty());
  const auto &templates = ovis::phrase::builtin_templates();
  REQUIRE(templates.size() >= 10);
  for (const auto &item : templates) {
    INFO(item.file << ":" << item.line);
    REQUIRE(!item.core.empty());
  }
}
