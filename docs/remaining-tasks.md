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

Done (types.md §1): Ovis Core (M2.1), the surface (M2.2), `field of T`
slots (M2.3), questions (M2.4), an example and eval programs (M2.5). See
state.md history, architecture-spec §4–§6 and
[dream/remaining-tasks](dream/remaining-tasks/INDEX.md). Left from it:

- [ ] A record field of its own kind (`next (node)`) can't be given a value
  in phrases: there is no `nothing` for records yet (Core has null).
- [ ] Defaults are read before other records' fields, in file order, so a
  default can't be `a point with …` of a record declared later.
- [ ] Unknown-field candidates in `a NAME with …` still offer fields
  already given in the line.

### M3: built-in traits

Done (types.md §2, "Built-in traits"): M3.1 and M3.2. See state.md history,
architecture-spec §4 and [dream/remaining-tasks](dream/remaining-tasks/INDEX.md).
Left from it:

- [ ] A record without the phrase (`sort books`, `books contains b`) gives
  an "unclear" report offering unrelated readings (`show books`). Say
  instead: "a book can't be sorted: write `to (a: book) comes before (b:
  book):`".
- [ ] `position of` and `count of` still have a template per element type;
  records can't use them (they need `equatable` and a generic each).
- [ ] A list of records prints structurally even when the record has
  `text of`; `+` and `as text` of a list of records don't join.
- [ ] `text of`, `comes before` and `is the same as` are written only as
  `to (a: book) …:` with stated record types; `a NAME is a TRAIT:` writes the impl of a user trait (M4.2); built-in
  traits are still implemented by `to (a: book) …:`.

### M4: user traits and impls

Done (types.md §2): M4.1–M4.4, and one-line phrase bodies (M5). See state.md history, architecture-spec §5–§6
and [dream/remaining-tasks](dream/remaining-tasks/INDEX.md). Left from it:

- [ ] Only records implement a trait: not numbers or text
  (`a whole number is a shape:`).
- [ ] A trait's own phrases need `(it)` and can't be operators or have the
  value in a built-in trait's place (`(a) comes before (b)`); traits that
  require other traits and generic traits are left out (types.md).
- [ ] A record with several common traits in a mixed list is not read as
  a list of any of them (the elaborator needs exactly one shared trait,
  `common_traits`): a way is to pick the trait by the phrases used on the
  loop variable, as untyped parameters do.
- [ ] **M4.5** (later, optional) Direct calls where only one concrete type
  reaches a call site. A call on a record already calls its impl directly.

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

- [ ] Ranking: the right reading is first in 10 of 24 (ties in library
  order). An embedding tie-break raised that to 21 but not the model's
  right picks (state.md, "questions measured"; code in `8ffe5c3`, which
  `git revert` of its revert restores). Measure ranking by right picks,
  not rank, over several orders (`ovis_eval questions --orders 6`: 3B
  22–23 of 26). Next: try asking in two orders and averaging, against the
  order bias and the confident "none" misses; and revisit choosing the 9
  shown by embedding (a third fewer asks). Embeddings alone (0.5B) come
  within 3 cases of 3B asked but never say "none" (state.md): decide
  whether the model stays a matchmaker or gets a generative job
  (composing readings, proposing `to …:` phrases), checked by the types
  and the lockfile. Owner: no generative job; GPU first (7B with a GPU,
  3B without) and cheaper questions (done: run-off header reuse).
- [ ] Measure 7B on a GPU (the owner's Mac): time per question and the
  hard set with `--orders 4`, to confirm GPU-first. Then CUDA/Vulkan
  builds in CI, if a runner has a GPU.
- [ ] Cheaper questions, next: fewer tokens per option (each repeats its
  arguments), and whether a run-off can be avoided when one group's
  favourite is clear. Measure answers with `--orders`.
- [ ] Candidates are capped at 21 in library order among equals, so
  readings with the arguments reordered (ranked last among equals) are the
  first lost (`if 3 goes into n` offers `n is divisible by 3` 21st of 21).
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
