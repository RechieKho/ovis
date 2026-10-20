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

TEST_CASE("Lists keep their numbers at their size.", "[numbers][lists]") {
  CHECK(run_core("(let xs (list (as i16 300) -20 7))\n"
                 "(append xs 1000)\n"
                 "(print xs (get xs 0) (len xs))\n"
                 "(sort xs)\n"
                 "(print xs)\n"
                 "(let fs (list (as f32 2.5) 0.5))\n"
                 "(sort fs)\n"
                 "(print fs (get fs 1))\n"
                 "(let total 0)\n"
                 "(each x xs (set total (+ total x)))\n"
                 "(print total)\n") ==
        "[300, -20, 7, 1000] 300 4\n"
        "[-20, 7, 300, 1000]\n"
        "[0.5, 2.5] 2.5\n"
        "1287\n");
  // An item written in the program must fit.
  CHECK(first_problem("(let xs (list (as u8 1)))\n(append xs 256)\n") ==
        "256 doesn't fit in a byte (0 to 255).");
}

TEST_CASE("A phrase for a list of whole numbers takes a list of a size.",
          "[numbers][lists]") {
  // The function is copied for the list's items: `total__i16`.
  CHECK(run_core("(fn total ((xs int[])) int\n"
                 "  (let sum 0)\n"
                 "  (each x xs (set sum (+ sum x)))\n"
                 "  (return sum))\n"
                 "(let small (list (as i8 100) 100 100))\n"
                 "(let wide (list (as u32 4000000000) 1))\n"
                 "(print (total small) (total wide) (total (list 1 2)))\n") ==
        "300 4000000001 3\n");
}

TEST_CASE("Maps hold values of a size.", "[numbers][maps]") {
  CHECK(run_core("(let m (map-new str u8))\n"
                 "(map-put m \"a\" 200)\n"
                 "(let f (map-new int f32))\n"
                 "(map-put f 1 0.1)\n"
                 "(print m (+ (map-get m \"a\") 55) f)\n") ==
        "{\"a\": 200} 255 {1: 0.1}\n");
}

TEST_CASE("Records keep fields at their size; packed ones have no gaps.",
          "[numbers][records]") {
  CHECK(run_core("(record pixel (red u8) (green u8) (blue u8) (alpha u8))\n"
                 "(packed-record header (magic u16) (size u32) (offset u32))\n"
                 "(record spaced (magic u16) (size u32))\n"
                 "(let h (new header (magic 0x4D42) (size 70) (offset 54)))\n"
                 "(let p (new pixel (red 255) (green 0) (blue 10) (alpha 255)))\n"
                 "(print p (size-of (record pixel)))\n"
                 "(print h (size-of (record header)) (size-of (record spaced)))\n"
                 "(print (load u16 (cast ptr h)) (load u32 (offset (cast ptr h) 2 u8)))\n") ==
        "a pixel with red 255, green 0, blue 10, alpha 255 16\n"
        "a header with magic 19778, size 70, offset 54 10 16\n"
        "19778 70\n");
  CHECK(first_problem("(packed-record header (magic u16))\n"
                      "(trait named (name (it) str))\n"
                      "(impl named header (fn name (it) \"h\"))\n")
            .starts_with("'header' is packed"));
}

TEST_CASE("Arithmetic wraps or is clamped where it says so.", "[numbers]") {
  // FNV-1a's 32-bit multiply wraps; clamped arithmetic stops at the ends.
  CHECK(run_core("(let h (as u32 2166136261))\n"
                 "(set h (wrapping (* (bit-xor h 97) 16777619)))\n"
                 "(let b (as u8 200))\n"
                 "(let small (as i8 -100))\n"
                 "(print h (wrapping (+ b 100)) (saturating (+ b 100))\n"
                 "       (saturating (- small 100)) (saturating (* small 2))\n"
                 "       (wrapping (- (as u8 0) 1)))\n") ==
        "3826002220 44 255 -128 -128 255\n");
  CHECK(first_problem("(print (wrapping 1.5))\n")
            .starts_with("Only whole numbers wrap"));
}

TEST_CASE("A decimal's bits, and a number's bytes.", "[numbers][bits]") {
  CHECK(run_core("(print (to-hex (float-bits 1.0)) (to-hex (float-bits (as f32 1.0)))\n"
                 "       (bits-float 0x3FF8000000000000) (bits-float (as u32 0x40490FDB)))\n"
                 "(let bytes (to-bytes (as u32 0x11223344)))\n"
                 "(print bytes (to-bytes-big (as u16 0x1234)) (to-bytes (as i8 -1)))\n"
                 "(print (to-hex (from-bytes u32 bytes 0)) (from-bytes-big u16 bytes 1)\n"
                 "       (from-bytes i8 (to-bytes (as i8 -5)) 0))\n"
                 "(print \"a\")\n"
                 "(print (from-bytes u32 bytes 1))\n") ==
        "3FF0000000000000 3F800000 1.5 3.1415927\n"
        "[68, 51, 34, 17] [18, 52] [255]\n"
        "11223344 13090 -5\n"
        "a\n");
}
