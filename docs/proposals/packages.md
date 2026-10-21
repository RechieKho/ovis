# Proposal: packages hosted anywhere git is

Status: **proposed** (owner's idea, 2026-10-21); decisions below are
recommendations, not yet agreed. Plan H1–H10. Builds on
[projects.md](projects.md) (`ovis.toml`, `ovis.lock`, J1, J2, J6).

## Why

Programs will want phrases other people wrote: a JSON reader, a CSV
writer, a game loop. Today the only way to share Ovis code is to copy the
files into a program folder. A central registry is a service someone
must run, moderate and pay for, and a single place a name can be
squatted. Git is already everywhere: GitHub, GitLab, Codeberg, a company
server, a folder on disk. So **a package is a git repository, its
versions are its tags, and its URL is its name**, as in Go. Nothing has
to be hosted for Ovis itself.

Three things are particular to Ovis and shape the design:

1. **A package's lines are read, not just compiled.** An unclear line in
   a dependency must not reach the user's model: a package ships its
   meanings and lockfiles, and is always built `--locked` (§6).
2. **A dependency's phrases change how the user's lines read.** A new
   phrase can make one of the user's lines a question, or a different
   reading the better one. So versions move only when asked (§3), and only
   a package's direct dependencies' phrases are in scope (§5).
3. **Generics are copied per use** (generics.md). A generic from a
   dependency is copied with the user's types, so a package is source, not
   a compiled library (§7).

## 1. A package

A package is an Ovis project (projects.md §1) with a `[package]` table,
at the root of a git repository or in a subfolder of one:

```toml
[package]
name = "csv"                    # what users write by default (§5)
version = "1.4.0"               # must match the tag (§2)
ovis = "0.3"                    # the oldest toolchain that builds it

[dependencies]
text-tools = { git = "https://codeberg.org/ana/text-tools", version = "2.1" }
log = { git = "https://github.com/ovis-libs/log", version = "1.2" }

[c.zlib]                        # C dependencies as in projects.md §4
pkg-config = "zlib"
```

Its modules are its `.ovis` files, as in a program folder, without
`main.ovis` (a package with one is also a program, its example or tool).
Everything its files define is exported, except files under `internal/`
(Go's rule): records, traits, phrases and constants a package needs but
doesn't promise to keep.

What is published is the tree at the tag: the `.ovis` files, their
`.lock` files and `@[meaning: …]` sigils, `phrases/`, `ovis.toml`. Nothing
is built in advance.

## 2. Versions are tags

| Option | Pros | Cons |
| --- | --- | --- |
| **A. Semantic-version tags `vX.Y.Z`** (`sub/vX.Y.Z` for a package in a subfolder, as Go) | No service; what every git host shows; ordered | Tags can be moved or deleted (§8 handles it) |
| B. Branches | Easy to follow | Not versions: no order, move all the time |
| C. A versions file in the repository listing commits | Any naming | One more file to keep right; two sources of truth |

Recommended: **A**. `version` in `ovis.toml` must equal the tag
(`ovis release` checks, §9). Versions are listed with `git ls-remote
--tags URL`: one round trip, no clone.

A dependency names a **version line**, not a range: `version = "1.2"`
means "1.2.0 or any later 1.x". Before 1.0, the minor is the line:
`"0.3"` means 0.3.x only (Cargo's rule), so 0.3 and 0.4 are as different
as 1 and 2. Exact pins (`"=1.2.3"`) and commits (`commit = "…"`) are
allowed only in the root project and in overrides (§4): a published
package that pinned would force its exact choice on everyone above it.

## 3. Choosing versions: one per line, the least that satisfies

| Option | Pros | Cons |
| --- | --- | --- |
| **A. Minimal version selection** (Go): for each package and line, the highest of the minimums asked for | Deterministic without a lockfile; never picks a version nobody asked for; no solver; the same answer on every machine and day | Doesn't take new fixes until someone asks (`ovis update`) |
| B. Newest compatible (Cargo, npm) | Fixes arrive by themselves | Needs the lockfile to be reproducible; a new release can change how the user's lines read on the next fresh resolve |
| C. A full solver over ranges (pip, Bundler) | Expressive | Slow, can fail with errors nobody understands; ranges with upper bounds rot |

Recommended: **A**, because of point 2 under Why: with Ovis a newer
dependency isn't only new code, it can be new readings of the user's own
lines, so it should arrive only when the user asks. `ovis.lock` still
records the result (§8) so builds need no network, and `ovis update`
raises the minimums on purpose.

The walk: start from the root's dependencies; for each `(URL, line)`
keep the highest minimum seen; read that version's `ovis.toml` and add
its dependencies; repeat until nothing rises. A version that rises is
re-read, and versions only rise, so the walk ends. Every version read is
one `git` fetch of one tag's tree (§8 caches them).

## 4. The same package at different versions

The case that decides a package system. `app` depends on `csv` and
`report`; both depend on `log`:

| `csv` asks | `report` asks | Result |
| --- | --- | --- |
| `log 1.2` | `log 1.4` | **one** `log`: 1.4.0 (the highest minimum). `csv` gets a newer `log` than it asked for, which the 1.x line promises is fine |
| `log 1.2` | `log 2.0` | **two** `log`s, 1.2.0 and 2.0.0, as two packages (`log@1`, `log@2`) |
| `log 0.3` | `log 0.4` | two, as above: before 1.0 the minor is the line |
| `log 1.2` (one URL) | `log 1.2` (a fork's URL) | two different packages: a fork is another URL; the root makes them one with an override |
| `log commit abc` | anything | refused: only the root pins commits |

**One version per line** keeps the program small and, more importantly,
means a `log` value made by `csv` is the same type `report` takes. Only
across lines do two copies exist, and then they are kept apart
completely:

- **Names.** Each package's definitions get the package's identity in
  their Core names (`ovis.pkg.<hash of URL and line>.write`), so `log@1`
  and `log@2` never clash, in Core or when linked.
- **Types.** A record or trait from `log@1` is not the one from `log@2`,
  even with the same fields. Passing one where the other is wanted is a
  build error that names both and why there are two:
  `report's "add (e: entry)" takes an entry from log 2.0, but this entry
  is from log 1.2 (csv uses log 1.2, report uses log 2.0)`. The fix is
  the authors' (moving `csv` to log 2), or the root's (an override, below).
- **Phrases.** Only a package's *direct* dependencies' phrases are read
  in its lines (§5), so `log@1`'s phrases and `log@2`'s never compete in
  one file unless a project depends on both itself, under two names.
- **Globals.** Each copy has its own top-level variables (a logger's
  settings in `log@1` don't reach `log@2`). Said in the docs; it is what
  "two packages" means.
- **Generics.** A generic copied with the same types for two dependents
  is one copy (copies are named by the package, the generic and the
  types, and emitted once, `linkonce_odr`).

**Traits across packages.** Two packages may both make `a whole number
is hashable:` for a trait neither owns; together they'd conflict. So, as
Rust's orphan rule: a package may implement a trait for a type only if
it defines the trait or the type. The root project is held to the same
rule, so adding a dependency can never break an impl.

**C libraries are one per program.** C symbols are global, so two
versions of a C library can't be linked together. A `[c.*]` dependency
(projects.md §4) is shared by URL across all packages: `pkg-config` ones
are whatever the system has (each package may state a minimum,
`pkg-config = "zlib >= 1.2.11"`, checked); `git` ones must agree on the
tag, or the build stops naming who asked for what, and the root decides
with an override.

**Overrides**, the root's last word (Cargo's `[patch]`, Go's `replace`):

```toml
[overrides]
log = { git = "https://github.com/me/log-fixed", tag = "v1.4.1" }   # a fork, everywhere
"https://github.com/ovis-libs/log" = { path = "../log" }            # a local checkout
"c.cjson" = { tag = "v1.7.18" }                                     # one C version for all
```

An override replaces a package for the whole graph, at one version, so
a fork and its original become one package. Overrides in dependencies are
ignored (with a note), as Cargo does.

## 5. Names in the program

```toml
[dependencies]
csv = { git = "https://github.com/ovis-libs/csv", version = "1.4" }
oldlog = { git = "https://github.com/ovis-libs/log", version = "1.2" }
log = { git = "https://github.com/ovis-libs/log", version = "2.0" }
```

The key is the package's name in this project (its `[package] name`
when it matches; any name when two would clash, as above). A dependency's
exported phrases join the phrases the project's lines are read with,
exactly as a helper file's do now: `read the table in "data.csv"` just
works. When two dependencies give phrases that fit a line, it is a
question like any other, settled once and written as a meaning.

| Option for writing which package a meaning means | Pros | Cons |
| --- | --- | --- |
| **A. The package's name before the phrase, with `'s`**: `@[meaning: log's write "hi"]` | Reads as English; `'s` is already "belongs to" | `'s` also reads fields; the parser must try packages first (names are known) |
| B. Core-style `log.write` | Matches Core and modules' `module.name` | Not English |
| C. A suffix: `write "hi" (log)` | Leaves the phrase alone | Parentheses already group |

Recommended: **A**, accepted only for names in `[dependencies]` (no
variable can be named like a dependency). Authors rarely write it: it
appears where the compiler had to ask.

The lockfile's question keys include the candidate readings (§10 of the
spec), so a dependency that adds a phrase fitting one of the user's lines
asks that line again. Under `--locked` (CI, CMake) that is an error
naming the line and the new phrase, not a silent change. `ovis
check-phrases` learns `--dependencies`: which of the project's lines a
dependency's phrases would make questions, before adding or updating it.

## 6. Building a dependency never asks

A package is built `--locked`: its unclear lines must have meanings in
its own files and lockfiles, and nothing in a dependency is ever
written. So a dependency builds the same for everyone, with no model and
no network once fetched (invariant 1, extended to the whole graph).
`ovis release` (§9) refuses to tag a package that doesn't build locked.
A package's checked-memory rules, warnings and `--lax-memory` are its own:
its author settled them; the user sees errors in a dependency only if it
doesn't build at all, and then as `in log 1.4.0 (…/log/main.ovis): …`.

## 7. Source, built with the program

| Option | Pros | Cons |
| --- | --- | --- |
| **A. Dependencies are source, read and compiled with the program** | Generics copy with the user's types; one optimiser pass over everything; no binary compatibility between toolchains to promise | Every build reads every dependency |
| B. Packages ship compiled objects and an interface file | Fast builds | Generics can't be copied; an object per target and toolchain; trust in binaries |

Recommended: **A**, with a cache of each dependency's parsed and
elaborated Core keyed by (tree hash, toolchain version) in
`~/.cache/ovis/build/`, so reading dependencies is paid once. Measured
before deciding anything bigger (state.md holds front-end timings).

The toolchain: each package says the oldest `ovis` it needs; the graph
needs the highest of them, and a build with an older toolchain stops
naming the package (`ovisup install 0.4` is the fix; the project's
`toolchain` line, projects.md §6, can pin it).

## 8. Fetching, the lockfile and trust

**Fetching** runs `git` (as `ovis model pull` runs `curl`): `git
ls-remote --tags` to list, then a shallow fetch of the one tag into a
read-only, content-addressed cache:

```text
~/.cache/ovis/packages/
  github.com/ovis-libs/log/      a bare clone, fetched into as needed
  trees/<sha256>/                one version's files, read-only, shared by all projects
```

Credentials, proxies and mirrors are git's: ssh keys, credential
helpers, `url.<base>.insteadOf` (a company mirror of GitHub needs no Ovis
setting). Ovis keeps no tokens.

**`ovis.lock`** (the project's, projects.md J6) gains the graph:

```json
{"packages": [
  {"name": "log", "git": "https://github.com/ovis-libs/log", "line": "1",
   "version": "1.4.0", "commit": "9f2c…", "tree": "sha256:51ab…",
   "dependencies": []},
  {"name": "csv", "git": "https://github.com/ovis-libs/csv", "line": "1",
   "version": "1.4.0", "commit": "c01d…", "tree": "sha256:7e3a…",
   "dependencies": ["log@1"]}]}
```

- With a lockfile, a build fetches exactly those commits and checks each
  tree's SHA-256 (over the sorted paths and contents, not git's SHA-1):
  a moved tag or a rewritten repository is an error naming the package,
  never a different build.
- Without one, or after `ovis.toml` changed, §3's walk runs and writes it.
- `ovis update [NAME]` raises minimums to the newest tag on each line and
  rewrites the lock; `ovis update NAME --line 2` moves to a new line.
- A tag deleted upstream still builds from the cache or `vendor/`.
- Only the root's `ovis.lock` decides versions: a dependency's own lock
  is ignored (its versions are minimums in its `ovis.toml`), while its
  per-file `.lock` files, which hold its meanings, are always used (§6).

**Vendoring:** `ovis vendor` copies every dependency's tree into
`vendor/` (by name and version) and builds from there afterwards, for
projects that must build with no network or must outlive their
dependencies' hosts.

**Trust:** a dependency is code the user runs, and with `use the C
library` it reaches C. The lock's hashes make it the code the user
reviewed; nothing makes it safe. No install scripts run: building a
package only reads its files, and its C goes through the same generated
CMake the user can read (`ovis cmake --print`).

## 9. Commands

| Command | Does |
| --- | --- |
| `ovis add NAME --git URL [--version 1.2]` | adds a dependency (the newest tag's line by default) and locks it |
| `ovis remove NAME` | removes it and what only it needed |
| `ovis update [NAME] [--line N]` | §8 |
| `ovis tree` | the graph, with `(2 copies: 1.4.0 for csv, 2.0.0 for report)` where lines differ, and why each version was chosen |
| `ovis vendor` | §8 |
| `ovis fetch` | downloads what the lock names, for a later `--offline` build |
| `ovis release X.Y.Z` | checks `version`, builds locked, no path or commit dependencies, no orphan impls, then `git tag vX.Y.Z` (the author pushes) |

Publishing is `git push --tags`. Finding packages is outside Ovis: a
README list, a wiki, a search over hosts' topics (`ovis-package`). An
optional index can come later as a plain git repository of URLs, which
Ovis needs to read only for `ovis search`.

## 10. Plan

| Step | Work | Test |
| --- | --- | --- |
| H1 | `[package]`, `[dependencies]`, `[overrides]` in the `ovis.toml` reader; version lines | parse, bad versions, pins refused in a package |
| H2 | fetching: `ls-remote`, shallow fetch, the cache, tree hashes | `file://` repositories made by the test with tags; a moved tag fails |
| H3 | §3's walk and `ovis.lock`'s `packages`; `ovis tree` | diamonds: same line (one copy), two lines (two), 0.x, an override, a fork |
| H4 | loading packages as modules with a package identity: Core names, `internal/`, direct-dependency phrase scope | two lines of one package in one program, both called |
| H5 | types per package, the two-copies error message; generics copied once across dependents | passing `log@1`'s entry to `log@2` fails with the message |
| H6 | `NAME's phrase` in lines and meanings; `check-phrases --dependencies` | a phrase in two dependencies becomes a question, settled by a meaning |
| H7 | dependencies built `--locked`; errors located in the package; the orphan rule | a dependency with an unsettled line; an impl in neither package |
| H8 | C dependencies unified across packages; pkg-config minimums | two packages wanting one C library, agreeing and not |
| H9 | `ovis add`, `remove`, `update`, `vendor`, `fetch`, `release` | an `examples/packages/` project with two local packages |
| H10 | Core cache per dependency (§7), measured; docs (README "Packages", architecture-spec, c-libraries.md) | a second build reads no dependency |

H1 needs projects J1–J2 (the reader, building a project) and shares J6's
git fetching and `ovis.lock`; J6 should be written with packages in mind.

## Decisions to make

1. A package is a git repository, versions are `vX.Y.Z` tags, the URL is
   its name (§1, §2 A)?
2. Minimal version selection, one version per line (§3 A)?
3. Different lines coexist as separate packages, their types distinct;
   C libraries one per program (§4)?
4. The orphan rule for trait impls, the root included (§4)?
5. Only direct dependencies' phrases in scope; `NAME's phrase` to say
   which (§5 A)?
6. Dependencies always built `--locked`; `ovis release` refuses a package
   that doesn't build so (§6)?
7. Source packages with a Core cache, not compiled ones (§7 A)?
8. Exported unless under `internal/` (§1)?
