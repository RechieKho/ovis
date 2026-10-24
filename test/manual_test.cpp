#include <algorithm>

#include <catch2/catch_test_macros.hpp>

#include <ovis/phrase/front_end.hpp>

using namespace std::literals::string_literals;

TEST_CASE("Find a phrase by the words of what it does.", "[phrase][manual]") {
  // The pattern's words count in full, so the phrase asked for comes first.
  const auto rounding = ovis::phrase::manual_search({"round", "decimals"}, {}, 3);
  REQUIRE(!rounding.empty());
  CHECK(rounding.front().pattern == "NUMBER rounded to NUMBER decimal");
  CHECK(rounding.front().topic == "numbers");
  CHECK(!rounding.front().about.empty());
  CHECK(!rounding.front().examples.empty());

  // A way of saying it other than the first finds it too, and shows the rest.
  const auto upper = ovis::phrase::manual_search({"uppercase"}, {}, 3);
  REQUIRE(!upper.empty());
  CHECK(upper.front().pattern == "TEXT in capitals");
  CHECK(std::ranges::find(upper.front().aliases, "TEXT in uppercase"s) !=
        upper.front().aliases.end());

  // Words only the `about:` sentence has: "letter" is not in `in capitals`.
  const auto by_meaning = ovis::phrase::manual_search({"letter", "capital"}, {}, 6);
  CHECK(std::ranges::any_of(by_meaning, [](const auto &p_entry) {
    return p_entry.pattern == "TEXT in capitals";
  }));
  // ...but a word of the pattern ranks above a word of the sentence.
  CHECK(by_meaning.front().pattern == "TEXT in capitals");

  CHECK(ovis::phrase::manual_search({"zzyzx"}, {}, 6).empty());
}

TEST_CASE("List every phrase by topic when asked for none in particular.",
          "[phrase][manual]") {
  const auto every = ovis::phrase::manual_search({}, {}, 0);
  CHECK(every.size() == ovis::phrase::builtin_templates().size());
  // The library's order keeps a topic's phrases together.
  CHECK(every.front().topic == "core");
  CHECK(std::ranges::all_of(every, [](const auto &p_entry) {
    return !p_entry.about.empty() && !p_entry.pattern.empty();
  }));
}

TEST_CASE("A program's own phrases are searched with the library's.",
          "[phrase][manual]") {
  auto own = ovis::phrase::read_templates(
      "expr  flip of {xs: num[]} : num[] => (prelude_reverse $xs)\n"
      "      about: The numbers back to front.\n",
      "phrases/mine.phr");
  const auto found = ovis::phrase::manual_search({"flip"}, own, 3);
  REQUIRE(!found.empty());
  CHECK(found.front().pattern == "flip of LIST");
  CHECK(found.front().topic == "mine");
}
