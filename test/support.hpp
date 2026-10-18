#ifndef OVIS_TEST_SUPPORT_HPP
#define OVIS_TEST_SUPPORT_HPP

#include <string>

#include <ovis/core/ast.hpp>

// Compiles the program to a native executable and returns what it prints,
// with `p_input` on its standard input.
auto run_program(const ovis::core::program &p_program,
                 const std::string &p_input = "") -> std::string;

#endif // OVIS_TEST_SUPPORT_HPP
