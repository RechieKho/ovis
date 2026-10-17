#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <catch2/catch_test_macros.hpp>
#include <llvm/IR/LLVMContext.h>

#include <ovis/codegen/llvm_codegen.hpp>
#include <ovis/compiler.hpp>
#include <ovis/core/checker.hpp>
#include <ovis/core/elaborate.hpp>
#include <ovis/core/modules.hpp>
#include <ovis/english/prompt.hpp>

using namespace std::literals::string_literals;

namespace {

// Replies with canned answers, in order, separately for each pass (told
// apart by the grammar it is asked to follow), and remembers what it was
// asked.
class scripted_model final : public ovis::english::language_model {
public:
  std::deque<std::string> details;
  std::deque<std::string> reviews;
  std::deque<std::string> translations;
  std::deque<std::string> explanations;
  std::vector<std::vector<ovis::english::chat_message>> requests;
  std::vector<std::string> grammars;

  auto name() const -> std::string override { return "scripted"; }

  auto chat(const std::vector<ovis::english::chat_message> &p_messages,
            std::string_view p_grammar) -> std::string override {
    requests.push_back(p_messages);
    grammars.emplace_back(p_grammar);
    auto &queue = p_grammar == ovis::english::detail_grammar()   ? details
                  : p_grammar == ovis::english::review_grammar() ? reviews
                  : p_grammar == ovis::english::explanation_grammar()
                      ? explanations
                      : translations;
    REQUIRE(!queue.empty());
    auto reply = queue.front();
    queue.pop_front();
    return reply;
  }

  // Translation requests only, in order.
  auto translation_requests() const
      -> std::vector<std::vector<ovis::english::chat_message>> {
    std::vector<std::vector<ovis::english::chat_message>> result;
    for (std::size_t index = 0; index < requests.size(); ++index)
      if (grammars[index] != ovis::english::detail_grammar() &&
          grammars[index] != ovis::english::review_grammar() &&
          grammars[index] != ovis::english::explanation_grammar())
        result.push_back(requests[index]);
    return result;
  }
};

auto detail_reply(const std::string &p_steps,
                  const std::string &p_assumptions = "") -> std::string {
  auto result = "STATUS: ok\nSTEPS:\n" + p_steps;
  if (!p_assumptions.empty())
    result += "ASSUMPTIONS:\n" + p_assumptions;
  return result;
}

auto check_text(const std::string &p_core)
    -> std::vector<ovis::core::diagnostic> {
  ovis::core::program program;
  program.units.push_back(ovis::core::parse_unit(p_core));
  ovis::core::elaborate(program);
  return ovis::core::check(program);
}

// The elaborator's notes, one string each.
auto elaborate_text(const std::string &p_core, ovis::core::program &p_program)
    -> std::vector<std::string> {
  p_program.units.push_back(ovis::core::parse_unit(p_core));
  std::vector<std::string> result;
  for (const auto &note : ovis::core::elaborate(p_program))
    result.push_back(note.message);
  return result;
}

auto mentions(const std::vector<std::string> &p_notes,
              const std::string &p_text) -> bool {
  for (const auto &note : p_notes)
    if (note.find(p_text) != std::string::npos)
      return true;
  return false;
}

auto read_file(const std::filesystem::path &p_path) -> std::string {
  std::ifstream input(p_path);
  std::stringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

// Compiles the program to a native executable and returns what it prints.
auto run_program(const ovis::core::program &p_program,
                 const std::string &p_input = "") -> std::string {
  llvm::LLVMContext context;
  auto module = ovis::codegen::generate_module(p_program, context, "test");
  ovis::codegen::optimize(*module);
  const auto executable =
      std::filesystem::temp_directory_path() / "ovis-test-program";
  ovis::codegen::build_executable(*module, executable,
                                  ovis::codegen::libraries_of(p_program));

  auto command = executable.string();
  if (!p_input.empty())
    command = "printf '" + p_input + "' | " + command;
  std::string output;
  auto *const pipe = popen(command.c_str(), "r");
  REQUIRE(pipe != nullptr);
  char buffer[256];
  while (const auto count = std::fread(buffer, 1, sizeof(buffer), pipe))
    output.append(buffer, count);
  pclose(pipe);
  std::filesystem::remove(executable);
  return output;
}

// Where the tests' own C library is.
auto test_c_imports() -> ovis::core::c_import_options {
  return {{OVIS_TEST_CLIB_INCLUDE}, {OVIS_TEST_CLIB_LIBRARY}};
}

auto run_core(const std::string &p_core, const std::string &p_input = "")
    -> std::string {
  ovis::core::program program;
  program.units.push_back(ovis::core::parse_unit(p_core));
  for (const auto &problem :
       ovis::core::resolve_c_imports(program, test_c_imports()))
    UNSCOPED_INFO(problem.message);
  ovis::core::elaborate(program);
  const auto problems = ovis::core::check(program);
  for (const auto &problem : problems)
    UNSCOPED_INFO(problem.message);
  REQUIRE(problems.empty());
  return run_program(program, p_input);
}

} // namespace

TEST_CASE("Read S-expressions.", "[sexpr]") {
  using kind = ovis::core::sexpr::kind_type;
  const auto forms = ovis::core::read_sexprs(
      "(print \"a \\\"b\\\"\" 12 -3.5) ; comment\n(x (y))");
  REQUIRE(forms.size() == 2);
  REQUIRE(forms[0].head() == "print");
  REQUIRE(forms[0].items[1].kind == kind::string);
  REQUIRE(forms[0].items[1].text == "a \"b\"");
  REQUIRE(forms[0].items[2].kind == kind::integer);
  REQUIRE(forms[0].items[2].integer == 12);
  REQUIRE(forms[0].items[3].kind == kind::real);
  REQUIRE(forms[0].items[3].real == -3.5);
  REQUIRE(forms[1].items[1].items[0].is_symbol("y"));
  REQUIRE(forms[1].location.line == 2);

  REQUIRE_THROWS_AS(ovis::core::read_sexprs("(a (b)"),
                    ovis::core::syntax_error);
  REQUIRE_THROWS_AS(ovis::core::read_sexprs("a)"), ovis::core::syntax_error);
}

TEST_CASE("Parse lenient Ovis Core.", "[parser]") {
  using kind = ovis::core::statement::kind_type;
  const auto unit = ovis::core::parse_unit(
      "(fn twice ((x float)) float (return (* x 2)))\n"
      "(let names str[] (list \"a\"))\n"
      "(let count int)\n"
      "(if (== count 0) (print \"zero\") (print \"other\"))\n"
      "(set (get names 0) \"b\")");
  REQUIRE(unit.functions.size() == 1);
  REQUIRE(unit.functions[0].return_type == ovis::core::value_type::real());
  REQUIRE(unit.statements.size() == 4);
  REQUIRE(*unit.statements[0].declared_type ==
          ovis::core::value_type::list_of(ovis::core::base_type::string));
  REQUIRE(unit.statements[1].values.empty());
  REQUIRE(unit.statements[2].values[0].text == "=");
  REQUIRE(unit.statements[2].body.size() == 1);
  REQUIRE(unit.statements[2].else_body.size() == 1);
  REQUIRE(unit.statements[3].kind == kind::put);

  REQUIRE_THROWS_AS(ovis::core::parse_unit("(let)"), ovis::error);
  REQUIRE_THROWS_AS(ovis::core::parse_unit("(if (f (fn g () void)))"),
                    ovis::error);
}

TEST_CASE("Check types and names.", "[checker]") {
  REQUIRE(check_text("(let x int 1) (set x (+ x 2.5))").size() == 1);
  REQUIRE(check_text("(let x 1.0) (set x (+ x 2))").empty());

  const auto unknown = check_text("(let total 0) (print amount)");
  REQUIRE(unknown.size() == 1);
  REQUIRE(unknown[0].message.find("Unknown variable 'amount'") !=
          std::string::npos);
  REQUIRE(unknown[0].message.find("total int") != std::string::npos);

  REQUIRE(check_text("(print (* \"a\" 1))")[0].message.find(
              "needs numbers") != std::string::npos);
  REQUIRE(check_text("(print (strlen 5))").size() == 1);
  REQUIRE(check_text("(printf \"%d %d\" 1)")[0].message.find(
              "needs 2 value(s)") != std::string::npos);
  REQUIRE(check_text("(printf \"%s\" 1)").size() == 1);
  REQUIRE(check_text("(fn f () int (return true))").size() == 1);
  REQUIRE(check_text("(break)").size() == 1);
  REQUIRE(check_text("(print (missing 1))").size() == 1);
  REQUIRE(check_text("(fn f ((a int)) int (return a)) (print (f 1 2))")
              .size() == 1);

  // Pseudo-code habits that are accepted.
  REQUIRE(check_text("(set fresh 3) (let fresh 4) (print fresh)").empty());
  REQUIRE(check_text("(fn uses_global () int (return limit)) (let limit 5)")
              .empty());
}

TEST_CASE("Split English into paragraphs.", "[english]") {
  const auto paragraphs = ovis::english::split_paragraphs(
      "First line\nstill first.\n\n\n  \nSecond.\r\n");
  REQUIRE(paragraphs.size() == 2);
  REQUIRE(paragraphs[0].text == "First line\nstill first.");
  REQUIRE(paragraphs[1].line == 6);
  REQUIRE(paragraphs[1].text == "Second.");
  REQUIRE(ovis::english::normalize("  a\n  b\tc ") == "a b c");
}

TEST_CASE("Extract code from model replies.", "[english]") {
  REQUIRE(ovis::english::extract_core("(print 1)") == "(print 1)");
  REQUIRE(ovis::english::extract_core(
              "Sure!\n```lisp\n(print 1)\n(print 2)\n```\nDone.") ==
          "(print 1)\n(print 2)");
  REQUIRE(ovis::english::extract_core("Here: (print 1) hope it helps") ==
          "(print 1)");
}

TEST_CASE("Detail, translate and repair with a model.", "[compiler]") {
  scripted_model model;
  model.details = {
      detail_reply("1. Make a number named total, starting at 0.\n"
                   "2. For every whole number i from 1 to 10, add the square "
                   "of i to total.\n"
                   "3. Print total.\n",
                   "- \"It\" means the total.\n"),
      detail_reply("1. Define a function square that takes a whole number x "
                   "and gives back x times x.\n"),
  };
  model.translations = {
      // The first paragraph uses a function that the second one defines,
      // and misspells a variable.
      "(let total 0)\n(for i 1 10 (set total (+ total (sqr i))))\n"
      "(print totl2)",
      "(fn square ((x int)) int (return (* x x)))",
      "(let total 0)\n(for i 1 10 (set total (+ total (square i))))\n"
      "(print total)",
  };
  ovis::english::lockfile lockfile;
  const auto source = "Add up the squares of 1 to 10 and print it.\n\n"
                      "The square of a number is the number times itself.\n"s;
  auto compilation = ovis::compile(source, lockfile, &model);
  INFO(compilation.report(true));
  REQUIRE(compilation.succeeded());
  REQUIRE(run_program(compilation.program) == "385\n");

  // The translator works from the steps.
  const auto translations = model.translation_requests();
  REQUIRE(translations.size() == 3);
  REQUIRE(translations[0].back().content.find(
              "Steps:\n1. Make a number named total") != std::string::npos);
  // The detail pass of the second paragraph knows what the first does.
  REQUIRE(model.requests[1].back().content.find("add the square of i") !=
          std::string::npos);
  const auto &repair = translations[2].back().content;
  REQUIRE(repair.find("Unknown function 'sqr'") != std::string::npos);
  REQUIRE(repair.find("Known functions: square") != std::string::npos);
  // The repair may only call functions that exist.
  const auto &repair_grammar = model.grammars[4];
  REQUIRE(repair_grammar.find("\"square\"") != std::string::npos);
  REQUIRE(repair_grammar.find("\"sqr\"") == std::string::npos);

  // The detail is written into the source, under the author's words.
  REQUIRE(compilation.detailed_source ==
          "Add up the squares of 1 to 10 and print it.\n"
          "    1. Make a number named total, starting at 0.\n"
          "    2. For every whole number i from 1 to 10, add the square of i "
          "to total.\n"
          "    3. Print total.\n"
          "    Assumption: \"It\" means the total.\n"
          "\n"
          "The square of a number is the number times itself.\n"
          "    1. Define a function square that takes a whole number x and "
          "gives back x\n"
          "       times x.\n");
  REQUIRE(lockfile.details.size() == 2);
  REQUIRE(lockfile.translations.size() == 2);
  REQUIRE(lockfile.model == "scripted");

  // Building the detailed source needs no model, and changes nothing.
  auto again = ovis::compile(compilation.detailed_source, lockfile, nullptr);
  REQUIRE(again.succeeded());
  REQUIRE(again.paragraphs[0].from_lockfile);
  REQUIRE(again.detailed_source == compilation.detailed_source);
  // Nor does building the original source again.
  REQUIRE(ovis::compile(source, lockfile, nullptr).detailed_source ==
          compilation.detailed_source);
}

TEST_CASE("Check steps the author edited.", "[compiler]") {
  ovis::english::lockfile lockfile;
  lockfile.details.push_back({"Greet the user.",
                              {.steps = {"Print \"Hello\"."},
                               .assumptions = {"The greeting is \"Hello\"."}}});
  scripted_model model;
  model.reviews = {"STATUS: ok\nASSUMPTIONS:\n- The user is a friend.\n"};
  model.translations = {"(print \"Hello, friend\")"};

  // The old assumption no longer fits the edited step; the review replaces
  // it.
  const auto edited = "Greet the user.\n    1. Print \"Hello, friend\".\n"
                      "    Assumption: The greeting is \"Hello\".\n"s;
  const auto compilation = ovis::compile(edited, lockfile, &model);
  INFO(compilation.report(true));
  REQUIRE(compilation.succeeded());
  REQUIRE(compilation.paragraphs[0].steps_from_source);
  REQUIRE(model.details.empty());
  REQUIRE(model.requests[0].back().content.find("1. Print \"Hello, friend\".") !=
          std::string::npos);
  REQUIRE(compilation.detailed_source ==
          "Greet the user.\n    1. Print \"Hello, friend\".\n"
          "    Assumption: The user is a friend.\n");
  REQUIRE(lockfile.details[0].detail.steps[0] == "Print \"Hello, friend\".");
  // Checked once: building again needs no model.
  REQUIRE(ovis::compile(compilation.detailed_source, lockfile, nullptr)
              .succeeded());
}

TEST_CASE("Reject edited steps that drop what the paragraph asks.",
          "[compiler]") {
  ovis::english::lockfile lockfile;
  lockfile.details.push_back(
      {"Read three numbers, sort them and print them.",
       {.steps = {"Read three whole numbers into numbers.",
                  "Sort numbers from smallest to largest.", "Print numbers."},
        .assumptions = {"Sorting is from smallest to largest."}}});
  scripted_model model;
  model.reviews = {
      "STATUS: mismatch\nREASON: The paragraph asks for the numbers to be "
      "sorted, but no step sorts them.\nSUGGESTION: Add a step that sorts "
      "numbers, or remove \"sort them\" from the paragraph.\n"};

  // The author deleted the key step but left its assumption behind.
  const auto source = "Read three numbers, sort them and print them.\n"
                      "    1. Read three whole numbers into numbers.\n"
                      "    2. Print numbers.\n"
                      "    Assumption: Sorting is from smallest to largest.\n"s;
  const auto compilation = ovis::compile(source, lockfile, &model);
  REQUIRE(!compilation.succeeded());
  REQUIRE(model.translation_requests().empty());
  const auto report = compilation.report();
  REQUIRE(report.find("has steps that no longer do what it says") !=
          std::string::npos);
  REQUIRE(report.find("2. Print numbers.") != std::string::npos);
  REQUIRE(report.find("Why: The paragraph asks for the numbers to be sorted") !=
          std::string::npos);
  REQUIRE(report.find("Try: Add a step that sorts") != std::string::npos);
  // The author's text is left for them to fix.
  REQUIRE(compilation.detailed_source == source);
  // The verdict holds without asking again until something changes.
  REQUIRE(ovis::compile(source, lockfile, nullptr).report() == report);
}

TEST_CASE("Remove steps whose sentence was deleted.", "[compiler]") {
  ovis::english::lockfile lockfile;
  lockfile.details.push_back(
      {"Say hello.", {.steps = {"Print \"Hello\"."}}});
  lockfile.details.push_back(
      {"Then sort the list.", {.steps = {"Sort the list.",
                                         "Print the sorted list."}}});
  lockfile.translations.push_back({"1. Print \"Hello\".", "(print \"Hello\")"});

  // The second sentence is gone, its (edited) steps are not.
  const auto source = "Say hello.\n    1. Print \"Hello\".\n\n"
                      "    1. Sort the list.\n"
                      "    2. Print the sorted list, largest first.\n"s;
  const auto compilation = ovis::compile(source, lockfile, nullptr);
  INFO(compilation.report(true));
  REQUIRE(compilation.succeeded());
  REQUIRE(compilation.detailed_source ==
          "Say hello.\n    1. Print \"Hello\".\n");
  REQUIRE(compilation.warnings.size() == 1);
  REQUIRE(compilation.warnings[0].find("was deleted, so these steps were "
                                       "removed too") != std::string::npos);
  REQUIRE(compilation.warnings[0].find("largest first") != std::string::npos);
  REQUIRE(run_program(compilation.program) == "Hello\n");
  REQUIRE(lockfile.details.size() == 1);
  REQUIRE(compilation.detailed_lines.front() == 1);

  // Steps Ovis never wrote are not removed: it asks instead.
  const auto unknown = ovis::compile("    1. Print \"Bye\".\n", lockfile,
                                     nullptr);
  REQUIRE(!unknown.succeeded());
  REQUIRE(unknown.report().find("no sentence above them") != std::string::npos);
}

TEST_CASE("Rewrite steps when their sentence changes.", "[compiler]") {
  ovis::english::lockfile lockfile;
  lockfile.details.push_back(
      {"Read three numbers, sort them and print them.",
       {.steps = {"Read three whole numbers into numbers.",
                  "Sort numbers from smallest to largest.", "Print numbers."},
        .assumptions = {"Sorting is from smallest to largest."}}});
  scripted_model model;
  model.details = {detail_reply("1. Read three whole numbers into numbers.\n"
                                "2. Print numbers, separated by commas.\n")};
  model.translations = {"(let numbers (list (read_int) (read_int) "
                        "(read_int)))\n(print numbers)"};

  // The author removed "sort them" from the sentence and edited a step, but
  // left the sorting step and its assumption.
  const auto compilation = ovis::compile(
      "Read three numbers and print them.\n"
      "    1. Read three whole numbers into numbers.\n"
      "    2. Sort numbers from smallest to largest.\n"
      "    3. Print numbers, separated by commas.\n"
      "    Assumption: Sorting is from smallest to largest.\n",
      lockfile, &model);
  INFO(compilation.report(true));
  REQUIRE(compilation.succeeded());
  REQUIRE(compilation.warnings.size() == 1);
  // The model sees the old steps and is told to drop what no longer fits.
  const auto &request = model.requests[0].back().content;
  REQUIRE(request.find("2. Sort numbers from smallest to largest.") !=
          std::string::npos);
  REQUIRE(request.find("drop every step and assumption") != std::string::npos);
  REQUIRE(compilation.detailed_source ==
          "Read three numbers and print them.\n"
          "    1. Read three whole numbers into numbers.\n"
          "    2. Print numbers, separated by commas.\n");
}

TEST_CASE("Reject paragraphs that are too vague or irrelevant.",
          "[compiler]") {
  scripted_model model;
  model.details = {
      "STATUS: note\n",
      "STATUS: too_vague\nREASON: It doesn't say what to analyse.\n"
      "SUGGESTION: Say which numbers to read and what to work out.\n",
      "STATUS: irrelevant\nREASON: It is about the weather.\n"
      "SUGGESTION: Remove this paragraph.\n",
  };
  ovis::english::lockfile lockfile;
  const auto source = "This program is a small helper.\n\n"
                      "Analyse the data properly.\n\n"
                      "It is sunny today.\n"s;
  const auto compilation = ovis::compile(source, lockfile, &model);
  REQUIRE(!compilation.succeeded());
  // Nothing is translated once a paragraph is rejected.
  REQUIRE(model.translation_requests().empty());
  const auto report = compilation.report();
  REQUIRE(report.find("line 3 is too vague") != std::string::npos);
  REQUIRE(report.find("Why: It doesn't say what to analyse.") !=
          std::string::npos);
  REQUIRE(report.find("Try: Say which numbers") != std::string::npos);
  REQUIRE(report.find("line 5 is not about what the program does") !=
          std::string::npos);
  REQUIRE(report.find("line 1") == std::string::npos);
  REQUIRE(compilation.detailed_source == source);

  // The verdicts are remembered, so the same text fails the same way.
  const auto again = ovis::compile(source, lockfile, nullptr);
  REQUIRE(again.report() == report);
}

TEST_CASE("Explain paragraphs that can't be made to work.", "[compiler]") {
  scripted_model model;
  model.details = {detail_reply("1. Print the value.\n")};
  model.translations = {"(print x)", "(print y)"};
  model.explanations = {
      "REASON: The paragraph never says which value to print.\n"
      "SUGGESTION: Print the number 42.\n"};
  ovis::english::lockfile lockfile;
  const auto compilation =
      ovis::compile("Print it.", lockfile, &model, {.max_attempts = 2});
  REQUIRE(!compilation.succeeded());
  const auto report = compilation.report();
  REQUIRE(report.find("could not be turned into code") != std::string::npos);
  REQUIRE(report.find("Why: The paragraph never says which value") !=
          std::string::npos);
  REQUIRE(report.find("Try: Print the number 42.") != std::string::npos);
  REQUIRE(report.find("Unknown variable") == std::string::npos);
  REQUIRE(compilation.report(true).find("Unknown variable 'y'") !=
          std::string::npos);
  REQUIRE(lockfile.translations.empty());
}

TEST_CASE("Report paragraphs missing from the lockfile.", "[compiler]") {
  ovis::english::lockfile lockfile;
  const auto compilation = ovis::compile("Say hi.", lockfile, nullptr);
  REQUIRE(!compilation.succeeded());
  REQUIRE(compilation.report().find("has not been detailed yet") !=
          std::string::npos);
}

TEST_CASE("Read and write detailed paragraphs.", "[english]") {
  const auto parsed = ovis::english::parse_detailed(
      "Work out the average\nof the scores.\n"
      "    1. Add up\n       the scores 1, 2 and\n       3. Then stop.\n"
      "    2) Divide by the count.\n"
      "    Assumption: whole numbers.\n"
      "    Compiler: `total` holds float values, because it is given\n"
      "        one later on.");
  REQUIRE(parsed.intent == "Work out the average\nof the scores.");
  REQUIRE(parsed.steps ==
          std::vector<std::string>{"Add up the scores 1, 2 and 3. Then stop.",
                                   "Divide by the count."});
  REQUIRE(parsed.assumptions == std::vector<std::string>{"whole numbers."});
  REQUIRE(ovis::english::render_detailed(parsed.intent, parsed.steps,
                                         parsed.assumptions,
                                         {"`total` holds float values."}) ==
          "Work out the average\nof the scores.\n"
          "    1. Add up the scores 1, 2 and 3. Then stop.\n"
          "    2. Divide by the count.\n"
          "    Assumption: whole numbers.\n"
          "    Compiler: `total` holds float values.");

  const auto detail = ovis::english::parse_detail(
      "STATUS: ok\nSTEPS:\n1. One.\n2. Two.\nASSUMPTIONS:\n- Three.\n");
  REQUIRE(detail.steps == std::vector<std::string>{"One.", "Two."});
  REQUIRE(detail.assumptions == std::vector<std::string>{"Three."});
  REQUIRE_THROWS_AS(ovis::english::parse_detail("I don't know."), ovis::error);
}

TEST_CASE("Generate working native code.", "[codegen]") {
  REQUIRE(run_core("(print 7 (/ 7 2) (div 7 2) (% -7 3) (- 4))") ==
          "7 3.5 3 -1 -4\n");
  REQUIRE(run_core("(print (and true false) (or false true) (not false) "
                   "(= \"a\" \"a\") (!= 1 1.0) (< 1 2.5))") ==
          "false true true true false true\n");
  REQUIRE(run_core("(print (sqrt 16) (pow 2 10) (floor 2.7) (abs -3) "
                   "(min 4 2 8) (max 1.5 3) (int 3.9) (float 2))") ==
          "4.0 1024.0 2.0 3 2 3.0 3 2.0\n");
  // `and` must not evaluate its second operand when the first is false.
  REQUIRE(run_core("(let xs (list 1))\n"
                   "(if (and (< 5 (len xs)) (= (get xs 5) 0)) "
                   "(then (print \"bad\")) (else (print \"safe\")))") ==
          "safe\n");
  REQUIRE(run_core("(let flags (array bool 2)) (put flags 1 true)\n"
                   "(let words (array str 1)) (append words \"x\")\n"
                   "(print flags words (len words))") ==
          "[false, true] [\"\", \"x\"] 2\n");
  REQUIRE(run_core("(let i 0)\n"
                   "(while true (set i (+ i 1)) (if (> i 3) (then (break))))\n"
                   "(for j 1 5 (if (= (% j 2) 0) (then (continue))) "
                   "(print j))\n(print i)") == "1\n3\n5\n4\n");
  REQUIRE(run_core("(fn fact ((n int)) int\n"
                   "  (if (<= n 1) (then (return 1)))\n"
                   "  (return (* n (fact (- n 1)))))\n"
                   "(fn greet ((name str)) void (print \"hi\" name))\n"
                   "(greet \"bob\") (print (fact 10))") ==
          "hi bob\n3628800\n");
  REQUIRE(run_core("(let a (read_int)) (let b (read_float)) (print (+ a b))",
                   "2 0.5") == "2.5\n");
}

TEST_CASE("Build the examples from their lockfiles.", "[examples]") {
  const std::filesystem::path examples = OVIS_EXAMPLES_DIR;
  const std::vector<std::tuple<std::string, std::string, std::string>> cases =
      {
          {"hello", "", "Hello, world!\n"},
          {"fizzbuzz", "",
           "1\n2\nFizz\n4\nBuzz\nFizz\n7\n8\nFizz\nBuzz\n11\nFizz\n13\n14\n"
           "FizzBuzz\n"},
          {"primes", "",
           "[2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47]\n"
           "Found 15 primes\n97 is prime\n"},
          {"statistics", "5 4 8 15 16 23",
           "Numbers: [4, 8, 15, 16, 23]\nSmallest: 4 Largest: 23\n"
           "Average: 13.2\n"},
          {"wordcount", "Hello world\\nThe quick brown fox\\n42 is the answer\\n",
           "Lines: 3, words: 10, letters: 37\nAverage word length: 3.7\n"},
          {"temperature", "",
           "21.5 C = 70.7 F\n23.0 C = 73.4 F\n19.8 C = 67.64 F\n"
           "25.0 C = 77.0 F\n30.1 C = 86.18 F\nHot days: 2\n"},
      };
  for (const auto &[name, input, expected] : cases) {
    INFO(name);
    const auto source = examples / (name + ".ovis");
    auto lockfile =
        ovis::english::lockfile::load(ovis::english::lockfile::path_for(source));
    const auto text = read_file(source);
    const auto compilation = ovis::compile(text, lockfile, nullptr);
    INFO(compilation.report(true));
    REQUIRE(compilation.succeeded());
    // The committed examples are fully detailed already.
    REQUIRE(compilation.detailed_source == text);
    REQUIRE(run_program(compilation.program, input) == expected);
  }
}

TEST_CASE("Infer what the translation left out.", "[elaborate]") {
  ovis::core::program program;
  const auto notes = elaborate_text(
      "(fn average (xs)\n"
      "  (let total 0)\n"
      "  (each x xs (set total (+ total x)))\n"
      "  (/ total (len xs)))\n"
      "(let readings (list))\n"
      "(append readings 2.5)\n"
      "(print (average readings))",
      program);
  const auto &average = program.units[0].functions[0];
  REQUIRE(average.parameters[0].type ==
          ovis::core::value_type::list_of(ovis::core::base_type::real));
  REQUIRE(average.return_type == ovis::core::value_type::real());
  // `total` starts as 0 but has floats added to it.
  REQUIRE(*average.body[0].declared_type == ovis::core::value_type::real());
  REQUIRE(mentions(notes, "gives back the value it works out last"));
  REQUIRE(mentions(notes, "`total` holds a decimal number"));
  REQUIRE(ovis::core::check(program).empty());
}

TEST_CASE("Fill in details deterministically.", "[elaborate]") {
  ovis::core::program program;
  const auto notes = elaborate_text(
      "(fn is_even (n) (= (% n 2) 0))\n"
      "(let counter 3)\n"
      "(print countr (isEven counter))\n"
      "(while counter (set counter (- counter 1)))\n"
      "(let seconds (time))\n"
      "(let things (list))",
      program);
  REQUIRE(mentions(notes, "Read `countr` as `counter`"));
  REQUIRE(mentions(notes, "Read `isEven` as `is_even`"));
  REQUIRE(mentions(notes, "is not zero"));
  REQUIRE(mentions(notes, "gets null"));
  REQUIRE(mentions(notes, "Nothing says what this list holds"));
  REQUIRE(mentions(notes, "`is_even` takes `n` as a whole number"));
  REQUIRE(ovis::core::check(program).empty());

  // The same input always gives the same result.
  ovis::core::program again;
  REQUIRE(elaborate_text(
              "(fn is_even (n) (= (% n 2) 0))\n(let counter 3)\n"
              "(print countr (isEven counter))\n"
              "(while counter (set counter (- counter 1)))\n"
              "(let seconds (time))\n(let things (list))",
              again) == notes);

  // Ambiguous names are left for the model to fix.
  REQUIRE(ovis::core::closest_name("cat", {"bat", "hat"}) == std::nullopt);
  REQUIRE(ovis::core::closest_name("totl", {"total", "count"}) == "total");
}

TEST_CASE("Work with text.", "[codegen]") {
  REQUIRE(run_core("(let name \"Ada\")\n"
                   "(print (+ \"Hi \" name \", you are \" 36 \"!\"))\n"
                   "(print (substring \"hello\" 1 3) (char_at \"A\" 0) "
                   "(chr 66) (to_str 2.5) (concat \"a\" \"b\"))") ==
          "Hi Ada, you are 36!\nell 65 B 2.5 ab\n");
  REQUIRE(run_core("(let count 0)\n"
                   "(let line (read_line))\n"
                   "(while (!= line null)\n"
                   "  (set count (+ count 1))\n"
                   "  (set line (read_line)))\n"
                   "(print count \"lines\")",
                   "one\\ntwo\\nthree\\n") == "3 lines\n");
  REQUIRE(run_core("(let xs (list 3 1 2)) (sort xs) (print xs) (reverse xs) "
                   "(print xs)\n(let words (list \"b\" \"a\")) (sort words) "
                   "(print words)") ==
          "[1, 2, 3]\n[3, 2, 1]\n[\"a\", \"b\"]\n");
}

TEST_CASE("Call the C standard library.", "[libc]") {
  REQUIRE(run_core("(printf \"%d|%5.2f|%s|%c|%x\\n\" 42 pi \"ok\" 65 255)") ==
          "42| 3.14|ok|A|ff\n");
  // Values are converted to what the format shows.
  REQUIRE(run_core("(printf \"%d %f\\n\" 2.9 3)") == "2 3.000000\n");
  REQUIRE(run_core("(print (round (sin 0)) (strlen \"four\") "
                   "(toupper (char_at \"q\" 0)) (atoi \"12\") "
                   "(strcmp \"a\" \"b\") (fmax 1 2))") ==
          "0.0 4 81 12 -1 2.0\n");

  const auto path = (std::filesystem::temp_directory_path() / "ovis-test.txt")
                        .string();
  REQUIRE(run_core("(let file (fopen \"" + path + "\" \"w\"))\n"
                   "(fprintf file \"%s %d\\n\" \"line\" 1)\n"
                   "(fclose file)\n"
                   "(let input (fopen \"" + path + "\" \"r\"))\n"
                   "(let text (buffer 100))\n"
                   "(if (!= (fgets text 100 input) null) "
                   "(then (printf \"read: %s\" text)))\n"
                   "(fclose input)") == "read: line 1\n");
  std::filesystem::remove(path);

  REQUIRE(run_core("(extern atoll (str) int)\n(extern llabs (int) int)\n"
                   "(link \"m\")\n(print (atoll \"9000000000\") (llabs -3))") ==
          "9000000000 3\n");
}

TEST_CASE("Reject the vague example with reasons.", "[examples]") {
  const std::filesystem::path source =
      std::filesystem::path(OVIS_EXAMPLES_DIR) / "vague.ovis";
  auto lockfile =
      ovis::english::lockfile::load(ovis::english::lockfile::path_for(source));
  const auto compilation = ovis::compile(read_file(source), lockfile, nullptr);
  REQUIRE(!compilation.succeeded());
  const auto report = compilation.report();
  REQUIRE(report.find("line 3 is too vague") != std::string::npos);
  REQUIRE(report.find("Try: List the stock") != std::string::npos);
  REQUIRE(report.find("line 5 is not about what the program does") !=
          std::string::npos);
}

TEST_CASE("Find weights embedded in an executable.", "[model]") {
  const auto directory = std::filesystem::temp_directory_path();
  const auto program = directory / "ovis-test-program-file";
  const auto weights = directory / "ovis-test-weights.gguf";
  const auto combined = directory / "ovis-test-combined";
  std::ofstream(program, std::ios::binary) << "pretend executable";
  std::ofstream(weights, std::ios::binary) << "pretend weights";

  const auto plain = ovis::english::find_builtin_model(std::nullopt, program);
  REQUIRE((!plain || !plain->is_embedded));
  ovis::english::embed_model(program, weights, combined);
  const auto found = ovis::english::find_builtin_model(std::nullopt, combined);
  REQUIRE(found);
  REQUIRE(found->is_embedded);
  REQUIRE(found->offset % 4096 == 0);
  REQUIRE(found->size == std::filesystem::file_size(weights));
  std::ifstream input(combined, std::ios::binary);
  input.seekg(static_cast<std::streamoff>(found->offset));
  std::string text(found->size, '\0');
  input.read(text.data(), static_cast<std::streamsize>(text.size()));
  REQUIRE(text == "pretend weights");

  // Embedding again replaces the weights instead of stacking them.
  ovis::english::embed_model(combined, weights, combined);
  REQUIRE(std::filesystem::file_size(combined) ==
          found->offset + found->size + 32);

  REQUIRE_THROWS_AS(
      ovis::english::find_builtin_model(directory / "missing.gguf", program),
      ovis::error);
  for (const auto &path : {program, weights, combined})
    std::filesystem::remove(path);
}

TEST_CASE("The built-in model repeats itself exactly.", "[model]") {
  const auto *const path = std::getenv("OVIS_TEST_MODEL");
  if (path == nullptr || !ovis::english::has_builtin_model_support())
    SKIP("Set OVIS_TEST_MODEL to a GGUF file to run this test.");

  const auto options = ovis::english::builtin_options{
      .location = *ovis::english::find_builtin_model(std::string(path), "."),
      .max_tokens = 48};
  const auto request = ovis::english::translation_request(
      "Print the numbers 1 to 3.", "1. Print 1, 2 and 3.", {});
  const std::string grammar(ovis::english::core_grammar());

  ovis::english::builtin_model first(options);
  const auto reply = first.chat(request, grammar);
  // A different request in between must not change the answer, even though
  // part of the prompt is reused.
  first.chat(ovis::english::detail_request("Say hi.", {}),
             ovis::english::detail_grammar());
  REQUIRE(first.chat(request, grammar) == reply);
  ovis::english::builtin_model second(options);
  REQUIRE(second.chat(request, grammar) == reply);
  REQUIRE(!first.fingerprint().empty());

  // The output follows the grammar: it can always be read.
  const auto restricted = ovis::english::restricted_core_grammar({});
  const auto repaired = second.chat(request, restricted);
  REQUIRE(!repaired.empty());
}

TEST_CASE("Keep tracking documents under 500 lines.", "[docs]") {
  // Inactive information belongs in docs/dream/ (see the dream skill).
  const std::filesystem::path root = OVIS_SOURCE_DIR;
  std::vector<std::filesystem::path> documents = {
      root / "CLAUDE.md", root / "docs" / "state.md",
      root / "docs" / "remaining-tasks.md",
      root / "docs" / "architecture-spec.md"};
  for (const auto &entry :
       std::filesystem::directory_iterator(root / "docs" / "dream"))
    if (entry.is_directory())
      documents.push_back(entry.path() / "INDEX.md");

  for (const auto &document : documents) {
    INFO(document.string());
    REQUIRE(std::filesystem::exists(document));
    const auto text = read_file(document);
    REQUIRE(std::ranges::count(text, '\n') < 500);

    // Relative links must point at files that exist.
    for (std::size_t position = text.find("]("); position != std::string::npos;
         position = text.find("](", position + 2)) {
      const auto end = text.find(')', position);
      const auto target = text.substr(position + 2, end - position - 2);
      if (target.starts_with("http") || target.starts_with("#"))
        continue;
      INFO(target);
      REQUIRE(std::filesystem::exists(document.parent_path() /
                                      target.substr(0, target.find('#'))));
    }
  }
}

TEST_CASE("Import a C library from its header.", "[c-import]") {
  if (!ovis::core::has_c_import_support())
    SKIP("Built without libclang.");
  const auto &library = ovis::core::import_c_library("ovistest.h", "ovistest",
                                                     test_c_imports());
  const auto find = [&](const std::string &p_name) {
    return std::ranges::find(library.functions, p_name,
                             &ovis::core::c_function::name);
  };
  using enum ovis::core::c_type;
  REQUIRE(find("ovt_add") != library.functions.end());
  REQUIRE(find("ovt_wrap")->result == uint8_type);
  REQUIRE(find("ovt_wrap")->parameters[0] == uint16_type);
  REQUIRE(find("ovt_answer")->pointees[0] == int_type);
  REQUIRE(find("ovt_counter_open")->pointees[1] == pointer_type);
  REQUIRE(find("ovt_greet")->result == string_type);
  REQUIRE(find("ovt_add")->comment == "Adds two numbers.");
  // A struct by value can't be passed yet: skipped, with the reason.
  REQUIRE(find("ovt_point_sum") == library.functions.end());
  REQUIRE(std::ranges::any_of(library.skipped, [](const auto &p_line) {
    return p_line.starts_with("ovt_point_sum");
  }));
  const auto constant = [&](const std::string &p_name) {
    return std::ranges::find(library.constants, p_name,
                             &ovis::core::c_constant::name);
  };
  REQUIRE(constant("OVT_ERROR")->integer == -1);
  REQUIRE(constant("OVT_SCALE")->real == 2.5);
  REQUIRE(constant("OVT_BLUE")->integer == 6);
  REQUIRE(library.link_flags.back() == "-lovistest");
  REQUIRE(!library.header_hash.empty());

  REQUIRE(run_core(
              "(use-c \"ovistest.h\" \"ovistest\")\n"
              "(print (ovt_add 2 3) (ovt_scale 2.0 1.5) (ovt_wrap 300) "
              "(ovt_negate 5) (ovt_big))\n"
              "(let answer 0)\n(ovt_answer (ref answer))\n"
              "(let half 0.0)\n(ovt_half 5 (ref half))\n"
              "(print answer half (ovt_greet \"Ada\"))\n"
              "(let counter null)\n"
              "(print (ovt_counter_open \"four\" (ref counter)) "
              "(ovt_counter_get counter))\n"
              "(ovt_counter_add counter 3)\n"
              "(print (ovt_counter_get counter) "
              "(ovt_counter_open \"bad\" (ref counter)) OVT_ERROR OVT_VERSION "
              "OVT_SCALE OVT_GREEN OVT_BLUE)\n"
              "(print (ovt_visit null 7) (ovt_next_color OVT_RED))") ==
          "5 3.0 44 -5 4000000000\n42 2.5 hello, Ada\n0 4\n"
          "7 -1 -1 3 2.5 5 6\n-7 5\n");

  // (ref x) is only for C functions, and must match what C writes.
  REQUIRE(check_text("(let x 0) (print (ref x))")[0].message.find(
              "only be passed to a C function") != std::string::npos);
}

TEST_CASE("Use real C libraries.", "[c-import]") {
  if (!ovis::core::has_c_import_support() ||
      !std::filesystem::exists("/usr/include/zlib.h") ||
      !std::filesystem::exists("/usr/include/sqlite3.h"))
    SKIP("Needs libclang and the zlib and SQLite development files.");

  // zlib: compress and uncompress text, sizes passed by (ref ...).
  REQUIRE(run_core(
              "(use-c \"zlib.h\" \"z\")\n"
              "(let text \"hello hello hello hello hello hello\")\n"
              "(let packed_size (compressBound (strlen text)))\n"
              "(let packed (buffer packed_size))\n"
              "(print (compress packed (ref packed_size) text (strlen text)) "
              "Z_OK)\n"
              "(let unpacked_size 100)\n"
              "(let unpacked (buffer 101))\n"
              "(uncompress unpacked (ref unpacked_size) packed packed_size)\n"
              "(print (< packed_size (strlen text)) unpacked_size unpacked)") ==
          "0 0\ntrue 35 hello hello hello hello hello hello\n");

  // SQLite: open an in-memory database, insert and query.
  REQUIRE(run_core(
              "(use-c \"sqlite3.h\" \"sqlite3\")\n"
              "(let db null)\n"
              "(print (= (sqlite3_open \":memory:\" (ref db)) SQLITE_OK))\n"
              "(sqlite3_exec db \"create table t (n int); insert into t "
              "values (20), (22);\" null null null)\n"
              "(let statement null)\n"
              "(sqlite3_prepare_v2 db \"select sum(n) from t\" -1 "
              "(ref statement) null)\n"
              "(if (= (sqlite3_step statement) SQLITE_ROW) "
              "(then (print \"sum\" (sqlite3_column_int statement 0))))\n"
              "(sqlite3_finalize statement)\n(sqlite3_close db)") ==
          "true\nsum 42\n");
}

namespace {

// A program whose units are (module, Ovis Core) pairs, resolved like the
// compiler does. Returns the problems, or runs it when there are none.
auto run_modules(const std::vector<std::pair<std::string, std::string>> &p_units,
                 std::vector<std::string> *p_problems = nullptr)
    -> std::string {
  ovis::core::program program;
  for (const auto &[module, core] : p_units) {
    program.units.push_back(ovis::core::parse_unit(core));
    program.units.back().module = module;
  }
  std::vector<std::string> problems;
  for (const auto &problem : ovis::core::resolve_modules(program))
    problems.push_back(problem.message);
  ovis::core::elaborate(program);
  for (const auto &problem : ovis::core::check(program))
    problems.push_back(problem.message);
  if (p_problems != nullptr) {
    *p_problems = problems;
    return "";
  }
  for (const auto &problem : problems)
    UNSCOPED_INFO(problem);
  REQUIRE(problems.empty());
  return run_program(program);
}

} // namespace

TEST_CASE("Resolve names across modules.", "[modules]") {
  const std::pair<std::string, std::string> square = {
      "geometry.square", "(fn area (side) (* side side))"};
  const std::pair<std::string, std::string> circle = {
      "geometry.circle", "(fn area (radius) (* 3 radius radius))"};
  const std::pair<std::string, std::string> rates = {
      "rates", "(let rate 2)\n(fn scaled (value) (* value rate))"};

  // A name only one module defines needs no qualification; types flow
  // across modules (`value` is a float because main passes one).
  REQUIRE(run_modules({rates, {"main", "(print (scaled 1.5) rate)"}}) ==
          "3.0 2\n");

  // Two modules define `area`: main must say which, by any tail of the
  // module's name.
  std::vector<std::string> problems;
  run_modules({square, circle, {"main", "(print (area 2))"}}, &problems);
  REQUIRE(!problems.empty());
  REQUIRE(problems[0].find("geometry.circle.area or geometry.square.area") !=
          std::string::npos);
  REQUIRE(run_modules({square, circle,
                       {"main", "(print (square.area 2) "
                                "(geometry.circle.area 1))"}}) == "4 3\n");

  // A module's own definition comes first, and locals shadow globals.
  REQUIRE(run_modules({rates,
                       {"main", "(let rate 10)\n"
                                "(fn twice (rate) (* rate 2))\n"
                                "(print rate (twice 4) (rates.rate))"}},
                      &problems) == "");
  REQUIRE(problems.empty() == false); // (rates.rate) is not a function
  REQUIRE(run_modules({rates, {"main", "(let rate 10)\n"
                                       "(fn twice (rate) (* rate 2))\n"
                                       "(print rate (twice 4) rates.rate)"}}) ==
          "10 8 2\n");
}

TEST_CASE("Helper modules only define things.", "[modules]") {
  std::vector<std::string> problems;
  run_modules({{"helpers", "(print \"hi\")"}, {"main", "(pass)"}}, &problems);
  REQUIRE(problems.size() == 1);
  REQUIRE(problems[0].find("helper file") != std::string::npos);

  run_modules({{"helpers", "(let n (read_int))"}, {"main", "(pass)"}},
              &problems);
  REQUIRE(problems.size() == 1);

  // Data computed from other data is a definition, and is ready before main
  // runs.
  REQUIRE(run_modules({{"data", "(let base 4)\n(let sizes (list base (* base "
                                "2)))\n(let label (+ \"size \" base))"},
                       {"main", "(print sizes label)"}}) ==
          "[4, 8] size 4\n");
}

TEST_CASE("Load and build a program made of several files.", "[modules]") {
  namespace fs = std::filesystem;
  const auto root = fs::temp_directory_path() / "ovis-test-program";
  fs::remove_all(root);
  fs::create_directories(root / "pricing");
  fs::create_directories(root / "tools");
  fs::create_directories(root / ".hidden");
  const auto write = [&](const fs::path &p_path, const std::string &p_text) {
    std::ofstream(root / p_path) << p_text;
  };
  write("main.ovis", "Main.\n");
  write("pricing/tax.ovis", "Tax.\n");
  write("a.ovis", "A.\n");
  write(".hidden/secret.ovis", "Hidden.\n");
  // A subfolder with its own main.ovis is a separate program.
  write("tools/main.ovis", "Another program.\n");
  write("tools/helper.ovis", "Its helper.\n");

  for (const auto &path : {root, root / "main.ovis"}) {
    const auto files = ovis::load_program(path);
    REQUIRE(files.size() == 3);
    REQUIRE(files[0].module == "a");
    REQUIRE(files[1].module == "pricing.tax");
    REQUIRE(files[1].name == "pricing/tax.ovis");
    REQUIRE(files[2].module == "main");
  }
  // Any other file is a program on its own.
  REQUIRE(ovis::load_program(root / "a.ovis").size() == 1);
  REQUIRE(ovis::load_program(root / "a.ovis")[0].module.empty());
  fs::remove(root / "main.ovis");
  REQUIRE_THROWS_AS(ovis::load_program(root), ovis::error);
  fs::remove_all(root);

  // The shop example: helper modules in a subfolder, one lockfile and one
  // detailed source per file, and nothing changes when built again.
  auto files =
      ovis::load_program(std::filesystem::path(OVIS_EXAMPLES_DIR) / "shop");
  REQUIRE(files.size() == 3);
  const auto compilation = ovis::compile(files, nullptr);
  INFO(compilation.report(true));
  REQUIRE(compilation.succeeded());
  for (std::size_t index = 0; index < files.size(); ++index)
    REQUIRE(compilation.detailed_sources[index] == files[index].text);
  REQUIRE(run_program(compilation.program) ==
          "After discount: 42.08\nWith tax: 45.44\n");
}

TEST_CASE("Tell the detail pass about helper files and C libraries.",
          "[modules]") {
  scripted_model model;
  model.details = {
      detail_reply("1. Use the C library z (header zlib.h).\n"),
      "STATUS: too_vague\nREASON: A helper file can't print.\n"
      "SUGGESTION: Move this to main.ovis.\n",
  };
  std::vector<ovis::source_file> files = {
      {.name = "notes.ovis", .module = "notes", .text = "Use zlib.\n\nPrint the zlib version.\n"},
      {.name = "main.ovis", .module = "main", .text = "Nothing yet.\n"}};
  model.details.push_back("STATUS: note\n");
  const auto compilation = ovis::compile(files, &model);
  REQUIRE(!compilation.succeeded());
  const auto &second = model.requests[1].back().content;
  REQUIRE(second.find("notes.ovis, a helper file") != std::string::npos);
  if (ovis::core::has_c_import_support() &&
      std::filesystem::exists("/usr/include/zlib.h"))
    REQUIRE(second.find("(c zlibVersion") != std::string::npos);
  REQUIRE(compilation.report().find("notes.ovis, paragraph at line 3") !=
          std::string::npos);
}

TEST_CASE("Translate a paragraph that uses a C library.", "[c-import]") {
  if (!ovis::core::has_c_import_support())
    SKIP("Built without libclang.");
  scripted_model model;
  model.details = {detail_reply(
      "1. Use the C library ovistest (header ovistest.h).\n"
      "2. Print what ovt_add gives back for 1 and 2.\n")};
  model.translations = {
      "(use-c \"ovistest.h\" \"ovistest\")\n(print (ovt_add 1 2))"};
  ovis::english::lockfile lockfile;
  const auto compilation =
      ovis::compile("Add 1 and 2 with the ovistest library.", lockfile, &model,
                    {.c_imports = test_c_imports()});
  INFO(compilation.report(true));
  REQUIRE(compilation.succeeded());
  // The translator was shown the library's matching function.
  REQUIRE(model.translation_requests()[0].back().content.find(
              "(c ovt_add (cint cint) cint)  ; Adds two numbers.") !=
          std::string::npos);
  REQUIRE(run_program(compilation.program) == "3\n");

  // The header is recorded; a different one is noticed on the next build.
  REQUIRE(lockfile.c_headers.contains("ovistest.h"));
  lockfile.c_headers["ovistest.h"] = "an older version";
  const auto again = ovis::compile(compilation.detailed_source, lockfile,
                                   nullptr, {.c_imports = test_c_imports()});
  REQUIRE(again.succeeded());
  REQUIRE(again.warnings.size() == 1);
  REQUIRE(again.warnings[0].find("ovistest.h changed") != std::string::npos);
}
