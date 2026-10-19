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

`OVIS_UNLOCKED=ON` lets the build do what `ovis run` does. The by-value
struct wrappers (references.md R4) are not built yet.
