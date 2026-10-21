# Proposal: projects without CMake, and turning them into CMake

Status: **proposed** (owner's idea, 2026-10-21); decisions below are
recommendations, not yet agreed. Plan J1–J7.

## Why

CMake is how Ovis builds with C code a project owns
([cmake.md](cmake.md)), but it is a lot to learn before writing a first
program. Most programs don't need it: `ovis run` and `ovis build` already
link by themselves, with system C libraries found by pkg-config from `use
the C library "zlib.h" from "z"`. What a newcomer lacks is a **project**:
a folder that `ovis new` makes, that `ovis build` and `ovis run` build with
no arguments, that remembers its settings (`-I`, `-L`, `--phrases`, the
output's name, the toolchain), and that becomes a CMake project with one
command the day it needs a C project of its own.

So CMake stays optional: a project needs it only to build C code with its
own build (a library's CMakeLists.txt, a vendored project, cross-compiling).

## 1. The project file

| Option | Pros | Cons |
| --- | --- | --- |
| **A. `ovis.toml`, a small TOML subset** (as `Cargo.toml`) | Known from Cargo and pyproject; editors highlight it; easy to read from the shell (the ovisup shim, §5) and to generate CMake from | A small parser to write (tables, strings, lists of strings, no dates or inline tables) |
| B. A file in plain English (`project.ovis`: "This program is called greeter.") | Fits the language | A second grammar; settings like `-I` dirs read badly in English; tools can't read it |
| C. No file: convention only (a folder with `main.ovis`) and flags | Nothing new | Settings live in the user's shell history; nothing for the shim or the CMake conversion to read |

Recommended: **A**.

```toml
[project]
name = "greeter"          # the executable's name (default: the folder's)
toolchain = "0.1.0"       # optional; the ovisup shim runs this toolchain

[build]
include = ["include"]     # -I, relative to the project
libraries = ["/opt/x/lib"]  # -L
defines = ["USE_FAST=1"]  # -D, while reading headers
phrases = ["phrases"]     # --phrases (phrases/ beside main.ovis is read anyway)
c = ["c/*.c"]             # the project's own C files (§3)
```

Every key is optional, so the smallest project is `[project]` alone, or an
empty file. The program is the folder (its `main.ovis` and modules), as
today; a `src/` layout is not needed.

## 2. Commands

| Command | Does |
| --- | --- |
| `ovis new NAME` | makes `NAME/` with `ovis.toml`, `main.ovis` (a hello program), `.gitignore` (`/build/`), and `git init` when git is there |
| `ovis init` | the same in the current folder, keeping files that exist |
| `ovis build`, `run`, `check`, `fix`, `fmt` with no program | find `ovis.toml` in this folder or above it, and build its folder with its settings; output `build/NAME` |
| `ovis cmake` | writes `CMakeLists.txt` from `ovis.toml` (§4) |

Flags on the command line add to the file's settings. A program named on
the command line (`ovis run x.ovis`) works as today and ignores
`ovis.toml`, so single files stay the quickest start.

## 3. C files of the project's own, without CMake

| Option | Pros | Cons |
| --- | --- | --- |
| **A. `c = [...]` files compiled by `cc` and linked in** | A helper `.c` file needs no CMake; Ovis already runs `cc` (linking, the struct wrappers); headers beside them are read with the same `-I`/`-D` | Ovis grows a tiny C build: no per-file flags, no dependency tracking beyond "rebuild when changed" |
| B. Any C code means CMake | One way to build C | Converting for one 20-line `.c` file is the hurdle this proposal removes |

Recommended: **A**, deliberately small: one list of files and the
`[build]` flags for all of them, objects in `build/c/`, rebuilt when the
file or a header it includes is newer. Anything more (a C library with its
own build, per-target flags, static vs. shared, other languages) is the
signal to run `ovis cmake`.

## 4. Turning a project into CMake

| Option | Pros | Cons |
| --- | --- | --- |
| **A. `ovis cmake` generates a `CMakeLists.txt` once; CMake builds from then on** | The file is plain, readable CMake the author owns and extends (`add_subdirectory`, `FetchContent`); nothing hidden | Settings changed in `ovis.toml` afterwards don't reach CMake (`ovis cmake --force` regenerates, warning that edits are lost) |
| B. `ovis_add_project(.)` in CMake reads `ovis.toml` every configure | One source of truth stays | A second way to describe targets inside CMake; newcomers read a file that hides what it does |

Recommended: **A**. Ovis writes it because Ovis knows what the program
uses: it reads the program and turns each C library of a `use` line into
a CMake target, so `--link-by-caller` builds still link it.

```cmake
# Made by `ovis cmake` from ovis.toml. Yours to edit from now on.
cmake_minimum_required(VERSION 3.20)
project(greeter LANGUAGES C)

find_package(Ovis REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(zlib REQUIRED IMPORTED_TARGET zlib)   # use … from "z"

add_library(greeter_c STATIC c/shout.c)                 # c = ["c/*.c"]
target_include_directories(greeter_c PUBLIC include)
target_compile_definitions(greeter_c PUBLIC USE_FAST=1)

ovis_add_executable(greeter . LINK greeter_c PkgConfig::zlib)
```

A library pkg-config doesn't know becomes `find_library(… NAMES z)` with
an imported target. The lockfile is committed already, so the CMake build
(locked) needs no model. After conversion `ovis.toml` keeps `[project]`
(name, toolchain); `ovis cmake` removes `[build]`, or comments it out,
so there is one place that says how to build.

`ovis build` and `ovis run` in a project with a `CMakeLists.txt` run
`cmake -S . -B build` (once) and `cmake --build build`, then run
`build/NAME`: the commands a newcomer learned keep working after the
switch. A project that wants plain `cmake` uses it directly.

## 5. The toolchain line

`toolchain = "…"` in `[project]` is remaining-tasks' "toolchain file in a
project": the ovisup shim walks up from the current folder to the first
`ovis.toml` and uses its toolchain, after `+T` and `OVIS_TOOLCHAIN` and
before the default. A separate `ovis-toolchain` file isn't needed.

## 6. Plan

| Step | Work | Test |
| --- | --- | --- |
| J1 | `ovis.toml` reader (the subset; errors name the line) and project lookup from the current folder | parse, bad keys and types, lookup from a subfolder |
| J2 | `ovis build`/`run`/`check`/`fix`/`fmt` with no program; settings merged with flags; output in `build/` | a project with `include`, `defines`, `phrases` builds and runs |
| J3 | `ovis new`, `ovis init` | the new project builds and prints hello; `init` keeps existing files |
| J4 | `c = [...]`: compile with `cc`, rebuild when changed, link | `examples/project/`: an Ovis program and a C helper, no CMake |
| J5 | `ovis cmake`: generate from `ovis.toml` and the program's `use` lines | `[cmake]` test: convert `examples/project/`, build with CMake, same output |
| J6 | `ovis build`/`run` drive CMake when `CMakeLists.txt` exists | the converted example via `ovis run` |
| J7 | ovisup shim reads `toolchain`; docs (README "Projects" first, CMake after it; deployment.md; c-libraries.md) | `[ovisup]`: shim picks the project's toolchain from a subfolder |

## Decisions to make

1. The project file: `ovis.toml` (§1 A)?
2. Own `.c` files without CMake (§3 A), or C always means CMake (§3 B)?
3. Convert once by generating `CMakeLists.txt` (§4 A), or CMake reads
   `ovis.toml` (§4 B)?
4. After conversion, `ovis build`/`run` drive CMake (§4)?
