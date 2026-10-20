#include <catch2/catch_test_macros.hpp>

#include "support.hpp"

// Numbers of a size and their bits in Ovis Core
// (docs/proposals/numbers.md): `i8`…`u64`, `f32`, conversions, and the
// operations on bits.

using test_support::check_text;
using test_support::run_core;

namespace {

// The first problem's message, or "".
auto first_problem(const std::string &p_core) -> std::string {
  const auto problems = check_text(p_core);
  return problems.empty() ? "" : problems.front().message;
}

} // namespace

TEST_CASE("Whole numbers are written in hex, binary, octal and with _.",
          "[numbers]") {
  CHECK(run_core("(print 0x1F 0b1010 0o17 1_000_000 -0x10)\n") ==
        "31 10 15 1000000 -16\n");
  const auto number = ovis::core::read_whole_number("0xFFFF_FFFF_FFFF_FFFF");
  REQUIRE(number);
  CHECK(number->is_big);
  CHECK(!ovis::core::read_whole_number("1__0"));
  CHECK(!ovis::core::read_whole_number("_1"));
  CHECK(!ovis::core::read_whole_number("0x1_0000_0000_0000_0000"));
  CHECK(!ovis::core::read_whole_number("0b102"));
}

TEST_CASE("Numbers of a size are kept and worked out at their size.",
          "[numbers]") {
  CHECK(run_core("(let b (as u8 200))\n"
                 "(let c (+ b 55))\n"
                 "(print b c (as i8 -128) (as u64 0xFFFFFFFFFFFFFFFF))\n"
                 "(let h (as i16 1000))\n"
                 "(print (* h 3) (div h 7) (% h 7) (/ h 8))\n"
                 "(let f (as f32 0.1))\n"
                 "(print f (* f 3) (+ f 0.5) (as f32 2))\n"
                 "(print (+ (as i8 1) (as i32 2)) (+ (as u32 1) (as i32 2)))\n") ==
        "200 255 -128 18446744073709551615\n"
        "3000 142 6 125.0\n"
        "0.1 0.3 0.6 2.0\n"
        "3 3\n");
}

TEST_CASE("Arithmetic that doesn't fit its size stops the program.",
          "[numbers]") {
  // A runtime error ends the program: what comes after isn't printed.
  CHECK(run_core("(let b (as u8 200))\n(print \"a\")\n(print (+ b 100))\n"
                 "(print \"b\")\n") == "a\n");
  CHECK(run_core("(let big 300)\n(print \"a\")\n(let b u8 0)\n"
                 "(set b big)\n(print \"b\")\n") == "a\n");
}

TEST_CASE("A number written in the program must fit where it goes.",
          "[numbers]") {
  CHECK(first_problem("(let b (as u8 1))\n(print (+ b 300))\n") ==
        "300 doesn't fit in a byte (0 to 255).");
  CHECK(first_problem("(print (as i8 200))\n") ==
        "200 doesn't fit in an int8 (-128 to 127).");
  CHECK(first_problem("(let x (as u64 1))\n(let y 2)\n(print (+ x y))\n")
            .starts_with("'+' can't mix a uint64 and a whole number"));
}

TEST_CASE("Conversions check, wrap or clamp.", "[numbers]") {
  CHECK(run_core("(let big 300)\n"
                 "(print (wrap-to u8 big) (clamp-to u8 big) (clamp-to u8 -4)\n"
                 "       (wrap-to i8 200) (clamp-to i16 1e9) (as int 2.9))\n") ==
        "44 255 0 -56 32767 2\n");
}

TEST_CASE("Bits of whole numbers.", "[numbers][bits]") {
  CHECK(run_core("(let x 0b1100)\n"
                 "(print (bit-and x 0b1010) (bit-or x 1) (bit-xor x 0b0110)\n"
                 "       (bit-not 0) (shift-left 1 10) (shift-right -16 2))\n"
                 "(print (bit-test x 2) (bit-test x 0) (bit-set x 0)\n"
                 "       (bit-clear x 3) (bit-flip x 2) (bit-field 0xABCD 4 11))\n"
                 "(print (count-ones 255) (leading-zeros 1) (trailing-zeros 8))\n"
                 "(let b (as u8 0x81))\n"
                 "(print (rotate-left b 1) (shift-right b 1) (bit-not b)\n"
                 "       (byte-swap (as u16 0x1234)) (leading-zeros b))\n"
                 "(print (to-hex 255) (to-hex b) (to-binary b) (to-hex 10 4)\n"
                 "       (to-hex -1) (from-hex \"0xff\"))\n") ==
        "8 13 10 -1 1024 -4\n"
        "true false 13 4 8 188\n"
        "8 63 3\n"
        "3 64 126 13330 0\n"
        "FF 81 10000001 000A FFFFFFFFFFFFFFFF 255\n");
}

TEST_CASE("A bit outside the number stops the program.", "[numbers][bits]") {
  CHECK(run_core("(let n 8)\n(print \"a\")\n(print (shift-left (as u8 1) n))\n") ==
        "a\n");
}
