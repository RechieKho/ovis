# Proposal: projects, with CMake only where C needs it

Status: **accepted** (owner's idea, 2026-10-21; the owner took every
recommendation below, 2026-10-21). Plan J1–J8.

## Why

CMake is how Ovis builds with C code a project owns
([cmake.md](cmake.md)), but it is a lot to learn before writing a first
program, and a CMakeLists.txt written once and then edited by hand leaves
every new dependency to the author. Most programs don't need it: `ovis run`
and `ovis build` already link by themselves, with system C libraries found
by pkg-config from `use the C library "zlib.h" from "z"`.

So: a **project** (`ovis.toml`) says what the program needs, C
dependencies included, and stays the one place that says it. Ovis builds
it with `cc` when it can, and otherwise **generates** the CMake project
each build from `ovis.toml`, never asking the author to keep one. Only C
that Ovis can't describe goes in the author's own CMake file, which
defines targets the generated project links. Nobody converts anything.

## 1. The project file

| Option | Pros | Cons |
| --- | --- | --- |
| **A. `ovis.toml`, a small TOML subset** (as `Cargo.toml`) | Known from Cargo and pyproject; editors highlight it; easy to read from the shell (the ovisup shim, §6) | A small parser to write (tables, strings, lists of strings) |
| B. A file in plain English (`project.ovis`) | Fits the language | A second grammar; paths and URLs read badly in English; tools can't read it |
| C. No file: a folder with `main.ovis` and flags | Nothing new | Settings live in shell history; nothing to generate CMake from |

Recommended: **A**.

```toml
[project]
name = "greeter"            # the executable's name (default: the folder's)
toolchain = "0.1.0"         # optional; the ovisup shim runs this toolchain

[build]
include = ["include"]       # -I for headers, relative to the project
defines = ["USE_FAST=1"]    # -D, while reading headers and compiling c
phrases = ["phrases"]       # --phrases (phrases/ beside main.ovis is read anyway)
c = ["c/*.c"]               # the project's own C files (§3)

# C dependencies (§4). The table's name is how `link` and errors refer to it.
[c.zlib]
pkg-config = "zlib"         # a system library

[c.cjson]
git = "https://github.com/DaveGamble/cJSON"
tag = "v1.7.18"             # a CMake project fetched at this tag
targets = ["cjson"]         # its targets the program links

[c.engine]
path = "native/engine"      # a CMake project in this repository
targets = ["engine"]

[c.local]
cmake = "native.cmake"      # the author's own CMake (§5)
targets = ["shout"]
```

Every key is optional; the smallest project is `[project]` alone. The
program is the folder (its `main.ovis` and modules), as today.

## 2. Commands

| Command | Does |
| --- | --- |
| `ovis new NAME` | makes `NAME/` with `ovis.toml`, `main.ovis` (a hello program), `.gitignore` (`/build/`), and `git init` when git is there |
| `ovis init` | the same in the current folder, keeping files that exist |
| `ovis build`, `run`, `check`, `fix`, `fmt` with no program | find `ovis.toml` here or above, build its folder with its settings; output `build/NAME` |
| `ovis add c NAME --pkg-config P` / `--git URL --tag T` / `--path DIR` | adds a `[c.NAME]` table (the file stays editable by hand) |
| `ovis cmake [--print]` | writes the generated CMake project (§4) and says where, for IDEs and debugging |

Flags on the command line add to the file's settings. `ovis run x.ovis`
on a single file works as today and ignores `ovis.toml`.

## 3. Two ways to build, chosen by what the project needs

| The project has | Ovis builds with |
| --- | --- |
| only `pkg-config` dependencies and `c = [...]` files | `cc`, as today: no CMake needed |
| any `git`, `path` or `cmake` dependency | a generated CMake project (§4); CMake must be installed (`ovisup check` says so) |

`c = [...]` is deliberately small without CMake: one list of files and
the `[build]` flags for all of them, objects in `build/c/`, rebuilt when a
file or a header it includes is newer. The same files become one static
library target in the generated project, so moving to CMake changes
nothing the author wrote.

## 4. The generated CMake project

| Option | Pros | Cons |
| --- | --- | --- |
| **A. Generated into `build/cmake/` each build from `ovis.toml`; never edited** | One source of truth; adding a dependency is a line in `ovis.toml`; the author never reads CMake unless they write some (§5) | A generated file to debug when CMake fails (`ovis cmake --print` shows it) |
| B. Generated once into the project, then the author's (the earlier draft) | Plain CMake the author owns | Every later dependency is manual CMake; `ovis.toml` and CMakeLists.txt drift apart |
| C. A CMake function that reads `ovis.toml` at configure time | No generated file | A TOML reader in CMake; harder to debug than a file one can read |

Recommended: **A**, regenerated only when `ovis.toml`, its `cmake` files'
list or the program's `use` lines change (otherwise CMake's own
up-to-date checks run). For `ovis.toml` above it writes:

```cmake
# Generated by ovis from ovis.toml. Don't edit: change ovis.toml, or the
# files it names under `cmake`.
cmake_minimum_required(VERSION 3.20)
project(greeter LANGUAGES C)
set(OVIS_PROJECT_DIR "/home/me/greeter")
find_package(Ovis REQUIRED)                      # this toolchain's prefix

find_package(PkgConfig REQUIRED)                 # [c.zlib]
pkg_check_modules(ovis_c_zlib REQUIRED IMPORTED_TARGET zlib)

include(FetchContent)                            # [c.cjson]
FetchContent_Declare(cjson GIT_REPOSITORY https://github.com/DaveGamble/cJSON
                     GIT_TAG v1.7.18)
FetchContent_MakeAvailable(cjson)

add_subdirectory(${OVIS_PROJECT_DIR}/native/engine engine)   # [c.engine]
include(${OVIS_PROJECT_DIR}/native.cmake)                    # [c.local]

add_library(greeter_c STATIC ${OVIS_PROJECT_DIR}/c/shout.c)  # c = [...]
target_include_directories(greeter_c PUBLIC ${OVIS_PROJECT_DIR}/include)
target_compile_definitions(greeter_c PUBLIC USE_FAST=1)

ovis_add_executable(greeter ${OVIS_PROJECT_DIR}
  LINK greeter_c PkgConfig::ovis_c_zlib cjson engine shout)
```

`ovis_add_executable` already gives libclang every linked target's
include directories and defines (cmake.md §3), so the program's `use`
lines read the dependencies' headers with no more settings. A `use` line
naming a library that no `[c.*]` table provides still links through
pkg-config or `-l`, as today, so small programs need no tables at all.

The generated build is `--locked` like any CMake build: `ovis build`
first settles the program's questions unlocked (as `ovis run` does), then
runs CMake, so the author never sees the difference.

**Pinning:** a `git` dependency needs a `tag` (or `commit`); the commit a
tag resolved to is kept in `ovis.lock` beside the project, and a build
whose tag now points elsewhere stops and says so (`ovis update c NAME`
moves it). This keeps invariant 1: the same commit builds the same
program.

## 5. The author's own CMake

For C that `ovis.toml` can't describe (per-file flags, generated sources,
a library found an unusual way), `[c.NAME] cmake = "FILE"` names a CMake
file of the author's. The generated project `include()`s it after the
other dependencies (so it may use them) and before the program, and links
the targets it lists:

```cmake
# native.cmake: the C this project builds its own way.
add_library(shout STATIC native/shout.c)
target_include_directories(shout PUBLIC ${OVIS_PROJECT_DIR}/native/include)
target_compile_options(shout PRIVATE -O3)
```

| Option | Pros | Cons |
| --- | --- | --- |
| **A. `targets = [...]` in `ovis.toml` says what the file gives** | All the program links is in one file; Ovis can check each target exists and say which table promised it | The target's name is written twice |
| B. The file calls `ovis_export(shout)` | Written once | The program's inputs are split between two files |

Recommended: **A**. Paths in the file are relative to `OVIS_PROJECT_DIR`
(the generated project is elsewhere); the generated header comment and
the error for a missing target say so.

## 6. The toolchain line

`toolchain = "…"` in `[project]` is remaining-tasks' "toolchain file in a
project": the ovisup shim walks up to the first `ovis.toml` and uses its
toolchain, after `+T` and `OVIS_TOOLCHAIN`, before the default. The
generated project finds that toolchain's `find_package(Ovis)` itself.

## 7. Plan

| Step | Work | Test |
| --- | --- | --- |
| J1 | `ovis.toml` reader (the subset; errors name the line) and project lookup | parse, bad keys and types, lookup from a subfolder |
| J2 | `ovis build`/`run`/`check`/`fix`/`fmt` with no program; settings merged with flags; output in `build/` | a project with `include`, `defines`, `phrases` |
| J3 | `ovis new`, `ovis init`, `ovis add c` | the new project builds and prints hello |
| J4 | `c = [...]` and `pkg-config` tables without CMake | `examples/project/`: Ovis and a C helper with zlib, no CMake |
| J5 | the generated CMake project: `path`, `cmake` and `targets`; `ovis cmake --print` | `[cmake]`: a project with a path dependency and an author's file |
| J6 | `git` dependencies, pinned in `ovis.lock`; `ovis update c` | a `file://` git repo as the dependency; a moved tag fails |
| J7 | ovisup shim reads `toolchain` | `[ovisup]`: picks the project's toolchain from a subfolder |
| J8 | docs: README "Projects" before CMake; deployment.md, c-libraries.md, architecture-spec | — |

`ovis_add_executable` in a CMake project of the author's (cmake.md) stays
for projects where CMake is the top level and Ovis one part.

## Decisions (accepted: every recommendation)

1. The project file: `ovis.toml` (§1 A)?
2. Build with `cc` when no dependency needs CMake, CMake otherwise (§3)?
3. Generate the CMake project into `build/` every time, never edited
   (§4 A)?
4. The author's own CMake file, its targets listed in `ovis.toml` (§5 A)?
5. Git dependencies pinned in a project `ovis.lock` (§4)?
