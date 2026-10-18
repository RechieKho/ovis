#include <catch2/catch_test_macros.hpp>

#include <ovis/core/checker.hpp>
#include <ovis/core/elaborate.hpp>

#include "support.hpp"

// Records in Ovis Core: `record`, `new`, `field`, `set-field`.

namespace {

struct elaborated {
  ovis::core::program program;
  std::vector<std::string> notes;
  std::vector<std::string> problems;
};

auto elaborate_and_check(const std::string &p_core) -> elaborated {
  elaborated result;
  result.program.units.push_back(ovis::core::parse_unit(p_core));
  for (const auto &note : ovis::core::elaborate(result.program))
    result.notes.push_back(note.message);
  for (const auto &problem : ovis::core::check(result.program))
    result.problems.push_back(problem.message);
  return result;
}

auto run_records(const std::string &p_core) -> std::string {
  auto checked = elaborate_and_check(p_core);
  for (const auto &problem : checked.problems)
    UNSCOPED_INFO(problem);
  REQUIRE(checked.problems.empty());
  return run_program(checked.program);
}

auto has(const std::vector<std::string> &p_messages, const std::string &p_text)
    -> bool {
  for (const auto &message : p_messages)
    if (message.find(p_text) != std::string::npos)
      return true;
  return false;
}

const std::string book = "(record book (title str) (pages int) (price float))\n";

} // namespace

TEST_CASE("Parse records.", "[records]") {
  const auto unit = ovis::core::parse_unit(
      book + "(let b (record book) (new book (title \"Dune\") (pages 412) "
             "(price 9.5)))\n"
             "(set-field b price 8)\n"
             "(print (field b title))\n"
             "(let shelf (list (record book)) (list b))");
  REQUIRE(unit.records.size() == 1);
  const auto &record = unit.records[0];
  CHECK(record.name == "book");
  REQUIRE(record.fields.size() == 3);
  CHECK(record.fields[2].name == "price");
  CHECK(record.fields[2].type == ovis::core::value_type::real());
  const auto book_type = ovis::core::value_type::record_of("book");
  CHECK(unit.statements[0].declared_type == book_type);
  CHECK(unit.statements[0].values[0].fields ==
        std::vector<std::string>{"title", "pages", "price"});
  CHECK(unit.statements[1].kind == ovis::core::statement::kind_type::set_field);
  CHECK(unit.statements[1].name == "price");
  CHECK(unit.statements[3].declared_type ==
        ovis::core::value_type::list_of(book_type));
  CHECK(book_type.to_string() == "(record book)");
  CHECK(ovis::core::describe_in_english(book_type) == "a book");
  CHECK(ovis::core::describe_in_english(
            ovis::core::value_type::list_of(book_type)) == "a list of books");
  CHECK(ovis::core::describe_in_english(
            ovis::core::value_type::record_of("order_item")) ==
        "an order item");

  CHECK_THROWS(ovis::core::parse_unit("(record int (x int))"));
  CHECK_THROWS(ovis::core::parse_unit("(print (new book title))"));
  CHECK_THROWS(ovis::core::parse_unit("(print (field b))"));
  CHECK_THROWS(ovis::core::parse_unit("(fn f () (record a (x int)))"));
}

TEST_CASE("Build, read and change records.", "[records][codegen]") {
  CHECK(run_records(book +
                    "(let b (new book (title \"Dune\") (pages 412) (price 9)))\n"
                    "(print b)\n"
                    "(set-field b price (+ (field b price) 0.5))\n"
                    "(print (field b title) (field b price))") ==
        "a book with title \"Dune\", pages 412, price 9.0\nDune 9.5\n");

  // By reference: a function changing its argument changes the caller's.
  CHECK(run_records(book +
                    "(fn discount ((b (record book)) (percent float))\n"
                    "  (set-field b price (* (field b price) "
                    "(- 1 (/ percent 100)))))\n"
                    "(let b (new book (title \"Dune\") (pages 412) (price 10)))\n"
                    "(let same b)\n"
                    "(discount b 20)\n"
                    "(print (field same price) (= same b))") ==
        "8.0 true\n");

  // Lists of records, records inside records, booleans and lists as fields.
  CHECK(run_records(
            "(record author (name str) (alive bool))\n"
            "(record novel (title str) (by (record author)) (years int[]))\n"
            "(let frank (new author (name \"Frank\") (alive false)))\n"
            "(let shelf (list (new novel (title \"Dune\") (by frank) "
            "(years (list 1965)))))\n"
            "(append shelf (new novel (by frank) (title \"Children\") "
            "(years (list 1976 1977))))\n"
            "(print shelf)\n"
            "(each n shelf (print (field (field n by) name) (len (field n "
            "years))))\n"
            "(print (array (record novel) 0))") ==
        "[(a novel with title \"Dune\", by (an author with name \"Frank\", "
        "alive false), years [1965]), (a novel with title \"Children\", by "
        "(an author with name \"Frank\", alive false), years [1976, "
        "1977])]\nFrank 1\nFrank 2\n[]\n");

  // A record that holds its own kind; an empty link prints as nothing.
  CHECK(run_records("(record node (value int) (next (record node)))\n"
                    "(let last (record node))\n"
                    "(let first (new node (value 1) (next last)))\n"
                    "(print first)") ==
        "a node with value 1, next nothing\n");
}

TEST_CASE("Infer record types and fields.", "[records][elaborate]") {
  // A field's type comes from the values given to it; a parameter's record
  // from the only record with the fields it uses.
  const auto checked = elaborate_and_check(
      "(record point x y (label))\n"
      "(fn shift (p) (set-field p x (+ (field p x) 1)))\n"
      "(let p (new point (x 1.5) (y 2) (label \"a\")))\n"
      "(shift p)\n"
      "(print (field p x))");
  for (const auto &problem : checked.problems)
    UNSCOPED_INFO(problem);
  REQUIRE(checked.problems.empty());
  const auto &record = checked.program.units[0].records[0];
  CHECK(record.fields[0].type == ovis::core::value_type::real());
  CHECK(record.fields[1].type == ovis::core::value_type::integer());
  CHECK(record.fields[2].type == ovis::core::value_type::string());
  CHECK(has(checked.notes, "A point's `x` is a decimal number, from the "
                           "values it is given."));
  CHECK(checked.program.units[0].functions[0].parameters[0].type ==
        ovis::core::value_type::record_of("point"));
  CHECK(run_program(checked.program) == "2.5\n");

  // A field nothing gives a value is a whole number, as variables are.
  const auto unused = elaborate_and_check("(record box (size))");
  CHECK(has(unused.notes, "A box's `size` is a whole number, since nothing "
                          "says otherwise."));
}

TEST_CASE("Report misused records in the author's words.",
          "[records][checker]") {
  const auto problems = [](const std::string &p_core) {
    return elaborate_and_check(p_core).problems;
  };
  CHECK(has(problems(book + "(print (new book (title \"Dune\")))"),
            "A new book needs a value for every field; missing: pages, "
            "price."));
  CHECK(has(problems(book + "(let b (new book (title \"a\") (pages 1) "
                            "(price 2) (cost 3)))"),
            "A book has no field 'cost'. Its fields are: title, pages, "
            "price."));
  CHECK(has(problems(book + "(let b (new book (title \"a\") (pages 1) "
                            "(price 2)))\n(print (field b cost))"),
            "A book has no field 'cost'."));
  CHECK(has(problems(book + "(let b (new book (title 1) (pages 1) "
                            "(price 2)))"),
            "The field 'title' must be text but is a whole number."));
  CHECK(has(problems(book + "(let b (new book (title \"a\") (pages 1) "
                            "(price 2)))\n(set-field b pages \"many\")"),
            "The new value of 'pages' must be a whole number but is text."));
  CHECK(has(problems("(let n 3)\n(print (field n size))"),
            "A field needs a record but got a whole number."));
  CHECK(has(problems("(let b (record shelf))"),
            "There is no record called 'shelf'."));
  CHECK(has(problems("(record a (x int))\n(record a (y int))"),
            "The record 'a' is defined twice."));
  CHECK(has(problems("(record a (x int) (x str))"),
            "The field 'x' of 'a' is listed twice."));
  CHECK(has(problems(book + "(let b (new book (title \"a\") (pages 1) "
                            "(price 2)))\n(print (+ \"Book: \" b))"),
            "Cannot join a book to text."));
  CHECK(has(problems(book + "(let b (new book (title \"a\") (pages 1) "
                            "(price 2)))\n(let s (list b))\n(sort s)"),
            "Only lists of numbers or text can be sorted."));
}

TEST_CASE("Stop on a record used before it has a value.", "[records][codegen]") {
  // The program fails at run time; its output so far is still shown.
  auto checked = elaborate_and_check(book + "(let b (record book))\n"
                                            "(print 1)\n"
                                            "(print (field b title))");
  REQUIRE(checked.problems.empty());
  CHECK(run_program(checked.program) == "1\n");
}
