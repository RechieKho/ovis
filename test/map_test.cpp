#include <catch2/catch_test_macros.hpp>

#include "support.hpp"

// Maps in Ovis Core: `(map K V)`, whole numbers or text as keys, entries in
// the order they were added.

using test_support::run_core;

TEST_CASE("Maps hold values by key, in the order they were added.", "[maps]") {
  CHECK(run_core("(let ages (map-new str int))\n"
                 "(map-put ages \"ann\" 34)\n"
                 "(map-put ages \"bob\" 27)\n"
                 "(map-put ages \"ann\" 35)\n"
                 "(print ages (len ages) (map-get ages \"bob\") (map-has ages \"cy\"))\n"
                 "(each name ages (print name (map-get ages name)))\n"
                 "(map-remove ages \"ann\")\n"
                 "(print ages)\n"
                 "(let squares (map-new int float))\n"
                 "(for i 1 40 (map-put squares i (* i 0.5)))\n"
                 "(print (len squares) (map-get squares 33))\n"
                 "(let keys (map-keys squares))\n"
                 "(print (len keys) (get keys 39))\n"
                 "(let nested (map-new str (list int)))\n"
                 "(map-put nested \"a\" (list 1 2))\n"
                 "(print nested)\n"
                 "(let kept (copy nested))\n"
                 "(append (map-get nested \"a\") 3)\n"
                 "(print kept nested)\n") ==
        "{\"ann\": 35, \"bob\": 27} 2 27 false\n"
        "ann 35\nbob 27\n"
        "{\"bob\": 27}\n"
        "40 16.5\n40 40\n"
        "{\"a\": [1, 2]}\n"
        "{\"a\": [1, 2]} {\"a\": [1, 2, 3]}\n");
}
