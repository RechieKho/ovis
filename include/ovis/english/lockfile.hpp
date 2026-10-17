#ifndef OVIS_ENGLISH_LOCKFILE_HPP
#define OVIS_ENGLISH_LOCKFILE_HPP

#include <filesystem>
#include <optional>
#include <vector>

#include <ovis/def.hpp>

namespace ovis::english {

// What the detail pass made of a paragraph.
struct detail_result {
  // `mismatch`: steps the author edited no longer do what the paragraph says.
  enum class status_type { ok, note, too_vague, irrelevant, mismatch };

  status_type status = status_type::ok;
  std::vector<string_type> steps;
  std::vector<string_type> assumptions;
  // For too_vague, irrelevant and mismatch.
  string_type reason;
  string_type suggestion;

  auto is_refusal() const -> bool {
    return status == status_type::too_vague ||
           status == status_type::irrelevant ||
           status == status_type::mismatch;
  }

  friend auto operator==(const detail_result &, const detail_result &)
      -> bool = default;
};

auto status_name(detail_result::status_type p_status) -> string_view_type;

// Remembers what the model made of every paragraph, so a program builds the
// same way every time and without the model:
// - details: the author's text -> the detail pass's verdict and steps;
// - translations: steps -> Ovis Core.
class lockfile {
public:
  struct detail_entry {
    string_type english;
    detail_result detail;

    friend auto operator==(const detail_entry &, const detail_entry &)
        -> bool = default;
  };
  struct translation_entry {
    string_type steps;
    string_type core;

    friend auto operator==(const translation_entry &,
                           const translation_entry &) -> bool = default;
  };

  std::vector<detail_entry> details;
  std::vector<translation_entry> translations;
  string_type model;
  // Tells apart translations made by different weights with the same name.
  string_type model_fingerprint;

  static auto path_for(const std::filesystem::path &p_source)
      -> std::filesystem::path;
  // A missing file is an empty lockfile.
  static auto load(const std::filesystem::path &p_path) -> lockfile;
  auto save(const std::filesystem::path &p_path) const -> void;
  auto to_text() const -> string_type;

  auto find_detail(string_view_type p_english) const -> const detail_entry *;
  // The entry a block of steps was most likely written from: the one sharing
  // the most steps with it, if at least half of them. Edited steps still
  // find their origin this way.
  auto find_origin(const std::vector<string_type> &p_steps) const
      -> const detail_entry *;
  auto find_translation(string_view_type p_steps) const
      -> std::optional<string_type>;
};

} // namespace ovis::english

#endif // OVIS_ENGLISH_LOCKFILE_HPP
