# C libraries (`c_import.cpp`)

How `use-c` reads a C header and calls into the library. Part of [architecture-spec.md](architecture-spec.md) §8, moved here to keep the spec under 500 lines.


- `(use-c "zlib.h" "z")` reads the header with **libclang**. Only
  declarations written in that header itself are imported:
  - functions;
  - enum values;
  - `#define`s that are a plain number (`N`, `-N`, `(N)`, `(-N)`, with
    suffixes).
- **Type mapping:**

  | C type | Ovis |
  | --- | --- |
  | 8/16/32-bit and unsigned integers, enums, `long`, `size_t` | `int`. Narrower values are sign- or zero-extended as their C type says. |
  | `float`, `double` | `float` |
  | `char *` (signed or unsigned) | `str` |
  | any other pointer (handles, `T **`, callbacks) | `ptr`; only a handle or `null` can be given for a callback |
  | struct by value, `long double` | not imported; listed in `c_library::skipped` |
- **Mixing text and pointers:** text may be passed where C takes a data
  pointer, and `null` where C takes text.
- **`pointees`:** for pointers to a single value (`int *`, `double *`,
  `T **`) the importer records the value's type. `(ref x)` then passes `x`
  directly when the sizes match. Otherwise it goes through a temporary of
  the C type, which is copied back after the call. Inference also uses the
  pointee's type for `x`.
- **Flags:** from `pkg-config` (the library name, then the header's name),
  falling back to `-lLIBRARY`. Headers are searched in `-I` directories,
  pkg-config's, then system directories. `-L` adds library directories.
- Imports are cached per process. They are resolved on every evaluation.
