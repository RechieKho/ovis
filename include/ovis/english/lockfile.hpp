#ifndef OVIS_ENGLISH_LOCKFILE_HPP
#define OVIS_ENGLISH_LOCKFILE_HPP

#include <filesystem>
#include <optional>
#include <vector>

#include <ovis/def.hpp>

namespace ovis::english {

// Remembers what every paragraph was translated into. With it a program
// builds the same way every time and without a language model, and it shows
// (and lets you fix) exactly what the model understood.
class lockfile {
public:
  struct entry {
    string_type english;
    string_type core;
    // Only for people reading the lockfile; worked out again on every build.
    std::vector<string_type> assumptions;

    friend auto operator==(const entry &, const entry &) -> bool = default;
  };

  std::vector<entry> entries;
  string_type model;
  // Tells apart translations made by different weights with the same name.
  string_type model_fingerprint;

  static auto path_for(const std::filesystem::path &p_source)
      -> std::filesystem::path;
  // A missing file is an empty lockfile.
  static auto load(const std::filesystem::path &p_path) -> lockfile;
  auto save(const std::filesystem::path &p_path) const -> void;

  auto find(string_view_type p_english) const -> std::optional<string_type>;
};

} // namespace ovis::english

#endif // OVIS_ENGLISH_LOCKFILE_HPP
