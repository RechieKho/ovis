#ifndef OVIS_CORE_LIBC_HPP
#define OVIS_CORE_LIBC_HPP

#include <algorithm>
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
  int8_type,   // signed char
  uint8_type,  // unsigned char, _Bool
  int16_type,  // short
  uint16_type, // unsigned short
  int_type,    // int, 32 bits
  uint_type,   // unsigned int
  long_type,   // long / long long / time_t / clock_t, 64 bits
  size_type,   // size_t
  double_type, // double
  float_type,  // float
  string_type, // char *, unsigned char *
  pointer_type // any other pointer: void *, FILE *, struct handles, T **
};

auto is_unsigned(c_type p_type) -> bool;

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
  // For each pointer parameter that points to a single value (`int *`,
  // `double *`, `T **`), the type of that value, so `(ref x)` can pass an
  // Ovis variable through it. Empty when not known.
  std::vector<std::optional<c_type>> pointees;
  // The header's comment for the function, if any; shown to the model.
  string_type comment;
  // A pointer to a struct the header defines is that C record (its name per
  // parameter, empty for others; and for the result).
  std::vector<string_type> records;
  string_type result_record;
  // A pointer to a function (a callback): its C phrase signature, `(i32
  // ptr) i32` (references.md §7), per parameter and for the result.
  std::vector<string_type> phrases;
  string_type result_phrase;
  // Structs passed or returned by value (references.md R4): Ovis calls a C
  // wrapper, `ovis_w_NAME`, that takes them by pointer and gives back a new
  // record. The C types as the header spells them, the header and the
  // flags to compile the wrapper with.
  std::vector<bool> by_value;
  bool result_by_value = false;
  std::vector<string_type> spellings;
  string_type result_spelling;
  string_type header_path;
  std::vector<string_type> compile_flags;

  // A function-like macro that is one call (`deflateInit(strm, level)`):
  // called through a wrapper that expands it, typed by the function it
  // calls.
  bool is_macro = false;
  // A function-like macro of one C expression (`MAX(a, b)`), as Zig's
  // translate-c reads one: its parameters have no types (`parameters` holds
  // one placeholder each), so each call takes the variant that fits its
  // arguments, which the elaborator picks. A variant is named for the
  // types it takes (`MAX__ii`: whole numbers; `f` a decimal, `s` text) and
  // expands `macro_name` in its wrapper, written only if a call uses it.
  std::vector<c_function> variants;
  string_type macro_name;
  mutable bool is_used = false;

  auto is_generic_macro() const -> bool { return !variants.empty(); }
  auto needs_wrapper() const -> bool {
    return result_by_value || is_macro ||
           std::ranges::find(by_value, true) != by_value.end();
  }
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
// A C function's parameter and result as Ovis types: a pointer to a struct
// the header defines is that C record.
auto c_parameter_type(const c_function &p_function, std::size_t p_index)
    -> value_type;
auto c_result_type(const c_function &p_function) -> value_type;
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
