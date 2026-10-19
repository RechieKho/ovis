#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <sstream>

#include <ovis/phrase/front_end.hpp>
#include <ovis/phrase/templates.hpp>

#include "support.hpp"

// Records written in phrases (types.md §1): `a NAME has:`, `a NAME with …`,
// fields, field slots in templates, and questions about them.

using Catch::Matchers::ContainsSubstring;

namespace {

using strings = std::vector<std::string>;

const std::string book = "a book has:\n"
                         "    title (text)\n"
                         "    pages (whole number)\n"
                         "    price (decimal, 0)\n";

auto problems_of(const ovis::phrase::front_end_result &p_result)
    -> std::string {
  std::string text;
  for (const auto &problem : p_result.problems)
    text += problem + "\n";
  return text;
}

// What the program prints, built with no model.
auto run_phrases(const std::string &p_source) -> std::string {
  const auto result = ovis::phrase::compile_phrases(p_source, nullptr);
  INFO(problems_of(result));
  REQUIRE(result.succeeded());
  REQUIRE(result.timing.questions == 0);
  return run_program(result.program);
}

auto problems(const std::string &p_source) -> std::string {
  const auto result = ovis::phrase::compile_phrases(p_source, nullptr);
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
    FAIL("Only choose is used.");
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

auto candidates(const std::string &p_source) -> strings {
  listing_model model;
  ovis::phrase::compile_phrases(p_source, &model);
  return model.options;
}

} // namespace

TEST_CASE("Declare records and use their fields.", "[phrase][records]") {
  // Defaults fill fields left out; a field is read and set three ways.
  CHECK(run_phrases(book + "b = a book with title \"Dune\", pages 412\n"
                           "show b\n"
                           "price of b = 9.5\n"
                           "b's pages = b.pages + 1\n"
                           "b.title = \"Dune!\"\n"
                           "show b's title, b.pages, price of b\n") ==
        "a book with title \"Dune\", pages 412, price 0.0\n"
        "Dune! 413 9.5\n");

  // Items written as English has them: an article, `of`/`being`/`is`
  // before the value, `and with`, and `named`/`called` for `name`.
  const std::string person = "a person has:\n"
                             "    name (text)\n"
                             "    age (whole number)\n";
  CHECK(run_phrases(person +
                    "a = a person with the name \"A\", and with the age of 1\n"
                    "b = a person named \"B\" with age being 2\n"
                    "c = a person called \"C\" and the age is 3\n"
                    "show a's name, a's age, b's name, b's age, c's name, "
                    "c's age\n") == "A 1 B 2 C 3\n");

  // A field named in several words is read where one word is: made, read
  // three ways, set, in a field slot, and printed as written.
  const std::string car = "a car has:\n    number of wheels (whole number)\n"
                          "    top speed (decimal, 100)\n";
  CHECK(run_phrases(car +
                    "c = a car with the number of wheels of 4\n"
                    "d = a car with number of wheels 3, top speed 90\n"
                    "show c\n"
                    "set c's number of wheels to 6\n"
                    "increase c's top speed by 20\n"
                    "show the number of wheels of c, c.number_of_wheels, "
                    "c's top speed\n"
                    "for each x in [c, d] sorted by number of wheels:\n"
                    "    show x's number of wheel\n") ==
        "a car with number of wheels 4, top speed 100.0\n"
        "6 6 120.0\n3\n6\n");

  // `set to`, `equal to`, `equals` before a value; a field's name may hold
  // an article (`button a`).
  CHECK(run_phrases("a switch has:\n    button a (truth value)\n"
                    "    button b (truth value)\n"
                    "s = a switch with button a set to true, button b set to false\n"
                    "t = a switch with button a equal to false and button b "
                    "equals true\n"
                    "show s, t's button b\n") ==
        "a switch with button a true, button b false true\n");

  // A list field's items joined by `and`, after `which are`.
  CHECK(run_phrases(person +
                    "a house has:\n    address (text)\n"
                    "    people (list of person)\n"
                    "w = a person with name \"W\", age 1\n"
                    "v = a person with name \"V\", age 2\n"
                    "h = a house with address of \"Road\", and people which "
                    "are w and v\n"
                    "g = a house with address \"Lane\", people v\n"
                    "show length of h's people, length of g's people\n") ==
        "2 1\n");

  // Variables named in several words, without `of`; a statement stays one.
  CHECK(run_phrases("fan speed = 100\n"
                    "set the top speed to fan speed + 20\n"
                    "let max load be 3\nincrease fan speed by 5\n"
                    "show fan speed, top speed, max load\n"
                    "x = 3\nshow x = 3\n") == "105 120 3\ntrue\n");

  // Variables and fields changed in words.
  CHECK(run_phrases(person +
                    "w = a person with name \"W\", age 1\n"
                    "set w's age to 10\nincrease w's age by 2\n"
                    "add 3 to w's age\nchange w's name to \"V\"\n"
                    "set x to 5\nlet y be 2\nmultiply x by y\n"
                    "subtract 1 from x\ndecrease y by 1\n"
                    "x increased by 1\nx decrease by 1\n"
                    "show w's name, w's age, x, y\n") == "V 15 9 1\n");

  // Records in records and lists; `and` before the last field; a phrase
  // taking a record, stated or inferred from the fields it uses.
  CHECK(run_phrases(
            "an author has:\n"
            "    name\n"
            "    born (whole number, 1900)\n" +
            book + "    by (author)\n"
            "to discount (item: book) by (percent):\n"
            "    price of item = price of item * (1 - percent / 100)\n"
            "to describe (it):\n"
            "    show title of it, name of by of it\n"
            "jane = an author with name \"Jane Austen\"\n"
            "books = [a book with title \"Emma\", pages 474, price 8 and by "
            "jane]\n"
            "add a book with title \"Persuasion\", pages 249, by jane to books\n"
            "for each b in books:\n"
            "    discount b by 50\n"
            "    describe b\n"
            "show item 1 of books's price, jane's born\n") ==
        "Emma Jane Austen\nPersuasion Jane Austen\n4.0 1900\n");

  // A list of records in a field, and an empty one.
  CHECK(run_phrases("a shelf has:\n"
                    "    books (list of books)\n" +
                    book +
                    "s = a shelf with books [a book with title \"A\", pages "
                    "1]\n"
                    "t = a shelf with books empty list of books\n"
                    "show length of s's books, length of t.books\n") ==
        "1 0\n");
}

TEST_CASE("Field slots work for every record.", "[phrase][records]") {
  // Copies of the generic functions for each record and field used.
  CHECK(run_phrases(book +
                    "a city has:\n"
                    "    name (text)\n"
                    "    people (whole number)\n"
                    "books = [a book with title \"B\", pages 20, price 2.5, a "
                    "book with title \"A\", pages 10, price 4]\n"
                    "cities = [a city with name \"Oslo\", people 700, a city "
                    "with name \"Bergen\", people 290]\n"
                    "show total price of books, total people of cities\n"
                    "show the book in books with the most pages's title\n"
                    "show the name of the city in cities with the fewest "
                    "people\n"
                    "for each b in books sorted by title:\n"
                    "    show b's title\n"
                    "sort cities by name\n"
                    "show first item of cities's name\n") ==
        "6.5 990\nB\nBergen\nA\nB\nBergen\n");
  // Only number fields have a total.
  CHECK_THAT(problems(book + "books = [a book with title \"A\", pages 1]\n"
                             "show total title of books\n"),
             ContainsSubstring("total title of books"));
}

TEST_CASE("Report mistakes in records.", "[phrase][records]") {
  CHECK_THAT(problems(book + "b = a book with title \"Dune\"\n"),
             ContainsSubstring("line 5: A new book needs a value for every "
                               "field; missing: pages."));
  CHECK_THAT(problems("a book has:\n    pages (whole number, \"many\")\n"),
             ContainsSubstring("line 2: The default of 'pages' is text, but "
                               "'pages' is a whole number."));
  CHECK_THAT(problems("a book has:\n    pages (pages)\n"),
             ContainsSubstring("The default of 'pages' can't be read"));
  CHECK_THAT(problems("a book has:\n    title\n    title (text)\n"),
             ContainsSubstring("line 3: 'title' is listed twice."));
  CHECK_THAT(problems("a whole number has:\n    x\n"),
             ContainsSubstring("'whole number' is already a type Ovis has"));
  CHECK_THAT(problems(book + "a book has:\n    x\n"),
             ContainsSubstring("line 5: A record called 'book' is already "
                               "declared on line 1."));
  CHECK_THAT(problems("if true:\n    a book has:\n        x\n"),
             ContainsSubstring("Declare records ('a NAME has:') at the top "
                               "level"));
  CHECK_THAT(problems(book + "b = a book with title \"A\", pages 1\n"
                             "show b's colour\n"),
             ContainsSubstring("\"colour\" is unclear"));
  CHECK_THAT(problems(book + "b = a book with title \"A\", pages 1\n"
                             "show \"Book: \" + b\n"),
             ContainsSubstring("line 6"));
}

TEST_CASE("Ask about unknown fields and records named by type.",
          "[phrase][records][question]") {
  const auto with_b = book + "b = a book with title \"Dune\", pages 412\n";
  // A word no field is called: the record's fields with which the line
  // reads.
  CHECK(candidates(with_b + "show cost of b * 2\n") == strings{"pages", "price"});
  CHECK(candidates(with_b + "show total cost of [b]\n") ==
        strings{"pages", "price"});
  CHECK(candidates(with_b + "c = a book with name \"Emma\", pages 3\n") ==
        strings{"title"});
  // Not the fields the line already gives.
  CHECK(candidates(with_b + "c = a book with cost 3, pages 5\n") ==
        strings{"price"});
  // One close spelling is the rule's.
  auto result = ovis::phrase::compile_phrases(with_b + "show b's titl\n", nullptr);
  INFO(problems_of(result));
  REQUIRE(result.succeeded());
  CHECK_THAT(result.source,
             ContainsSubstring("@[meaning: show b's title]\nshow b's titl\n"));

  // "the book": the one variable holding a book, without asking.
  result = ovis::phrase::compile_phrases(
      with_b + "n = 3\nshow the title of the book\n", nullptr);
  INFO(problems_of(result));
  REQUIRE(result.succeeded());
  CHECK(result.timing.by_rule == 1);
  CHECK_THAT(result.source,
             ContainsSubstring("@[meaning: show the title of b]\n"));
  CHECK(ovis::phrase::compile_phrases(result.source, nullptr).source ==
        result.source);
  // Two: the model or the author decides, the most recent first.
  CHECK(candidates(with_b + "c = b\nshow the book\n") == strings{"c", "b"});
  CHECK_THAT(problems(book + "show the book\n"),
             ContainsSubstring("No variable here holds a book, so \"the "
                               "book\" means nothing yet."));
}

TEST_CASE("Read field slots in templates.", "[phrase][templates][records]") {
  const auto templates = ovis::phrase::read_templates(
      "expr  total {f: num field of T} of {xs: T[]} : f => (g:$f $xs)\n"
      "expr  the {t: type} of {xs: t[]} by {f: field of t} : t => (h:$f $xs)\n",
      "test.phr");
  REQUIRE(templates.size() == 2);
  CHECK(templates[0].slots[0].type.kind ==
        ovis::phrase::type_name::kind_type::field);
  CHECK(to_string(templates[0], templates[0].patterns[0]) ==
        "total {f: num field of T} of {xs: T[]}");
  CHECK(to_string(templates[1], templates[1].patterns[0]) ==
        "the {t: type} of {xs: t[]} by {f: field of t}");
  const auto failure = [](std::string_view p_text) {
    try {
      ovis::phrase::read_templates(p_text, "test.phr");
    } catch (const ovis::error &p_error) {
      return std::string(p_error.what());
    }
    return std::string("no error");
  };
  CHECK_THAT(failure("expr  total {f: field of T} of {x: int} : int => (g:$f $x)\n"),
             ContainsSubstring("which no other slot decides"));
  CHECK_THAT(failure("expr  total {f: field of x} of {x: int} : int => (g:$f $x)\n"),
             ContainsSubstring("is neither"));
  CHECK_THAT(failure("expr  total {f: list field of T} of {x: T[]} : int => "
                     "(g:$f $x)\n"),
             ContainsSubstring("is not a type a field can have"));
}

TEST_CASE("A record implements a built-in trait by a phrase.",
          "[phrase][records][traits]") {
  const std::string by_price =
      book + "to (a: book) comes before (b: book):\n"
             "    give back price of a < price of b\n"
             "to (a: book) is the same as (b: book):\n"
             "    give back title of a = title of b\n"
             "to text of (b: book):\n"
             "    give back title of b\n"
             "dune = a book with title \"Dune\", pages 1, price 9\n"
             "emma = a book with title \"Emma\", pages 1, price 8\n"
             "books = [dune, emma]\n";
  // Static dispatch: the operators and the library's phrases call the
  // record's own functions.
  CHECK(run_phrases(by_price + "sort books\n"
                               "show title of first item of books\n"
                               "show smallest of books, largest of books\n"
                               "show dune > emma, dune <= emma\n"
                               "show books contains emma\n") ==
        "Emma\nEmma Dune\ntrue false\ntrue\n");
  // Two records can implement the same trait.
  CHECK(run_phrases(by_price + "a cup has:\n    size (whole number)\n"
                               "to (a: cup) comes before (b: cup):\n"
                               "    give back size of a < size of b\n"
                               "cups = [a cup with size 3, a cup with size 1]\n"
                               "sort cups\n"
                               "show size of first item of cups\n"
                               "sort books\n"
                               "show title of first item of books\n") ==
        "1\nEmma\n");
  // Without the phrase a record is not comparable: the line is unclear and
  // no phrase reads it (offline: a problem).
  const auto result = ovis::phrase::compile_phrases(
      book + "books = [a book with title \"A\", pages 1]\nsort books\n", nullptr);
  REQUIRE(!result.succeeded());
  CHECK_THAT(problems_of(result),
             ContainsSubstring("\"sort books\" needs a book to be "
                               "comparable: write `to (a: book) comes before "
                               "(b: book):`"));
  CHECK_THAT(problems(book + "b = a book with title \"A\"\n"
                             "books = [b]\nshow books contains b\n"),
             ContainsSubstring("to (a: book) is the same as (b: book):"));
  // One implementation per trait and record.
  const auto twice = ovis::phrase::compile_phrases(
      book + "to (a: book) comes before (b: book):\n    give back true\n"
             "to (a: book) comes before (b: book):\n    give back false\n",
      nullptr);
  REQUIRE(!twice.succeeded());
  CHECK_THAT(problems_of(twice), ContainsSubstring("already defined"));
}

TEST_CASE("A default may be a record declared further down.",
          "[records][phrase]") {
  CHECK(run_phrases("a shape has:\n"
                    "    name (text)\n"
                    "    origin (point, a point with x 1, y 2)\n"
                    "\n"
                    "a point has:\n"
                    "    x (whole number)\n"
                    "    y (whole number, 0)\n"
                    "\n"
                    "s = a shape with name \"dot\"\n"
                    "show x of origin of s, y of origin of s\n") == "1 2\n");
  // A default that can't be read is still reported.
  CHECK_THAT(problems("a shape has:\n"
                      "    origin (point, a point with z 1)\n"
                      "a point has:\n"
                      "    x (whole number)\n"),
             ContainsSubstring("line 2"));
}

TEST_CASE("Make a record comparable, equatable or showable with 'is …:'.",
          "[records][phrase][traits]") {
  const auto source = std::string("a book has:\n"
                                  "    title (text)\n"
                                  "    price (decimal)\n"
                                  "a book is comparable:\n"
                                  "    (it) comes before (other):\n"
                                  "        give back price of it < price of other\n"
                                  "a book is equatable:\n"
                                  "    (it) is the same as (other):\n"
                                  "        give back title of it = title of other\n"
                                  "a book is showable:\n"
                                  "    text of (it):\n"
                                  "        give back title of it\n"
                                  "books = [a book with title \"Dune\", price 9.5,"
                                  " a book with title \"Emma\", price 8]\n");
  CHECK(run_phrases(source + "sort books\nshow books\n"
                             "show books contains a book with title \"Dune\", "
                             "price 1\n") == "[Emma, Dune]\ntrue\n");
  // A phrase of another trait, or a body that isn't one, and a second one.
  const auto header = std::string("a book has:\n    title (text)\n");
  CHECK_THAT(problems(header + "a book is comparable:\n"
                               "    (it) is the same as (other):\n"
                               "        give back true\n"),
             ContainsSubstring("A record that is comparable has one phrase"));
  CHECK_THAT(problems(header + "a book is showable:\n"
                               "    show 1\n"),
             ContainsSubstring("Write the phrase as its header"));
  CHECK_THAT(problems(header + "a book is comparable:\n"
                               "    (it) comes before (other):\n"
                               "        give back true\n"
                               "to (a: book) comes before (b: book):\n"
                               "    give back false\n"),
             ContainsSubstring("already defined"));
}
