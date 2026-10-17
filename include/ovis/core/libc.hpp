#ifndef OVIS_CORE_LIBC_HPP
#define OVIS_CORE_LIBC_HPP

#include <optional>
#include <set>
#include <span>
#include <vector>

#include <ovis/core/sexpr.hpp>
#include <ovis/def.hpp>

// What Ovis Core knows about C: the C standard library functions it can call
// directly, the constants it can use, and how C types map to Ovis types.
namespace ovis::core {

struct value_type;

enum class c_type {
  void_type,
  int_type,    // int, 32 bits
  long_type,   // long / time_t / clock_t, 64 bits
  size_type,   // size_t
  double_type, // double
  float_type,  // float
  string_type, // char *
  pointer_type // void *, FILE *
};

struct c_function {
  string_type name;
  c_type result = c_type::void_type;
  std::vector<c_type> parameters;
  bool is_variadic = false;
  // Index of the printf-style format argument, if there is one.
  std::optional<std::size_t> format_index;
  // Library to link (`m`, ...) or empty for libc itself.
  string_type library;
  source_location location;
};

struct c_constant {
  enum class kind_type { integer, real, null, runtime };

  string_type name;
  kind_type kind = kind_type::integer;
  max_int_type integer = 0;
  max_float_type real = 0;
  // For `runtime` constants: the runtime function returning the value.
  string_type getter;
};

auto ovis_type_of(c_type p_type) -> value_type;
// C type spellings accepted in `(extern ...)`.
auto parse_c_type(string_view_type p_name) -> std::optional<c_type>;

auto libc_functions() -> std::span<const c_function>;
auto find_libc_function(string_view_type p_name) -> const c_function *;
auto find_c_constant(string_view_type p_name) -> const c_constant *;

// Operations built into Ovis Core, by canonical name.
auto builtin_names() -> const std::set<string_view_type> &;
auto is_builtin(string_view_type p_name) -> bool;

// One conversion of a printf format string, e.g. `%5.2f`.
struct format_conversion {
  std::size_t begin = 0; // index of '%'
  std::size_t end = 0;   // one past the conversion character
  char_type conversion = 0;
  bool takes_star_width = false;
};

// Conversions that consume an argument (`%%` is skipped).
auto parse_format(string_view_type p_format) -> std::vector<format_conversion>;

} // namespace ovis::core

#endif // OVIS_CORE_LIBC_HPP
