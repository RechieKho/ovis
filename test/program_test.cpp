#include <filesystem>
#include <fstream>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <ovis/core/c_import.hpp>
#include <ovis/phrase/front_end.hpp>

#include "support.hpp"

using Catch::Matchers::ContainsSubstring;

namespace {

// A program folder in the temporary directory, removed afterwards.
class program_folder {
public:
  std::filesystem::path root;

  program_folder()
      : root(std::filesystem::temp_directory_path() / "ovis-phrase-program") {
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
  }
  ~program_folder() { std::filesystem::remove_all(root); }

  auto write(const std::string &p_name, const std::string &p_text) -> void {
    const auto path = root / p_name;
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path) << p_text;
  }

  auto compile() const -> ovis::phrase::program_result {
    return ovis::phrase::compile_phrase_program(
        ovis::phrase::load_phrase_program(root), nullptr,
        {.c_imports = {{OVIS_TEST_CLIB_INCLUDE}, {OVIS_TEST_CLIB_LIBRARY}}});
  }
};

auto problems_of(const ovis::phrase::program_result &p_result) -> std::string {
  std::string text;
  for (const auto &problem : p_result.problems)
    text += problem + "\n";
  return text;
}

} // namespace

TEST_CASE("Read a program of several files.", "[phrase][program]") {
  program_folder folder;
  folder.write("pricing/tax.ovis", "tax_rate = 0.08\n"
                                   "to add tax to (amount):\n"
                                   "    give back amount * (1 + tax_rate)\n");
  folder.write("pricing/discount.ovis",
               "to apply the discount to (total):\n"
               "    if total >= 40:\n"
               "        give back total * 0.9\n"
               "    give back total\n");
  // main.ovis has its own `total`, apart from the helper's parameter, and
  // its own `tax_rate` once it sets one.
  folder.write("main.ovis", "total = 0.0\n"
                            "for each price in [12.5, 4.0, 30.25]:\n"
                            "    total = total + price\n"
                            "discounted = apply the discount to total\n"
                            "printf(\"%.2f %.2f\\n\", discounted, add tax to discounted)\n"
                            "show tax_rate\n");
  const auto files = ovis::phrase::load_phrase_program(folder.root);
  REQUIRE(files.size() == 3);
  REQUIRE(files[0].name == "pricing/discount.ovis");
  REQUIRE(files[0].module == "pricing.discount");
  REQUIRE(files[2].module == "main");
  auto result = folder.compile();
  INFO(problems_of(result));
  REQUIRE(result.succeeded());
  REQUIRE(run_program(result.program) == "42.08 45.44\n0.08\n");

  // Helpers only define.
  folder.write("pricing/tax.ovis", "tax_rate = 0.08\n"
                                   "show tax_rate\n"
                                   "to add tax to (amount):\n"
                                   "    give back amount\n");
  REQUIRE_THAT(problems_of(folder.compile()),
               ContainsSubstring("pricing/tax.ovis, line 2: This is in "
                                 "pricing.tax, a helper file"));
  // One phrase, one file.
  folder.write("pricing/tax.ovis",
               "to apply the discount to (x):\n    give back x\n"
               "to add tax to (amount):\n    give back amount\n");
  REQUIRE_THAT(problems_of(folder.compile()),
               ContainsSubstring("pricing/tax.ovis, line 1: A phrase called "
                                 "'apply_discount_to' is already defined in "
                                 "pricing/discount.ovis on line 1"));
}

TEST_CASE("Use a C library's functions and constants.", "[phrase][program]") {
  if (!ovis::core::has_c_import_support())
    SKIP("Built without libclang.");
  program_folder folder;
  folder.write("main.ovis",
               "show ovt_add(OVT_VERSION, 2)\n"
               "use the C library \"ovistest.h\" from \"ovistest\"\n"
               "answer = 0\n"
               "ovt_answer(ref answer)\n"
               "show answer, ovt_greet(\"Ann\")\n");
  const auto result = folder.compile();
  INFO(problems_of(result));
  REQUIRE(result.succeeded());
  REQUIRE(result.locks.back().c_headers.contains("ovistest.h"));
  REQUIRE(run_program(result.program) == "5\n42 hello, Ann\n");

  // Its structs are records: fields in words, `a new …`, set and read.
  folder.write("main.ovis",
               "use the C library \"ovistest.h\" from \"ovistest\"\n"
               "p = ovt_point_new(3, 4)\n"
               "show p's x, y of p, ovt_point_area(p)\n"
               "x of p = 6\n"
               "show ovt_point_area(p)\n"
               "free p\n"
               "box = a new ovt_box\n"
               "box's width = 2\n"
               "box's label = \"crate\"\n"
               "ovt_box_grow(box, 1)\n"
               "show box's width, box's label, box's corner\n"
               "free box\n");
  const auto structs = folder.compile();
  INFO(problems_of(structs));
  REQUIRE(structs.succeeded());
  REQUIRE(run_program(structs.program) ==
          "3 4 12\n24\n3 crate an ovt point with x -1, y 0\n");

  folder.write("main.ovis", "use \"nowhere.h\"\n");
  REQUIRE_THAT(problems_of(folder.compile()),
               ContainsSubstring("main.ovis, line 1:"));
}

TEST_CASE("A program's phrases/ folder adds phrases without rebuilding ovis.",
          "[phrase][program]") {
  program_folder folder;
  folder.write("main.ovis", "show triple 5\nshow the cube of 3\n");
  folder.write("phrases/team.phr",
               "# The team's phrases.\n"
               "expr  triple {x: num}  : num => (* $x 3)\n"
               "expr  the cube of {x: num}  : num => (team_cube $x)\n");
  folder.write("phrases/team.core",
               "(fn team_cube ((x int)) int (return (* x (* x x))))\n");
  auto result = folder.compile();
  INFO(problems_of(result));
  REQUIRE(result.succeeded());
  REQUIRE(run_program(result.program) == "15\n27\n");
  // The files' hashes are in the lockfile; an edit warns on the next build.
  REQUIRE(result.locks.back().phrase_files.size() == 2);
  REQUIRE(result.locks.back().phrase_files.contains("phrases/team.phr"));
  folder.write("phrases/team.phr",
               "expr  triple {x: num}  : num => (* $x 4)\n"
               "expr  the cube of {x: num}  : num => (team_cube $x)\n");
  auto files = ovis::phrase::load_phrase_program(folder.root);
  files.back().lock = result.locks.back();
  const auto changed = ovis::phrase::compile_phrase_program(
      files, nullptr,
      {.c_imports = {{OVIS_TEST_CLIB_INCLUDE}, {OVIS_TEST_CLIB_LIBRARY}}});
  REQUIRE(changed.succeeded());
  REQUIRE(changed.warnings.size() == 1);
  REQUIRE_THAT(changed.warnings.front(),
               ContainsSubstring("phrases/team.phr changed since the last build"));

  // A mistake is reported at its file and line.
  folder.write("phrases/team.phr",
               "expr  triple {x: num}  : num => (* $x 3)\n"
               "expr  double {x: num} => (* $x 2)\n");
  REQUIRE_THAT(problems_of(folder.compile()),
               ContainsSubstring("phrases/team.phr:2:"));
}

TEST_CASE("A phrase file can't word a phrase as another is.", "[phrase][program]") {
  program_folder folder;
  folder.write("main.ovis", "show 1\n");
  folder.write("phrases/team.phr", "expr  square root of {x: num}  : float => (sqrt (float $x))\n");
  const auto problems = problems_of(folder.compile());
  REQUIRE_THAT(problems, ContainsSubstring("phrases/team.phr:1:"));
  REQUIRE_THAT(problems, ContainsSubstring("is already a phrase, in numbers.phr"));
}
