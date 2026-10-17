#ifndef OVIS_ENGLISH_PARAGRAPH_HPP
#define OVIS_ENGLISH_PARAGRAPH_HPP

#include <vector>

#include <ovis/def.hpp>

namespace ovis::english {

// The unit of translation. A program is plain text; blank lines separate
// paragraphs and that is the only structure the source has.
struct paragraph {
  std::size_t line = 1;
  string_type text;
};

auto split_paragraphs(string_view_type p_source) -> std::vector<paragraph>;

// Collapses whitespace so re-wrapping a paragraph doesn't invalidate its
// cached translation.
auto normalize(string_view_type p_text) -> string_type;

} // namespace ovis::english

#endif // OVIS_ENGLISH_PARAGRAPH_HPP
