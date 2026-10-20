#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <sstream>

#include <ovis/phrase/front_end.hpp>

#include "support.hpp"

// Generics written in phrases (docs/proposals/generics.md §2–§3): `any
// NAME`, bounds, `a stack of (item) has:`, type words `stack of books`.

using Catch::Matchers::ContainsSubstring;

namespace {

auto problems_of(const ovis::phrase::front_end_result &p_result)
    -> std::string {
  std::string text;
  for (const auto &problem : p_result.problems)
    text += problem + "\n";
  return text;
}

auto run_phrases(const std::string &p_source) -> std::string {
  ovis::phrase::front_end_options options;
  const auto result = ovis::phrase::compile_phrases(p_source, nullptr, options);
  INFO(problems_of(result));
  REQUIRE(result.succeeded());
  REQUIRE(result.timing.questions == 0);
  return run_program(result.program);
}

auto problems(const std::string &p_source) -> std::string {
  ovis::phrase::front_end_options options;
  const auto result = ovis::phrase::compile_phrases(p_source, nullptr, options);
  REQUIRE(!result.succeeded());
  return problems_of(result);
}

// Says "none of these" to every question, remembering the options.
class listing_model final : public ovis::english::language_model {
public:
  std::vector<std::string> options;

  auto name() const -> std::string override { return "listing"; }
  auto chat(const std::vector<ovis::english::chat_message> &,
            std::string_view) -> std::string override {
    return {};
  }
  auto choose(const std::vector<ovis::english::chat_message> &p_messages,
              std::size_t p_options) -> std::vector<double> override {
    std::istringstream lines(p_messages.back().content);
    for (std::string line; std::getline(lines, line);) {
      const auto dot = line.find(". ");
      if (dot != std::string::npos && dot > 0 &&
          line.find_first_not_of("0123456789") == dot &&
          line.substr(dot + 2) != "none of these")
        options.push_back(line.substr(dot + 2));
    }
    std::vector<double> result(p_options, 0);
    result.back() = 1;
    return result;
  }
};

const std::string stack =
    "a stack of (item) has:\n"
    "    items (list of items, empty list of items)\n"
    "    size (whole number, 0)\n"
    "\n"
    "to push (x: any item) onto (s: stack of item):\n"
    "    add x to items of s\n"
    "    increase size of s by 1\n"
    "\n"
    "to pop from (s: stack of any item):\n"
    "    x = last item of items of s\n"
    "    remove item (size of s) of items of s\n"
    "    decrease size of s by 1\n"
    "    give back x\n";

} // namespace

TEST_CASE("A generic record and generic phrases, for several types",
          "[phrase][generic]") {
  CHECK(run_phrases(stack +
                    "numbers = a stack with items [1, 2], size 2\n"
                    "push 3 onto numbers\n"
                    "show pop from numbers, size of numbers\n"
                    "names = a new stack of text\n"
                    "push \"ada\" onto names\n"
                    "show pop from names\n"
                    "show names\n") ==
        "3 2\nada\na stack of text with items [], size 0\n");
  // A record's name as a type word with its types: `stack of books`.
  CHECK(run_phrases(stack +
                    "a book has:\n    title (text)\n"
                    "to shelve (b: book) on (s: stack of books):\n"
                    "    push b onto s\n"
                    "shelf = a new stack of books\n"
                    "shelve a book with title \"Dune\" on shelf\n"
                    "show title of pop from shelf\n") == "Dune\n");
}

TEST_CASE("The type filter keeps a generic record's types", "[phrase][generic]") {
  CHECK_THAT(problems(stack + "names = a new stack of text\n"
                              "push 3 onto names\n"),
             ContainsSubstring("No phrase Ovis knows reads \"push 3 onto names\""));
}

TEST_CASE("A generic record's types are said or decided", "[phrase][generic]") {
  CHECK_THAT(problems(stack + "s = a new stack\n"),
             ContainsSubstring("Say what a stack holds: `a new stack of …`"));
}

TEST_CASE("Bounds say what a type variable can do", "[phrase][generic]") {
  const std::string biggest =
      "to the biggest of (xs: list of any comparable thing):\n"
      "    best = first item of xs\n"
      "    for each x in xs:\n"
      "        if best comes before x:\n"
      "            best = x\n"
      "    give back best\n";
  CHECK(run_phrases(biggest +
                    "a book has:\n    title (text)\n    pages (whole number)\n"
                    "to (a: book) comes before (b: book):\n"
                    "    give back pages of a < pages of b\n"
                    "show the biggest of [3, 9, 4], the biggest of [\"b\", \"a\"]\n"
                    "books = [a book with title \"A\", pages 3, a book with "
                    "title \"B\", pages 7]\n"
                    "show title of the biggest of books\n") == "9 b\nB\n");
  CHECK_THAT(problems(biggest + "a box has:\n    label (text)\n"
                                "boxes = [a box with label \"x\"]\n"
                                "show label of the biggest of boxes\n"),
             ContainsSubstring("needs comparable values, but a box isn't one. "
                               "Say how two compare: `to (a: box) comes before "
                               "(b: box):`."));
  CHECK_THAT(problems("to bump (x: any item):\n    give back x + 1\n"
                      "show bump 2\n"),
             ContainsSubstring("'+' needs an item to be number: write `any "
                               "number item` where it is first named."));
  CHECK(run_phrases("to bump (x: any number item):\n    give back x + 1\n"
                    "show bump 2, bump 1.5\n") == "3 2.5\n");
}

TEST_CASE("Untyped parameters stay one type, and say how to take any",
          "[phrase][generic]") {
  CHECK_THAT(problems("to swap (a) and (b):\n    show b, a\n"
                      "swap 1 and 2\nswap \"a\" and \"b\"\n"),
             ContainsSubstring("For a phrase that takes any type, write (a: "
                               "any thing) in its header."));
  CHECK(run_phrases("to swap (a: any showable thing) and (b: thing):\n    show b, a\n"
                    "swap 1 and 2\nswap \"a\" and \"b\"\n") == "2 1\nb a\n");
}

TEST_CASE("Traits on generic records", "[phrase][generic]") {
  CHECK(run_phrases(stack +
                    "a stack of (item) is showable:\n"
                    "    text of (it):\n"
                    "        give back \"a stack of \" + size of it\n"
                    "a stack of (comparable item) is comparable:\n"
                    "    (it) comes before (other):\n"
                    "        give back size of it < size of other\n"
                    "a container can:\n    count of (it) gives a whole number\n"
                    "a stack of (item) is a container:\n"
                    "    count of (it):\n        give back size of it\n"
                    "small = a new stack of whole numbers\n"
                    "big = a new stack of whole numbers\n"
                    "push 1 onto big\n"
                    "show small, small comes before big, count of big\n"
                    "things = [big]\n"
                    "show \"in a list:\", things\n") ==
        "a stack of 0 true 1\nin a list: [a stack of 1]\n");
}

TEST_CASE("A generic record's header names its type variables",
          "[phrase][generic]") {
  CHECK(run_phrases("a pair of (first) and (second) has:\n"
                    "    left (first)\n    right (second)\n"
                    "to swapped (p: pair of any key and any value):\n"
                    "    give back a pair of value and key with left right of p, "
                    "right left of p\n"
                    "p = a pair of whole numbers and text with left 1, right \"one\"\n"
                    "q = swapped p\n"
                    "show left of q, right of q\n") == "one 1\n");
  CHECK_THAT(problems("a list of (item) has:\n    items (list of items)\n"),
             ContainsSubstring("'list' is already a type Ovis has"));
}

TEST_CASE("Questions offer generic phrases", "[phrase][generic][question]") {
  listing_model model;
  ovis::phrase::front_end_options options;
  ovis::phrase::compile_phrases(stack + "numbers = a new stack of whole numbers\n"
                                        "push 3 on numbers\n",
                                &model, options);
  CHECK(std::ranges::find(model.options, "push 3 onto numbers") !=
        model.options.end());
}
