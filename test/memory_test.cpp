#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "support.hpp"

// Explicit memory (proposals/memory.md): `with-new-memory`, `with-memory`,
// `new-memory`, `clear-memory`, `free` and `keep` in Ovis Core.

using namespace test_support;

TEST_CASE("Text made in a new memory can be kept past it.", "[memory]") {
  CHECK(run_core("(let kept \"\")\n"
                 "(with-new-memory\n"
                 "  (let t (concat \"he\" \"llo\"))\n"
                 "  (set kept (keep t)))\n"
                 "(print kept)") == "hello\n");
  // A function gives back a kept value from inside the block.
  CHECK(run_core("(fn label ((n int)) str\n"
                 "  (with-new-memory\n"
                 "    (let t (concat \"item \" (to_str n)))\n"
                 "    (return (keep t))))\n"
                 "(let a (label 1))\n(let b (label 22))\n(print a b)") ==
        "item 1 item 22\n");
}

TEST_CASE("A new memory is freed however the block is left.", "[memory]") {
  // stop and skip, and falling off the end, in a loop of many passes: each
  // pass makes about 8 MB, so a memory that leaked would be gigabytes.
  CHECK(run_core("(let total 0)\n"
                 "(for i 1 3000\n"
                 "  (with-new-memory\n"
                 "    (let xs (array int 1000000))\n"
                 "    (put xs 0 i)\n"
                 "    (if (= (% i 2) 0) (then (continue)))\n"
                 "    (if (> i 2990) (then (break)))\n"
                 "    (set total (+ total (get xs 0)))))\n"
                 "(print total)") == "2235025\n");
}

TEST_CASE("Records and lists are copied whole by keep, once.", "[memory]") {
  CHECK(run_core("(record node (name str) (next (record node)))\n"
                 "(let kept (new node (name \"x\") (next null)))\n"
                 "(with-new-memory\n"
                 "  (let a (new node (name (concat \"a\" \"1\")) (next null)))\n"
                 "  (let b (new node (name \"b\") (next a)))\n"
                 "  (set-field a next b)\n"
                 "  (set kept (keep a)))\n"
                 "(print (field kept name) (field (field kept next) name)\n"
                 "       (field (field (field kept next) next) name))") ==
        "a1 b a1\n");
  CHECK(run_core("(record item (name str))\n"
                 "(let names (array str 0))\n"
                 "(let items (array (record item) 0))\n"
                 "(with-new-memory\n"
                 "  (let words (list (concat \"a\" \"b\") (concat \"c\" \"d\")))\n"
                 "  (set names (keep words))\n"
                 "  (let local (array (record item) 0))\n"
                 "  (append local (new item (name (concat \"x\" \"y\"))))\n"
                 "  (set items (keep local)))\n"
                 "(print names (field (get items 0) name))") ==
        "[\"ab\", \"cd\"] xy\n");
}

TEST_CASE("A named memory is cleared and reused.", "[memory]") {
  CHECK(run_core("(let scratch (new-memory))\n"
                 "(for i 1 3\n"
                 "  (with-memory scratch\n"
                 "    (let t (concat \"pass \" (to_str i)))\n"
                 "    (print t))\n"
                 "  (clear-memory scratch))\n"
                 "(free scratch)") == "pass 1\npass 2\npass 3\n");
}

TEST_CASE("Free text, lists, records and memories.", "[memory]") {
  CHECK(run_core("(record box (size int))\n"
                 "(let t (concat \"a\" \"b\"))\n"
                 "(let constant \"written in the program\")\n"
                 "(let xs (array int 3))\n"
                 "(let b (new box (size 3)))\n"
                 "(print t constant (len xs) (field b size))\n"
                 "(free t)\n(free constant)\n(free xs)\n(free b)\n"
                 "(print \"ok\")") ==
        "ab written in the program 3 3\nok\n");
  // Freeing what a new memory made leaves it to the memory.
  CHECK(run_core("(with-new-memory\n"
                 "  (let t (concat \"a\" \"b\"))\n"
                 "  (free t)\n"
                 "  (print \"ok\"))") == "ok\n");
}

TEST_CASE("The checker types the memory forms.", "[memory]") {
  CHECK(check_text("(let m (new-memory))\n(free m)").empty());
  CHECK(!check_text("(free 3)").empty());
  CHECK(!check_text("(clear-memory 3)").empty());
  CHECK(!check_text("(with-memory 3 (print 1))").empty());
  CHECK(!check_text("(let m (new-memory))\n(print m)").empty());
  CHECK(!check_text("(keep 3)").empty());
}
