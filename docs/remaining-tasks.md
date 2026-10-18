# Remaining tasks

Ordered by priority. Current status is in [state.md](state.md), and the
design these tasks change is in [architecture-spec.md](architecture-spec.md).
Move a task to state.md's history when it is done. Finished and dropped
tasks: [dream/remaining-tasks](dream/remaining-tasks/INDEX.md).

## P0: the phrase pivot

The design is in [proposals/phrase-grammar.md](proposals/phrase-grammar.md)
and [proposals/types.md](proposals/types.md) (accepted). A deterministic,
English-like parser reads the program; the model only picks between
readings that type-check. Milestones M0–M4 below follow the proposal's
phases.

M0 (model scoring) and M1 (the phrase front end, no records, with
follow-ups M1.15 and M1.16) are done: state.md history, and
[dream/remaining-tasks](dream/remaining-tasks/INDEX.md) for the tasks as
written.

### M2: records

types.md §1. M2.1 (records in Ovis Core) is done: architecture-spec §6–§8.

- [ ] **M2.2 Surface:** `a NAME has:` blocks with optional types and
  defaults; `a NAME with FIELD VALUE, ...` (a missing field without a
  default is an error); `F of X`, `X's F`, `X.F`, all settable. Defaults
  belong to the surface: lowering fills them into `(new …)`, which needs
  every field. The front end's types (`value_kind`) need a record kind.
- [ ] **M2.3 `field of T` slots** in templates, with library phrases such
  as `sorted by`, `total … of`, `the … with the highest …`.
- [ ] **M2.4 Questions:** unknown fields (candidates: the record's fields)
  and references by type name ("the book").
- [ ] **M2.5** An example and eval programs using records.

### M3: built-in traits

types.md §2, "Built-in traits".

- [ ] **M3.1** Template slot types (`num`, `any`, ...) become built-in
  traits; rewrite the library against trait slots, with no change to
  existing programs (all eval programs still build identically).
- [ ] **M3.2** `showable`, `comparable`, `equatable`: a record implementing
  one works with `show`, `sort`, `smallest of`, `=` and `contains`.
  Concrete types are known here, so dispatch is static.

### M4: user traits and impls

types.md §2.

- [ ] **M4.1 Ovis Core:** `trait` and `impl`; one impl per trait and type;
  missing methods listed; default methods.
- [ ] **M4.2 Surface:** `a NAME can:` and `a NAME is a TRAIT:`, with `it`
  as the value itself.
- [ ] **M4.3 Dynamic dispatch:** trait values as a record pointer plus a
  vtable; mixed lists (`list of shape`).
- [ ] **M4.4 Trait-typed parameters:** annotated, or inferred when the
  phrases used belong to exactly one trait, else an error asking for the
  annotation.
- [ ] **M4.5** (later, optional) Direct calls where only one concrete type
  reaches a call site.

### Defer: `at the end`

Zig's `defer`: cleanup that runs on every exit of its block. Design in
[proposals/defer.md](proposals/defer.md) (accepted). D1 can land before M1
is finished.

- [ ] **D1 Ovis Core `(defer …)`:** parser, elaborator (implicit return
  skips trailing defers), checker (no `return` or outer `break`/`continue`
  inside; not at a helper's top level), codegen with cleanup frames copied
  to every exit (fall-through, `break`, `continue`, `return` after its
  value, end of `main`). Tests: order, per-iteration, early return before
  the defer, return value unchanged, nested defer.
- [ ] **D2 Surface:** `at the end[,] STATEMENT`, `at the end:` and the
  `defer` alias in `shape_of`; lowering, no hoisting out of the block,
  names must be set above; the `Compiler:` note for non-function blocks.
- [ ] **D3** An example closing a file or SQLite handle with `at the end`,
  and an eval program using it.

## P1: correctness and robustness

- [ ] Ranking: "3.7 rounded down" is 10th of 14 readings, so the 9 shown
  miss it, and the right reading is first in 10 of 24 (13 at M0.4). Find
  why (`ovis_eval questions --no-model`), improve the lexical ranking or
  move to embeddings, and raise CI's `--min-first`/`--min-shown` floors.
- [ ] User phrases accept any type: their template slots are `any`, so the
  type filter can't drop them. With `examples/shop`'s phrases, "show the
  count of names" offers `apply the discount to names` first (trigrams of
  "count" in "discount"), above `length of names` (`ovis_eval questions
  --with examples/shop`). Give the slots the parameters' stated or
  inferred types, then make `--with examples/shop` a CI check.

- [ ] Memory: lists and text are never freed. That is fine for short
  programs, but long loops leak.
- [ ] Ovis Core `for` only counts up. Add a step or downward counting, so a
  `count down from (n)` phrase can lower to it.
- [ ] `random` uses `rand() % n`, which is biased and has a weak seed.
- [ ] Integer overflow is unchecked. Integer division by zero is checked.
- [ ] llama.cpp is built with `GGML_NATIVE` on, so binaries may not run on
  other CPUs (it happened: state.md lessons). Turn it off for release builds
  and in CI, but keep AVX2/FMA/F16C on for x86-64, or use
  `GGML_CPU_ALL_VARIANTS`; NATIVE off alone made questions 20× slower.
- [ ] Determinism holds only on the same machine and build. Document this
  in the lockfile, or store the fingerprint per entry.

## P2: language features

- [ ] Maps (dictionaries). Records are M2.
- [ ] Text as a first-class value beyond what exists: split, find,
  replace, upper and lower case, and number parsing without libc.
- [ ] Lists of lists.
- [ ] Callbacks into C (e.g. `qsort`, `sqlite3_exec` with an Ovis
  function). Today a callback parameter can only be given `null`.
- [ ] C structs: pass and return by value, read fields. Today these
  functions are skipped on import.
- [ ] C function-like macros, and declarations in headers that the imported
  header includes (only the header itself is read today).
- [ ] Ownership of memory C functions return (free, or document leaks).
- [ ] Static linking when a `.a` exists, as an option.

## P2: model and tooling

- [ ] Phrase files loaded at run time: `.phr` templates (optional words,
  alternatives, aliases, generic slots, any Core) from a `phrases/` folder
  in a program, or `--phrases DIR`, without rebuilding `ovis`. Today only
  `phrases/*.phr` compiled in, and `to …:` phrases in programs. Needs: the
  files' hashes in the lockfile (as for C headers, with a warning when
  one changed); errors at `file:line`; the overlap check for user phrases
  applied to them; `ovis_eval questions --with` reading them, so a team
  sees when its phrases make the questions worse.
- [ ] The phrase self-test in `ovis` itself (`ovis check-phrases PROGRAM`),
  with the question cases embedded, for authors without the repository.

- [ ] `ovis fmt`: re-render `Read as:` and `Compiler:` lines without
  building.
- [ ] Editor support: highlight phrases, `Read as:` lines and questions;
  jump from errors.
- [ ] Split `test/test.cpp` (about 770 lines) by area.
- [ ] Windows support and testing, then add it to CI.
- [ ] macOS: builds and tests pass with LLVM 22. The real-C-library test
  should find zlib/SQLite headers in the SDK (`xcrun --show-sdk-path`),
  not only `/usr/include`. Then add macOS to CI.
