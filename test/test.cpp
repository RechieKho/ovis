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
#include <ovis/english/prompt.hpp>

using namespace std::literals::string_literals;

namespace {

// Replies with canned answers in order and remembers what it was asked.
class scripted_model final : public ovis::english::language_model {
public:
  std::deque<std::string> replies;
  std::vector<std::vector<ovis::english::chat_message>> requests;

  auto name() const -> std::string override { return "scripted"; }
  auto chat(const std::vector<ovis::english::chat_message> &p_messages)
      -> std::string override {
    requests.push_back(p_messages);
    REQUIRE(!replies.empty());
    auto reply = replies.front();
    replies.pop_front();
    return reply;
  }
};

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
  ovis::codegen::build_executable(*module, executable);

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

auto run_core(const std::string &p_core, const std::string &p_input = "")
    -> std::string {
  ovis::core::program program;
  program.units.push_back(ovis::core::parse_unit(p_core));
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

TEST_CASE("Translate with a model and repair its mistakes.", "[compiler]") {
  scripted_model model;
  model.replies = {
      // The first paragraph uses a function that the second one defines,
      // and misspells a variable.
      "(let total 0)\n(for i 1 10 (set total (+ total (sqr i))))\n"
      "(print totl)",
      "(fn square ((x int)) int (return (* x x)))",
      "(let total 0)\n(for i 1 10 (set total (+ total (square i))))\n"
      "(print total)",
  };
  ovis::english::lockfile lockfile;
  const auto source = "Add up the squares of 1 to 10 and print it.\n\n"
                      "The square of a number is the number times itself.\n"s;
  auto compilation = ovis::compile(source, lockfile, &model);

  REQUIRE(compilation.succeeded());
  REQUIRE(model.requests.size() == 3);
  const auto &repair = model.requests[2].back().content;
  REQUIRE(repair.find("Unknown function 'sqr'") != std::string::npos);
  REQUIRE(repair.find("Known functions: square") != std::string::npos);
  REQUIRE(lockfile.entries.size() == 2);
  REQUIRE(lockfile.model == "scripted");
  REQUIRE(run_program(compilation.program) == "385\n");

  // A second build needs no model at all.
  auto again = ovis::compile(source, lockfile, nullptr);
  REQUIRE(again.succeeded());
  REQUIRE(again.paragraphs[0].from_lockfile);
}

TEST_CASE("Give up after the allowed attempts.", "[compiler]") {
  scripted_model model;
  model.replies = {"(print x)", "(print y)"};
  ovis::english::lockfile lockfile;
  const auto compilation = ovis::compile("Print it.", lockfile, &model,
                                         {.max_attempts = 2});
  REQUIRE(!compilation.succeeded());
  REQUIRE(compilation.report().find("Unknown variable 'y'") !=
          std::string::npos);
  REQUIRE(lockfile.entries.empty());
}

TEST_CASE("Report paragraphs missing from the lockfile.", "[compiler]") {
  ovis::english::lockfile lockfile;
  const auto compilation = ovis::compile("Say hi.", lockfile, nullptr);
  REQUIRE(!compilation.succeeded());
  REQUIRE(compilation.report().find("has no translation") !=
          std::string::npos);
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
    const auto compilation = ovis::compile(read_file(source), lockfile, nullptr);
    INFO(compilation.report());
    REQUIRE(compilation.succeeded());
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
  REQUIRE(mentions(notes, "returns the value of its last line"));
  REQUIRE(mentions(notes, "`total` holds float"));
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
  REQUIRE(mentions(notes, "parameter `n` of `is_even` is int"));
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
