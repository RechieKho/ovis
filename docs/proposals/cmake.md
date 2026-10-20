# Proposal: building with CMake

Status: **accepted** (decisions at the end), not yet built; the plan is
K1–K6 below. It settles the P2 task "static linking when a `.a` exists"
and gives the by-value struct wrappers of [references.md](references.md)
§2 a place to be compiled.

## Why

A program that uses its own C code needs that code built first, with the
right compiler and flags for the platform. CMake already does this, and
knows each C target's include directories, defines, dependencies and
whether it is static or shared. Ovis today links by itself: it runs `cc`
with `-l` flags from `use` lines and pkg-config, so a project's own C
library, its archive's dependencies, rpath and cross-compiling are all
the author's problem.

## 1. How Ovis learns what the C library provides

| Option | Pros | Cons |
| --- | --- | --- |
| **A. Headers through libclang, as today**, with the flags of the CMake target | Exact types, enums, constants, comments; nothing new for authors; the lockfile's header hashes keep working | The header must be read with the flags C was compiled with |
| B. Symbols of the `.a` (`nm`) | No headers | Names only: C symbols carry no types |
| C. DWARF from a `-g` build | Types without headers | No `#define`s or inline functions; needs a debug build; a large reader |
| D. A description file from `ovis import` in a CMake step | Fast, cacheable | An extra file; a cache of A |
| E. `(extern …)` written by hand | No tooling | Wrong signatures fail silently |

Decided: **A**. `use "mylib.h"` stays the author's way to say what is
used. The CMake module (§3) writes the target's include directories and
defines (`INTERFACE_INCLUDE_DIRECTORIES`, `INTERFACE_COMPILE_DEFINITIONS`,
transitively, through generator expressions) to a response file that
`ovis` reads with `@FILE`, so libclang reads the header as the C compiler
did. pkg-config stays for libraries outside CMake.

## 2. Who links

| Option | Pros | Cons |
| --- | --- | --- |
| A. Ovis links, given the `.a` | Ovis keeps control | Ovis must redo what CMake does: the archive's own dependencies, link order, rpath, platform flags |
| **B. Ovis writes an object file; CMake links it** | CMake handles static or shared, transitive dependencies, rpath, cross-compiling and the compiler; references.md's wrappers are C sources in the target | Ovis needs an object-only output, and its runtime as a library |

Decided: **B**. Under CMake, what is linked is whatever the target links
(`target_link_libraries`), so static or shared is CMake's choice, and the
library name in `use "x.h" "lib"` is optional (it is ignored with a
`--link-by-caller` build, §4). `ovis run` and `ovis build` without CMake
link as today.

## 3. The CMake module

`find_package(Ovis)` gives `ovis::runtime` (the C runtime as a static
library, built from `runtime_source()`) and `ovis_add_executable`:

```cmake
find_package(Ovis REQUIRED)
add_library(mylib STATIC src/mylib.c)
target_include_directories(mylib PUBLIC include)

ovis_add_executable(app main.ovis LINK mylib)
```

which does:

1. `file(GENERATE)` a response file with `-I`/`-D` from the `LINK`
   targets' interface properties.
2. `add_custom_command`: `ovis build --object app.o --locked
   --link-by-caller --depfile app.d @app.rsp main.ovis`, with `DEPFILE`
   so CMake reruns it when an input changes. It also writes the C
   wrappers for structs by value, if any (`app.wrappers.c`).
3. `add_executable(app app.o [app.wrappers.c])` and
   `target_link_libraries(app PRIVATE mylib ovis::runtime m)`.

A folder program (`main.ovis` with helpers) is given as the folder.
`ovis_add_library` (Ovis code called from C) is left for later: it needs
exported phrases.

## 4. What `ovis` needs

| Flag | Meaning |
| --- | --- |
| `--object FILE` | stop after the object file; don't link |
| `--depfile FILE` | write a Make-style dependency file: the `.ovis` files, the headers libclang read, the lockfiles and meanings (only what was read) |
| `--locked` | fail if the build would ask the model or change a source or lockfile; the error names the line and suggests `ovis run` or `ovis fix`. It is `--offline --keep-source` that fails instead of writing |
| `--link-by-caller` | don't resolve link flags (no pkg-config `--libs`, no `-l`); headers are still read |
| `@FILE` | read more arguments from FILE (CMake's response files) |

## 5. Builds inside CMake are locked

| Option | Pros | Cons |
| --- | --- | --- |
| **A. Locked by default** (`--locked`); `OVIS_UNLOCKED` turns it off | A build never edits the source tree or needs the model; CI is reproducible from the committed lockfile (invariant 1) | A new unclear line fails the CMake build until `ovis run` or `ovis fix` settles it |
| B. Unlocked, as `ovis run` | One step | CMake builds edit sources; parallel builds race on lockfiles; CI needs the model |

Decided: **A**.

## 6. Plan

| Step | Work | Test |
| --- | --- | --- |
| K1 | `--object`, `--link-by-caller`, `@FILE` | build an object and link it with `cc` and the runtime by hand |
| K2 | `--locked` | a changed line fails with its location; an unchanged program builds and writes nothing |
| K3 | `--depfile` | the file lists the sources, headers and lockfile |
| K4 | `ovis::runtime` library and `install()` / `OvisConfig.cmake` | `find_package(Ovis)` from an installed tree |
| K5 | `ovis_add_executable` | `examples/cmake/`: a C static library and an Ovis program using it, built with CMake in CI on Linux and macOS |
| K6 | docs: c-libraries.md, architecture-spec §8, a README section | — |

The by-value wrappers (references.md R4) emit `app.wrappers.c` once K5
exists, and compile with `cc` otherwise.

## Decisions

Agreed with the owner (2026-10-19), as recommended: headers through
libclang with the CMake target's flags (§1 A); Ovis writes an object and
CMake links (§2 B); builds under CMake are locked by default (§5 A).
