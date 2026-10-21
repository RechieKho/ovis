# Projects: ovis.toml

How a project is built, as built. The design and its reasons are in
[proposals/projects.md](proposals/projects.md) (J1–J8, accepted) and
[proposals/packages.md](proposals/packages.md) (H1–H10); the rest of the
compiler is in [architecture-spec.md](architecture-spec.md).

## The file

A folder with `ovis.toml` and a `main.ovis` is a project. `ovis.toml` is
read by `src/ovis/project/toml.cpp` (a TOML subset: tables, bare and
quoted keys, `"…"` and `'…'` texts, whole numbers, booleans, lists over
several lines, inline tables; no dates, floats, multi-line texts or
`[[…]]`) and checked by `project.cpp`, every error `ovis.toml, line N: …`.

| Table | Keys |
| --- | --- |
| `[project]` | `name` (the executable; default the folder's), `toolchain` (the ovisup shim's choice) |
| `[build]` | `include`, `defines`, `phrases` (folders, relative to the project), `c` (patterns of the project's C files: `*`, `?`, `**`) |
| `[c.NAME]` | one of `pkg-config` (a module, maybe `zlib >= 1.2.11`), `git` (with `tag` or `commit`), `path` (a folder with a CMakeLists.txt), `cmake` (the author's file); `targets` for all but pkg-config |
| `[package]` | `name`, `version`, `ovis` (packages.md §1) |
| `[dependencies]` | `NAME = { git = URL, version = "1.2" }` or `{ path = DIR }`; `=1.2.3` and `commit` only outside a package |
| `[overrides]` | by a dependency's name, URL or `"c.NAME"`: `git`, `path`, `tag`, `version`, `commit` |

`find_project` walks up from the current folder to the first `ovis.toml`.

## Commands (`src/main.cpp`)

- `run`, `build`, `ir`, `check`, `fix`, `fmt`, `check-phrases` with no
  program, or with a project's folder, build the project (`apply_project`):
  its include folders, defines and phrases go before the command line's
  flags; `build` writes `build/NAME` unless `-o`. A single `.ovis` file
  named builds as before, whatever project it is in.
- `ovis new NAME` / `ovis init`: `ovis.toml`, `main.ovis` (hello),
  `.gitignore` (`/build/`), `git init`; init keeps files that exist.
- `ovis cmake [--print]`: the generated CMake project, written or printed.
- `ovis update [c NAME]`: git C dependencies' tags looked up again.

## Building

| The project has | Built by |
| --- | --- |
| only pkg-config tables and `c = [...]` | `cc`: `native.cpp` compiles each C file into `build/c/<path with _>.o` with `-MMD`, again only when the file, a header in its `.d` or the flags changed; objects then pkg-config's `--libs` go before the program's own libraries on the link line |
| any `git`, `path` or `cmake` table | the generated CMake project (`cmake_project.cpp`) |

**The generated project** is written into `build/cmake/` at every build
(`CMakeLists.txt`, the embedded `OvisMacros.cmake`, the runtime's C),
each file only when its text changes; configured once into
`build/cmake-build/` (again by CMake itself when the file changes) and
built with `cmake --build`. Its order: the runtime; pkg-config, git
(FetchContent at the locked commit) and path tables; the author's
`cmake` files last, so they may use the others; a check per promised
target, citing ovis.toml's line; `c = [...]` as one static library with
the `[build]` flags; then `ovis_add_executable(NAME ROOT LINK … USE_LINKS)`
with `OVIS_UNLOCKED` on unless `--locked` and the command line's options
in `OVIS_FLAGS`. `USE_LINKS` passes `--link-flags FILE` instead of
`--link-by-caller`, and links with `@FILE`, so a `use` line's own library
(`from "m"`) is linked as by `ovis build`. CMake's output is shown only
when it fails (the log is `build/cmake.log`).

`check` and `fix` never run CMake: they read headers with `[build]`'s and
pkg-config's flags and, after a build configured the project, the
targets' flags from `build/cmake-build/NAME.ovis.rsp`.

## ovis.lock

The project's lockfile (`lock.cpp`), beside `ovis.toml` and committed;
not a file's `<file>.lock`. JSON, sorted, replaced whole and only when
it changes:

```json
{"ovis_project_lock_version": 1,
 "c": {"cjson": {"git": "https://…", "tag": "v1.7.18", "commit": "…"}},
 "packages": []}
```

A git table's tag is resolved once (`git ls-remote`, the peeled commit of
an annotated tag) and kept while `ovis.toml` asks for the same tag; a tag
moved upstream doesn't change the build until `ovis update`, which says
`v1.0.0 moved from A to B`. Under `--locked` a lock that would change is
an error.

## The toolchain line

The ovisup shim (deployment.md) and `ovisup show`/`which` take
`toolchain` from the `[project]` table of the nearest `ovis.toml`, after
`+T` and `OVIS_TOOLCHAIN`, before the default; a toolchain not installed
is named with where it came from.

## Tests

`project_test` (`[project]`: the TOML subset, tables, mistakes, versions,
lookup), `project_build_test` (new/init, no program, settings, C files
rebuilt by need, pkg-config, `examples/project`), `project_cmake_test`
(`[cmake]`: a path dependency and an author's file, a git dependency
pinned and updated, `--locked`), `ovisup_test` (the toolchain line).
