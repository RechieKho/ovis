#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "support.hpp"

// Typed pointers in Ovis Core (proposals/references.md §3, §5, §6, step
// R1): `ref` of variables, fields and items, `load store offset cast
// size-of`, C number types.

using Catch::Matchers::ContainsSubstring;
using test_support::run_core;

namespace {

auto problems(const std::string &p_core) -> std::string {
  std::string text;
  for (const auto &problem : test_support::check_text(p_core))
    text += problem.message + "\n";
  return text;
}

} // namespace

TEST_CASE("Read and write through a pointer.", "[pointers]") {
  // A variable's address changes it; a phrase changes its caller's number.
  CHECK(run_core("(fn bump ((n (ptr int)) (k int))\n"
                 "  (store int n (+ (load int n) k)))\n"
                 "(let score 10)\n"
                 "(bump (ref score) 5)\n"
                 "(print score)\n") == "15\n");
  // A field's and a list item's.
  CHECK(run_core("(record point (x int) (y float))\n"
                 "(let p (new point (x 1) (y 2.5)))\n"
                 "(let px (ref (field p x)))\n"
                 "(store int px 7)\n"
                 "(let xs (list 1 2 3))\n"
                 "(let second (ref (get xs 1)))\n"
                 "(store int second 20)\n"
                 "(print (field p x) xs (load float (ref (field p y))))\n") ==
        "7 [1, 20, 3] 2.5\n");
}

TEST_CASE("C number types: sizes, offsets and conversions.", "[pointers]") {
  CHECK(run_core("(print (size-of i8) (size-of u16) (size-of i32) (size-of f32)"
                 " (size-of int) (size-of (ptr i32)))\n") ==
        "1 2 4 4 8 8\n");
  // Bytes of a buffer: written as u8, read back as i32 (little-endian
  // assumed by the test: x86-64 and arm64 both are).
  CHECK(run_core("(let bytes (buffer 8))\n"
                 "(let p (cast (many u8) bytes))\n"
                 "(store u8 p 1)\n"
                 "(store u8 (offset p 1) 2)\n"
                 "(store u8 (offset p 3 u8) 255)\n"
                 "(print (load i32 p) (load u8 (offset p 3)) (load i8 (offset p 3)))\n"
                 "(store i32 p -1)\n"
                 "(print (load u32 p) (load i16 p) (load u8 (offset p 1 u8)))\n"
                 "(let f (cast (ptr f32) bytes))\n"
                 "(store f32 f 1.5)\n"
                 "(print (load f32 f))\n") ==
        "-16776703 255 -1\n4294967295 -1 255\n1.5\n");
  // A pointer's address as a whole number, and back.
  CHECK(run_core("(let n 4)\n"
                 "(let p (ref n))\n"
                 "(let address (cast int p))\n"
                 "(let q (cast (ptr int) address))\n"
                 "(print (= (load int q) 4) (> address 0))\n") == "true true\n");
}

TEST_CASE("Pointer types are checked.", "[pointers]") {
  CHECK_THAT(problems("(let n 1)\n(print (load i32 n))\n"),
             ContainsSubstring("needs a pointer"));
  CHECK_THAT(problems("(let n 1)\n(let p (ref n))\n(store int p \"text\")\n"),
             ContainsSubstring("must be a whole number"));
  CHECK_THAT(problems("(let p (cast ptr null))\n(print (offset p 1))\n"),
             ContainsSubstring("Say what to count in"));
  CHECK_THAT(problems("(let x 1.5)\n(print (cast (ptr int) x))\n"),
             ContainsSubstring("Can't change"));
  // A typed pointer goes where C's void * does.
  CHECK(problems("(let n 1)\n(let p (ref n))\n(let v (cast ptr p))\n").empty());
}
