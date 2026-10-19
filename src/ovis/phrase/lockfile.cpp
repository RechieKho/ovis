#include <algorithm>
#include <fstream>

#include <nlohmann/json.hpp>

#include <ovis/phrase/lockfile.hpp>

namespace ovis::phrase {

namespace {

constexpr int version = 3;

// FNV-1a, 64 bits, as hex: stable across machines and builds.
auto fnv_hash(string_view_type p_text) -> string_type {
  std::uint64_t hash = 14695981039346656037ULL;
  for (const auto character : p_text) {
    hash ^= static_cast<unsigned char>(character);
    hash *= 1099511628211ULL;
  }
  static constexpr char digits[] = "0123456789abcdef";
  string_type result(16, '0');
  for (auto index = result.size(); index-- > 0; hash >>= 4)
    result[index] = digits[hash & 15];
  return result;
}

} // namespace

auto question_key(string_view_type p_line, string_view_type p_span,
                  string_view_type p_context,
                  const std::vector<string_type> &p_candidates) -> string_type {
  // Separators that can't appear in a line keep "a b" + "c" apart from
  // "a" + "b c".
  string_type text = string_type(p_line) + '\x1f' + string_type(p_span) +
                     '\x1f' + string_type(p_context);
  for (const auto &candidate : p_candidates)
    text += '\x1e' + candidate;
  return fnv_hash(text);
}

auto lockfile::path_for(const std::filesystem::path &p_source)
    -> std::filesystem::path {
  auto result = p_source;
  result += ".lock";
  return result;
}

auto lockfile::load(const std::filesystem::path &p_path) -> lockfile {
  lockfile result;
  std::ifstream input(p_path);
  if (!input)
    return result;
  try {
    const auto document = nlohmann::json::parse(input);
    if (document.value("ovis_lock_version", 0) != version)
      throw error(p_path.string() + " is a lockfile of the paragraph front "
                                    "end; delete it to build with phrases.");
    result.model = document.value("model", "");
    result.model_fingerprint = document.value("model_fingerprint", "");
    result.c_headers = document.value(
        "c_headers", std::map<string_type, string_type>());
    result.phrase_files = document.value(
        "phrase_files", std::map<string_type, string_type>());
    for (const auto &item : document.value("readings", nlohmann::json::array()))
      result.readings.push_back({item.at("key").get<string_type>(),
                                 item.value("span", ""),
                                 item.at("reading").get<string_type>()});
    for (const auto &item : document.value("meanings", nlohmann::json::array()))
      result.meanings.push_back({item.at("line").get<string_type>(),
                                 item.at("meaning").get<string_type>()});
  } catch (const nlohmann::json::exception &p_error) {
    throw error("Cannot read " + p_path.string() + ": " + p_error.what());
  }
  return result;
}

auto lockfile::to_text() const -> string_type {
  nlohmann::ordered_json document;
  document["ovis_lock_version"] = version;
  document["model"] = model;
  if (!model_fingerprint.empty())
    document["model_fingerprint"] = model_fingerprint;
  if (!c_headers.empty())
    document["c_headers"] = c_headers;
  if (!phrase_files.empty())
    document["phrase_files"] = phrase_files;
  document["readings"] = nlohmann::ordered_json::array();
  for (const auto &entry : readings)
    document["readings"].push_back({{"key", entry.key},
                                    {"span", entry.span},
                                    {"reading", entry.reading}});
  if (!meanings.empty()) {
    document["meanings"] = nlohmann::ordered_json::array();
    for (const auto &entry : meanings)
      document["meanings"].push_back(
          {{"line", entry.line}, {"meaning", entry.meaning}});
  }
  return document.dump(2) + '\n';
}

auto lockfile::save(const std::filesystem::path &p_path) const -> void {
  std::ofstream output(p_path);
  if (!output)
    throw error("Cannot write " + p_path.string() + ".");
  output << to_text();
}

auto lockfile::find_meaning(string_view_type p_meaning) const
    -> const meaning_entry * {
  const auto found = std::ranges::find(meanings, p_meaning, &meaning_entry::meaning);
  return found == meanings.end() ? nullptr : &*found;
}

auto lockfile::find(string_view_type p_key) const -> const reading_entry * {
  const auto found = std::ranges::find(readings, p_key, &reading_entry::key);
  return found == readings.end() ? nullptr : &*found;
}

} // namespace ovis::phrase
