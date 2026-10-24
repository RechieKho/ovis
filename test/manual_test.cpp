#include <algorithm>
#include <filesystem>

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

namespace {

// Embeds by topic: a text about capitals is one direction, a query that says
// "shout" the same, everything else another. Counts the texts it was given.
class topic_model final : public ovis::english::language_model {
public:
  std::size_t embedded = 0;
  auto name() const -> std::string override { return "topic"; }
  auto chat(const std::vector<ovis::english::chat_message> &, std::string_view)
      -> std::string override {
    return {};
  }
  auto embed(const std::vector<std::string> &p_texts)
      -> std::vector<std::vector<float>> override {
    std::vector<std::vector<float>> result;
    for (const auto &text : p_texts) {
      ++embedded;
      const bool capitals = text.find("capitals") != std::string::npos ||
                            text.find("shout") != std::string::npos;
      result.push_back(capitals ? std::vector<float>{1, 0}
                                : std::vector<float>{0, 1});
    }
    return result;
  }
};

} // namespace

TEST_CASE("A model finds a phrase by its meaning where the words miss it.",
          "[phrase][manual]") {
  CHECK(ovis::phrase::manual_search({"shout"}, {}, 3).empty());

  topic_model model;
  const auto cache = std::filesystem::temp_directory_path() / "ovis-manual-test.bin";
  std::filesystem::remove(cache);
  const auto found = ovis::phrase::manual_search(
      {"shout"}, {}, 3, {.model = &model, .cache = cache});
  REQUIRE(!found.empty());
  CHECK(found.front().pattern == "TEXT in capitals");
  CHECK(model.embedded > 100);

  // A second search embeds the query alone: the entries come from the cache.
  model.embedded = 0;
  const auto again = ovis::phrase::manual_search(
      {"shout"}, {}, 3, {.model = &model, .cache = cache});
  CHECK(model.embedded == 1);
  CHECK(again.front().pattern == "TEXT in capitals");
  std::filesystem::remove(cache);

  // A word of the phrase still counts: the words and the meaning are joined.
  const auto both = ovis::phrase::manual_search({"reversed"}, {}, 3, {.model = &model});
  CHECK(std::ranges::any_of(both, [](const auto &p_entry) {
    return p_entry.pattern == "TEXT reversed";
  }));

  // A model that can't embed leaves the search to the words.
  class mute final : public ovis::english::language_model {
  public:
    auto name() const -> std::string override { return "mute"; }
    auto chat(const std::vector<ovis::english::chat_message> &, std::string_view)
        -> std::string override {
      return {};
    }
  } silent;
  CHECK(ovis::phrase::manual_search({"shout"}, {}, 3, {.model = &silent}).empty());
  CHECK(ovis::phrase::manual_search({"uppercase"}, {}, 3, {.model = &silent})
            .front()
            .pattern == "TEXT in capitals");
}
