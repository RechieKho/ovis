#ifndef OVIS_PHRASE_LOCKFILE_HPP
#define OVIS_PHRASE_LOCKFILE_HPP

#include <filesystem>
#include <map>
#include <vector>

#include <ovis/def.hpp>

// What the model decided for a phrase program, kept in `<file>.lock` next to
// it, so the program builds the same way without the model even when its
// `Read as:` lines aren't written (`--keep-source`).
namespace ovis::phrase {

struct reading_entry {
  // A hash of the question: the line, the span, what the line's names are
  // and the candidate readings. Any change to them asks again.
  string_type key;
  // For people reading the file: the span and the reading chosen.
  string_type span;
  string_type reading;

  friend auto operator==(const reading_entry &, const reading_entry &)
      -> bool = default;
};

class lockfile {
public:
  std::vector<reading_entry> readings;
  // The model that made the readings, and its weights' fingerprint.
  string_type model;
  string_type model_fingerprint;
  // C headers the program was built against, with a hash of each.
  std::map<string_type, string_type> c_headers;

  static auto path_for(const std::filesystem::path &p_source)
      -> std::filesystem::path;
  // A missing file is an empty lockfile. Throws `error` if it can't be read
  // or is the old pipeline's (version 2).
  static auto load(const std::filesystem::path &p_path) -> lockfile;
  auto save(const std::filesystem::path &p_path) const -> void;
  auto to_text() const -> string_type;

  auto find(string_view_type p_key) const -> const reading_entry *;

  friend auto operator==(const lockfile &, const lockfile &) -> bool = default;
};

// The key of a question for `reading_entry::key`.
auto question_key(string_view_type p_line, string_view_type p_span,
                  string_view_type p_context,
                  const std::vector<string_type> &p_candidates) -> string_type;

} // namespace ovis::phrase

#endif // OVIS_PHRASE_LOCKFILE_HPP
