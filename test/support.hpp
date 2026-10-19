#ifndef OVIS_TEST_SUPPORT_HPP
#define OVIS_TEST_SUPPORT_HPP

#include <filesystem>
#include <string>
#include <vector>

#include <ovis/core/ast.hpp>
#include <ovis/core/c_import.hpp>
#include <ovis/core/checker.hpp>

// Compiles the program to a native executable and returns what it prints,
// with `p_input` on its standard input.
auto run_program(const ovis::core::program &p_program,
                 const std::string &p_input = "") -> std::string;

// Helpers shared by the Ovis Core tests.
namespace test_support {

// The problems found in `p_core`, after elaboration.
auto check_text(const std::string &p_core)
    -> std::vector<ovis::core::diagnostic>;

// The elaborator's notes, one string each.
auto elaborate_text(const std::string &p_core, ovis::core::program &p_program)
    -> std::vector<std::string>;

auto mentions(const std::vector<std::string> &p_notes,
              const std::string &p_text) -> bool;

auto read_file(const std::filesystem::path &p_path) -> std::string;

// Where the tests' own C library is.
auto test_c_imports() -> ovis::core::c_import_options;

// Reads Ovis Core, requires it to check, runs it and returns what it prints.
auto run_core(const std::string &p_core, const std::string &p_input = "")
    -> std::string;

} // namespace test_support

#endif // OVIS_TEST_SUPPORT_HPP
