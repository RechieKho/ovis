#ifndef OVIS_PROJECT_TOML_HPP
#define OVIS_PROJECT_TOML_HPP

#include <cstddef>
#include <vector>

#include <ovis/def.hpp>

// The part of TOML that `ovis.toml` uses (proposals/projects.md §1):
// tables (`[c.zlib]`), keys (bare or quoted), basic and literal strings,
// whole numbers, booleans, arrays (over several lines too) and inline
// tables. Dates, floats, multi-line strings and arrays of tables are not
// read: a file that has them gets an error naming the line.
namespace ovis::project {

struct toml_entry;

class toml_value {
public:
  enum class kind_type { string, integer, boolean, array, table };

  kind_type kind = kind_type::table;
  string_type text;
  int64_type integer = 0;
  bool boolean = false;
  std::vector<toml_value> items;
  // A table's keys in the order the file gives them.
  std::vector<toml_entry> entries;
  // Where the value (or a table's header) is, for messages.
  std::size_t line = 0;

  auto find(string_view_type p_key) const -> const toml_value *;
  auto kind_name() const -> string_view_type;
};

struct toml_entry {
  string_type key;
  toml_value value;
};

// Reads `p_text`; errors are `FILE, line N: …`.
auto parse_toml(string_view_type p_text, string_view_type p_file_name)
    -> toml_value;

// A key as TOML writes it: bare when it can be, else quoted.
auto toml_key(string_view_type p_key) -> string_type;
// A basic string with its escapes.
auto toml_string(string_view_type p_text) -> string_type;

} // namespace ovis::project

#endif // OVIS_PROJECT_TOML_HPP
