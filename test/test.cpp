#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>

#include <catch2/catch_test_macros.hpp>
#include <llvm/IR/LLVMContext.h>

#include <ovis/codegen/llvm_codegen.hpp>
#include <ovis/core/checker.hpp>
#include <ovis/core/elaborate.hpp>
#include <ovis/core/modules.hpp>
#include <ovis/core/c_import.hpp>
#include <ovis/english/language_model.hpp>

#include "support.hpp"

using namespace std::literals::string_literals;

namespace {

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

  // Reals are read with strtod, which accepts more spellings than a number.
  const auto reals =
      ovis::core::read_sexprs("(.5 1e3 2.5E-1 inf nan +1.5 0x1p3 1e 1e999)");
  REQUIRE(reals[0].items[0].real == 0.5);
  REQUIRE(reals[0].items[1].real == 1000.0);
  REQUIRE(reals[0].items[2].real == 0.25);
  for (std::size_t index = 3; index < reals[0].items.size(); ++index)
    REQUIRE(reals[0].items[index].kind == kind::symbol);

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

  // A copied declaration is skipped; statements nested in a let follow it.
  const auto copied = ovis::core::parse_unit(
      "(fn square ((n int)) int)\n"
      "(let i 1 (while (< i 6) (print i) (set i (+ i 1))))");
  REQUIRE(copied.functions.empty());
  REQUIRE(copied.statements.size() == 2);
  REQUIRE(copied.statements[1].kind == kind::while_);

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
  REQUIRE(unknown[0].message.find("total (a whole number)") != std::string::npos);

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

TEST_CASE("Infer what Ovis Core leaves out.", "[elaborate]") {
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

TEST_CASE("Prefer the larger default model where a GPU runs it.", "[model]") {
  const auto directory =
      std::filesystem::temp_directory_path() / "ovis-test-defaults";
  std::filesystem::create_directories(directory);
  const auto program = directory / "ovis";
  // No model from this machine's own cache.
  const auto *const cache = std::getenv("XDG_CACHE_HOME");
  const std::optional<std::string> saved =
      cache != nullptr ? std::optional<std::string>(cache) : std::nullopt;
  ::setenv("XDG_CACHE_HOME", (directory / "cache").c_str(), 1);
  const auto *const model_path = std::getenv("OVIS_MODEL_PATH");
  const std::optional<std::string> saved_path =
      model_path != nullptr ? std::optional<std::string>(model_path)
                            : std::nullopt;
  ::unsetenv("OVIS_MODEL_PATH");
  const auto &small = ovis::english::default_model_for(false);
  const auto &large = ovis::english::default_model_for(true);
  REQUIRE(small.size == "3b");
  REQUIRE(large.size == "7b");
  std::ofstream(directory / large.file_name) << "pretend 7B";
  // Only the larger one: used either way.
  REQUIRE(ovis::english::find_builtin_model(std::nullopt, program, false)->file ==
          directory / large.file_name);
  std::ofstream(directory / small.file_name) << "pretend 3B";
  REQUIRE(ovis::english::find_builtin_model(std::nullopt, program, true)->file ==
          directory / large.file_name);
  REQUIRE(ovis::english::find_builtin_model(std::nullopt, program, false)->file ==
          directory / small.file_name);
  std::filesystem::remove_all(directory);
  if (saved)
    ::setenv("XDG_CACHE_HOME", saved->c_str(), 1);
  else
    ::unsetenv("XDG_CACHE_HOME");
  if (saved_path)
    ::setenv("OVIS_MODEL_PATH", saved_path->c_str(), 1);
}

TEST_CASE("The built-in model repeats itself exactly.", "[model]") {
  const auto *const path = std::getenv("OVIS_TEST_MODEL");
  if (path == nullptr || !ovis::english::has_builtin_model_support())
    SKIP("Set OVIS_TEST_MODEL to a GGUF file to run this test.");

  const auto options = ovis::english::builtin_options{
      .location = *ovis::english::find_builtin_model(std::string(path), "."),
      .max_tokens = 48};
  const std::vector<ovis::english::chat_message> request = {
      {"system", "You answer in a few lower case words."},
      {"user", "Name three colours."}};
  const std::string grammar = "root ::= [a-z ]+\n";

  ovis::english::builtin_model first(options);
  const auto reply = first.chat(request, grammar);
  // A different request in between must not change the answer, even though
  // part of the prompt is reused.
  first.chat({{"system", "You answer in a few lower case words."},
              {"user", "Say hi."}},
             ovis::english::choice_grammar(3));
  REQUIRE(first.chat(request, grammar) == reply);
  ovis::english::builtin_model second(options);
  REQUIRE(second.chat(request, grammar) == reply);
  REQUIRE(!first.fingerprint().empty());

  // The output follows the grammar.
  REQUIRE(!reply.empty());
  REQUIRE(reply.find_first_not_of("abcdefghijklmnopqrstuvwxyz ") ==
          std::string::npos);
}

TEST_CASE("Keep tracking documents under 500 lines.", "[docs]") {
  // Inactive information belongs in docs/dream/ (see the dream skill).
  const std::filesystem::path root = OVIS_SOURCE_DIR;
  std::vector<std::filesystem::path> documents = {
      root / "CLAUDE.md", root / "docs" / "state.md",
      root / "docs" / "remaining-tasks.md",
      root / "docs" / "architecture-spec.md",
      root / "docs" / "model-backends.md", root / "docs" / "c-libraries.md",
      root / "docs" / "ovis-core.md"};
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
              "Only a C function can be given a variable with ref") != std::string::npos);
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

