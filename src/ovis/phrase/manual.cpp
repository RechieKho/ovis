#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <numeric>

#include <ovis/phrase/front_end.hpp>

#include "questions.hpp"

namespace ovis::phrase {

namespace {

// What an entry is embedded as: every way of saying it, then its sentence.
// The examples are left out: their names would pull a query toward them.
auto entry_text(const phrase_template &p_item) -> string_type {
  string_type text;
  for (const auto &pattern : p_item.patterns)
    text += spoken_pattern(p_item, pattern) + ". ";
  return text + p_item.about;
}

// A key for the cache, not a safeguard: FNV-1a over the model and the text.
auto cache_key(const string_type &p_model, const string_type &p_text)
    -> std::uint64_t {
  std::uint64_t key = 14695981039346656037ULL;
  for (const auto letter : p_model + '\n' + p_text)
    key = (key ^ static_cast<unsigned char>(letter)) * 1099511628211ULL;
  return key;
}

constexpr char cache_magic[] = "OVMAN1\n";

using vectors_by_key = std::map<std::uint64_t, std::vector<float>>;

auto read_cache(const std::filesystem::path &p_path) -> vectors_by_key {
  vectors_by_key result;
  std::ifstream file(p_path, std::ios::binary);
  char magic[sizeof cache_magic - 1] = {};
  std::uint32_t size = 0;
  std::uint64_t count = 0;
  if (!file.read(magic, sizeof magic) ||
      std::memcmp(magic, cache_magic, sizeof magic) != 0 ||
      !file.read(reinterpret_cast<char *>(&size), sizeof size) ||
      !file.read(reinterpret_cast<char *>(&count), sizeof count) || size == 0)
    return {};
  for (std::uint64_t index = 0; index < count; ++index) {
    std::uint64_t key = 0;
    std::vector<float> vector(size);
    if (!file.read(reinterpret_cast<char *>(&key), sizeof key) ||
        !file.read(reinterpret_cast<char *>(vector.data()),
                   static_cast<std::streamsize>(size * sizeof(float))))
      return {};
    result.emplace(key, std::move(vector));
  }
  return result;
}

// A cache that can't be written is only slower next time.
auto write_cache(const std::filesystem::path &p_path,
                 const vectors_by_key &p_vectors) -> void {
  if (p_vectors.empty())
    return;
  std::error_code ignored;
  std::filesystem::create_directories(p_path.parent_path(), ignored);
  const auto temporary = std::filesystem::path(p_path).concat(".tmp");
  {
    std::ofstream file(temporary, std::ios::binary);
    const auto size = static_cast<std::uint32_t>(p_vectors.begin()->second.size());
    const auto count = static_cast<std::uint64_t>(p_vectors.size());
    file.write(cache_magic, sizeof cache_magic - 1);
    file.write(reinterpret_cast<const char *>(&size), sizeof size);
    file.write(reinterpret_cast<const char *>(&count), sizeof count);
    for (const auto &[key, vector] : p_vectors) {
      file.write(reinterpret_cast<const char *>(&key), sizeof key);
      file.write(reinterpret_cast<const char *>(vector.data()),
                 static_cast<std::streamsize>(size * sizeof(float)));
    }
    if (!file)
      return;
  }
  std::filesystem::rename(temporary, p_path, ignored);
}

// How alike each entry is to the query, by the dot product of unit vectors;
// empty when the model can't embed or fails (the search is then lexical).
// Only entries the cache lacks are embedded.
auto embedding_scores(const string_type &p_query,
                      const std::vector<string_type> &p_texts,
                      const manual_embeddings &p_options) -> std::vector<double> {
  try {
    const auto model = p_options.model->fingerprint() + p_options.model->name();
    auto cache = p_options.cache.empty() ? vectors_by_key{} : read_cache(p_options.cache);
    std::vector<std::uint64_t> keys;
    std::vector<string_type> missing;
    for (const auto &text : p_texts) {
      keys.push_back(cache_key(model, text));
      if (!cache.contains(keys.back()) &&
          std::ranges::find(missing, text) == missing.end())
        missing.push_back(text);
    }
    if (!missing.empty()) {
      const auto made = p_options.model->embed(missing);
      if (made.size() != missing.size())
        return {};
      for (std::size_t index = 0; index < missing.size(); ++index)
        cache[cache_key(model, missing[index])] = made[index];
      if (!p_options.cache.empty())
        write_cache(p_options.cache, cache);
    }
    const auto asked = p_options.model->embed({p_query});
    if (asked.size() != 1)
      return {};
    std::vector<double> scores;
    for (const auto key : keys) {
      const auto &vector = cache.at(key);
      if (vector.size() != asked[0].size())
        return {};
      double alike = 0;
      for (std::size_t at = 0; at < vector.size(); ++at)
        alike += double(vector[at]) * asked[0][at];
      scores.push_back(alike);
    }
    return scores;
  } catch (const error &) {
    return {};
  }
}

// Reciprocal-rank fusion's constant: small, so the first few of either list
// count much more than the rest.
constexpr double fusion_constant = 10;

} // namespace

auto manual_search(const std::vector<string_type> &p_words,
                   const std::vector<phrase_template> &p_more,
                   std::size_t p_limit, const manual_embeddings &p_embeddings)
    -> std::vector<manual_entry> {
  // The built-in wording first, so it ranks first among equals.
  auto templates = builtin_templates();
  templates.insert(templates.end(), p_more.begin(), p_more.end());
  std::vector<std::size_t> chosen;
  if (p_words.empty()) {
    for (std::size_t index = 0; index < templates.size(); ++index)
      chosen.push_back(index);
  } else {
    const auto lexical =
        internal::manual_matches(p_words, templates, templates.size());
    chosen = lexical;
    std::vector<double> alike;
    if (p_embeddings.model != nullptr) {
      std::vector<string_type> texts;
      for (const auto &item : templates)
        texts.push_back(entry_text(item));
      string_type query;
      for (const auto &word : p_words)
        query += (query.empty() ? "" : " ") + word;
      alike = embedding_scores(query, texts, p_embeddings);
    }
    if (!alike.empty()) {
      // Each list gives an entry 1 / (constant + its place); an entry only one
      // list has gets that list's share. Ties keep the lexical order.
      std::vector<std::size_t> by_meaning(templates.size());
      std::iota(by_meaning.begin(), by_meaning.end(), 0);
      std::ranges::stable_sort(by_meaning, std::ranges::greater{},
                               [&](std::size_t p_index) { return alike[p_index]; });
      std::vector<double> score(templates.size(), 0);
      for (std::size_t place = 0; place < by_meaning.size(); ++place)
        score[by_meaning[place]] += 1 / (fusion_constant + double(place));
      for (std::size_t place = 0; place < lexical.size(); ++place)
        score[lexical[place]] += 1 / (fusion_constant + double(place));
      std::vector<std::size_t> order(templates.size());
      std::iota(order.begin(), order.end(), 0);
      // The lexical order first, so it decides among equals.
      std::vector<std::size_t> rank(templates.size(), templates.size());
      for (std::size_t place = 0; place < lexical.size(); ++place)
        rank[lexical[place]] = place;
      std::ranges::stable_sort(order, [&](std::size_t p_left, std::size_t p_right) {
        return score[p_left] != score[p_right] ? score[p_left] > score[p_right]
                                               : rank[p_left] < rank[p_right];
      });
      chosen = std::move(order);
    }
    if (p_limit != 0 && chosen.size() > p_limit)
      chosen.resize(p_limit);
  }
  std::vector<manual_entry> result;
  for (const auto index : chosen) {
    const auto &item = templates[index];
    manual_entry entry{.topic = std::filesystem::path(item.file).stem().string(),
                       .package = item.package,
                       .pattern = spoken_pattern(item, item.patterns.front()),
                       .about = item.about,
                       .examples = item.examples};
    for (std::size_t other = 1; other < item.patterns.size(); ++other)
      entry.aliases.push_back(spoken_pattern(item, item.patterns[other]));
    result.push_back(std::move(entry));
  }
  return result;
}

} // namespace ovis::phrase
