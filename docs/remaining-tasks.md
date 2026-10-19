# Remaining tasks

Ordered by priority. Current status is in [state.md](state.md), and the
design these tasks change is in [architecture-spec.md](architecture-spec.md).
Move a task to state.md's history when it is done. Finished and dropped
tasks: [dream/remaining-tasks](dream/remaining-tasks/INDEX.md).

## P0: the phrase pivot

The design is in [proposals/phrase-grammar.md](proposals/phrase-grammar.md)
and [proposals/types.md](proposals/types.md) (accepted). A deterministic,
English-like parser reads the program; the model picks between
readings that type-check, or rewrites a line in phrases (checked). Milestones M0–M4 below follow the proposal's
phases.

M0 (model scoring) and M1 (the phrase front end, no records, with
follow-ups M1.15 and M1.16) are done: state.md history, and
[dream/remaining-tasks](dream/remaining-tasks/INDEX.md) for the tasks as
written.

### M2 and M3: records and built-in traits

Done, with their leftovers (types.md §1–§2): `nothing` for records and `is
nothing`, defaults of later records, a field given once, what to write when
a record lacks `comes before` / `is the same as`, `position of` and `count
of` for any equatable type, lists of records with `text of`, and `a book is
comparable:` (also `equatable`, `showable`). See state.md history,
architecture-spec §4–§5 and
[dream/remaining-tasks](dream/remaining-tasks/INDEX.md). New, small:

- [ ] `x = nothing` and `x != nothing` as operators read nothing (only `x
  is nothing` / `is not nothing` do): `nothing` is typed as a C pointer.
  Decided: read them as `is nothing` / `is not nothing`.

### M4: user traits and impls

Done (types.md §2): M4.1–M4.4, and one-line phrase bodies (M5). See state.md history, architecture-spec §5–§6
and [dream/remaining-tasks](dream/remaining-tasks/INDEX.md). Left from it
(designs decided 2026-10-19, state.md decisions):

- [ ] Only records implement a trait: not numbers or text
  (`a whole number is a shape:`). Decided: stay records-only; the error
  suggests a record that wraps the value. Boxing with a tag waits for
  memory E2–E3 and a program that needs it.
- [ ] A trait's own phrases need `(it)` and can't be operators or have the
  value in a built-in trait's place (`(a) comes before (b)`). Decided:
  the first parameter of the trait's type picks the impl; two such values
  must be the same record, checked when the program runs.
- [ ] Traits that require other traits (`a shape can: … and is
  showable`): decided yes, later; impls must have both. Generic traits:
  decided no (Core has no generics).
- [ ] Records of several common traits in one list: the list is a list of
  the first (by name); a phrase of another trait on its items, or passing
  it as a list of that trait, is allowed when some record has both, and a
  record without the second trait fails when the program runs (not when
  it is built). Decided: keep the run-time check and add a build-time one
  where the list's records are known (a list written out, or a variable
  given one); no list types that name several traits.
- [ ] **M4.5** (later, optional) Direct calls where only one concrete type
  reaches a call site. A call on a record already calls its impl directly.

### Defer: `at the end`

Done (D1 Ovis Core `(defer …)`, D2 surface with the `Compiler:` note, D3
`examples/notes.ovis` and `eval/programs/tidy.ovis`): state.md history,
architecture-spec §5–§6 and [proposals/defer.md](proposals/defer.md).

- [ ] `at the end` inside a nested block of a deferred block hoists names
  as other blocks do (they leak out of the deferred block's own `let`s).
  Rare; revisit if a program needs it.
- [ ] Making a record with a word the field doesn't have (`a person called
  "W" and aged 12`): the unclear span is only the words after the readable
  start, so the `a NAME with …` candidate isn't offered. Widen the span to
  the record's name when one starts the line.
- [ ] Missing phrases a question can only answer wrongly: `remove X from
  XS` (offered `add`), `T becomes V`, `make T V`; `divide x by 2` for a
  whole-number x says no phrase reads it, not that the result is a decimal.
- [ ] Edit candidates: two edits (`w's age becomes 13` is `set w's age to
  13`) would cover the rest of the near misses that rewrites miss, but the
  edits multiply; try only with a verb of the built-in forms put first.
  Parsing a line with a question now takes up to ~0.4 s.
- [ ] Rewrites: measure them over a held-out set (`ovis_eval rewrites`?)
  so prompt changes are judged, not tried on a few lines: a one-sentence
  change turned a right 3B rewrite wrong. 3B gives some lines back
  unchanged (`counter_a's count not 0`), where 7B rewrote them. `X is V` as equality is what the
  model writes most for a test (`winnie's age is 13`); the 10 s
  `--model-budget` holds about three rewrites on a CPU.
- [ ] The 7B daemon didn't start on a cold disk cache (320 s load); the
  build fell back to loading in-process. Wait longer, or say why.
- [ ] Buffers for C (`fgets`) can't be made in phrases (`buffer` is
  Core only): `examples/notes.ovis` reads with `fgetc`.

## P1: correctness and robustness

- [ ] Ranking: the right reading is first in 11 of 24 (ties in library
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

- [ ] **Explicit memory.** Lists and text are never freed, so long loops
  leak (783 MB for 10 million passes that join text). Design, accepted:
  [proposals/memory.md](proposals/memory.md). Each step leaves the suite
  green:
  - [ ] E1 Measure: the §Why loop, a list of records made and dropped each
    pass, a parent-linked tree; record peak memory as the baseline.
  - [ ] E2 Runtime memories: every allocation through `ovis_alloc`, the
    memory stack, arenas, `ovis_free_*`, `ovis_join`. No surface yet;
    everything still goes to the program's memory.
  - [ ] E3 Ovis Core and codegen: `memory`, `free`, `new-memory`,
    `with-memory`, `with-new-memory`, `clear-memory`, `keep`; the pop in
    exit frames. Test that `give back` and `stop` free the memory.
  - [ ] E4 Phrases: `free`, `new memory`, `with new memory:`, `with m:`,
    `clear`, `keep`; an example with a named memory and `clear`.
  - [ ] E5 Checker: the strict rule (§2) with new/existing/mixed phrases,
    one-allocation joins, and the lost and stale value checks (§4). Make
    the prelude, examples and eval programs follow it.
  - [ ] E6 Checked mode (§6): the value table, `ovis_line` in runtime
    errors, `--fast`, `--report-leaks`; measure its overhead.
  - [ ] E7 Docs: ovis-core.md, architecture-spec §8, c-libraries.md (§7).
- [ ] Ovis Core `for` only counts up. Add a step or downward counting, so a
  `count down from (n)` phrase can lower to it.
- [ ] `random` uses `rand() % n`, which is biased and has a weak seed.
- [ ] Integer overflow is unchecked. Integer division by zero is checked.
- [ ] llama.cpp is built with `GGML_NATIVE` on, so binaries may not run on
  other CPUs (it happened: state.md lessons). Turn it off for release builds
  and in CI, but keep AVX2/FMA/F16C on for x86-64; NATIVE off alone made
  questions 20× slower. Decided: NATIVE stays on for source builds;
  release builds and CI pin AVX2/FMA/F16C (NEON on ARM), not
  `GGML_CPU_ALL_VARIANTS`.
- [ ] Determinism holds only on the same machine and build. Decided:
  document it (architecture-spec §10–§11, README): the committed lockfile
  makes builds the same; no fingerprint per entry.

## P2: language features

- [ ] **Pointers, C structs and references**
  ([proposals/references.md](proposals/references.md), accepted). Covers
  the C struct, callback and included-header tasks below and the P0
  `fgets` buffer task. Each step leaves the suite green:
  - [ ] R1 Core: C number types, Zig's pointer kinds, `load store offset
    cast size-of`, `ref` of anything.
  - [ ] R1b Non-null records and lists, `or nothing`, narrowing; migrate
    the prelude, examples and eval programs.
  - [ ] R2 Import structs as C records (layout from clang), `a new T`.
  - [ ] R3 Arrays (slices), `the items of xs`, `text of` a byte array.
  - [ ] R4 Structs by value through C wrappers.
  - [ ] R5 Callbacks, phrase values (§7b: `compare _ and _`, `the phrase
    say goodbye`, `call f with …`), prototypes on import.
  - [ ] R6 Headers beside the header and types its functions reach.
  - [ ] R7 Phrases, examples (zlib streaming, `readdir`), docs.
  - [ ] R8 Warning for a dropped result; `a copy of x` (deep).

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
- [ ] Ownership of memory C functions return: the author's, freed with
  `free` ([proposals/memory.md](proposals/memory.md) §7). Done with E3–E7.
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

- [ ] **Quick fixes** ([proposals/quick-fix.md](proposals/quick-fix.md),
  accepted). Done: Q1 diagnostics, Q2 `ovis check --json`, Q3 `ovis fix`
  and fixes offered by `run`/`build` in a terminal. Next:
  - [x] Q6 `ovis lsp`, Q7 VS Code extension (dims sigils; folding a line
    away isn't possible in VS Code). Not yet tried in VS Code itself: the
    owner should install `editors/vscode` and report.
  - [x] Q4 closest phrases when nothing fits; `ovis phrases WORDS`.
  - [x] Q5 `ovis fmt` (re-render sigils, migrate old `Read as:` lines).
- [ ] A meaning the lockfile doesn't know is trusted as the author's, so
  without a committed lockfile an edit to a line under a compiler-written
  meaning is undone silently. Warn when the meaning drops a word the line
  now has?
  Decided: warn (not fail) when it loses a name or number the line has,
  with the rewrites' "nothing named is lost" check.
- [ ] Split `test/test.cpp` (about 770 lines) by area.
- [ ] Windows support and testing, then add it to CI.
- [x] macOS in CI: build, `test_ovis` and offline examples (`ci.yml`).
- [ ] macOS: the real-C-library test should find zlib/SQLite headers in
  the SDK (`xcrun --show-sdk-path`), not only `/usr/include`; it skips
  there now.
