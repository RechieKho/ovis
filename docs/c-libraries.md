# C libraries (`c_import.cpp`)

How `use-c` reads a C header and calls into the library. Part of [architecture-spec.md](architecture-spec.md) §8, moved here to keep the spec under 500 lines.


- `(use-c "zlib.h" "z")` reads the header with **libclang**. Declarations
  in that header and in the library's own headers beside it (the same
  directory, unless it is a system one) are imported, and the structs its
  functions and fields reach wherever they are declared (`struct tm` for
  `gmtime`, `struct dirent` for `readdir`; references.md R6):
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
  | `struct S *` of a struct the header defines | the C record `S` |
  | struct by value, `long double` | not imported; listed in `c_library::skipped` |
- **Structs** (references.md R2): complete structs and unions the header
  defines are imported as **C records** (`c_library::records`, added to
  the program's units by `resolve_c_imports`, and to the front end's
  records): the struct's tag, or a typedef's name for an unnamed one.
  Fields are at clang's offsets (`record_field::offset`): C numbers read
  and written through their C type (`storage`), `char *` as text,
  `char[N]` as text inside the struct (read where it is; set by copying
  at most N−1 bytes, `ovis_c_text_set`), a struct held inside as the
  record at that place (`inline_record`), a pointer to a struct as that
  record. Bitfields and other arrays are skipped (listed). A function's
  `struct S *` parameter or result is the record S (`c_function::records`,
  `result_record`); null and other pointers may still be given. `a new S`
  (Core `(new S)`) is zeroed, fields left out; a C record has no hidden
  tag, so it can't implement a trait; `keep`/`a copy of` copy its bytes;
  printing doesn't follow pointers to other structs.
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

## Memory C returns

Memory a C function returns is the author's, and the same `free` frees it:
`copy = strdup(name)` then `at the end, free copy`. C results are not new
values to the strict memory rules (the proposal's §2): Ovis can't tell
`strdup` from `getenv`. Checked builds know which pointers Ovis made, so
`free` never mixes the two up: a pointer Ovis made is freed as the value it
is, any other goes to C's `free`. C pointers into Ovis text are valid while
the text is.

## Building with CMake

Design: [proposals/cmake.md](proposals/cmake.md). `find_package(Ovis)`
(after `cmake --install BUILD --component ovis --prefix PREFIX`, with
`CMAKE_PREFIX_PATH` set to PREFIX) gives `ovis::runtime`, the runtime's C
source built with the project's compiler, and `ovis_add_executable(NAME
PROGRAM [LINK TARGET…])`, which:

1. writes a response file (`NAME.ovis.rsp`) with `-I` and `-D` from the
   `LINK` targets' include directories and definitions, transitively;
2. runs `ovis build PROGRAM --object NAME.ovis.o --link-by-caller --locked
   --depfile NAME.ovis.d @NAME.ovis.rsp`, rerun by CMake when a `.ovis`
   file, a lockfile or a header libclang read changes;
3. links the object with the `LINK` targets and `ovis::runtime`, so static
   or shared, dependencies and rpath are the targets' own.

`examples/cmake/` is a C static library and an Ovis program using it,
built from an installed Ovis in CI and by the `[cmake]` tests.

The flags `ovis` takes for this:

| Flag | Meaning |
| --- | --- |
| `--object FILE` | `build` stops at the object file |
| `--link-by-caller` | no pkg-config, no `-l`; headers are still read |
| `--locked` | never asks the model or writes; fails naming the line (a meaning to write into the source) or the lockfile that would change. A program that uses C headers needs its lockfile (the headers' hashes) from one `ovis run` |
| `--depfile FILE` | Make-style: the object, after the `.ovis` files, lockfiles and the non-system headers read |
| `@FILE`, `-DNAME` | response files; macros for reading headers |
| `ovis runtime [-o FILE]` | the runtime's C source |

On macOS libclang is given its resource directory (found beside it at
build time, `OVIS_CLANG_RESOURCE_DIR`, else `clang -print-resource-dir`)
and the SDK (`-isysroot`), so the SDK's headers read.

`OVIS_UNLOCKED=ON` lets the build do what `ovis run` does. The by-value
struct wrappers (references.md R4) are not built yet.
