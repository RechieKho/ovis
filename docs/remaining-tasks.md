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

- [x] `x = nothing` and `x != nothing` read as `is nothing` / `is not
  nothing` (a record compared with the `nothing` literal).

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
- [x] Making a record with a word the field doesn't have (`a person called
  "W" and aged 12`): the unclear span widens back to the record's name.
- [x] Missing phrases: `remove X from XS`, `remove item N of XS` (Core
  `remove-at`), `T becomes V`, `make T V`; `divide x by 2` of a whole
  number says the result is a decimal.
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
- [x] The daemon is waited for ten minutes (the 7B took 320 s once) and the
  fallback says it was still loading.
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
- [x] Candidates are capped at three questions' worth (27) in library order
  among equals; it was 21 and the library's growth pushed `n is divisible
  by 3` out for `if 3 goes into n`.
- [ ] User phrases: slots take the parameters' stated types, and a
  parameter the body uses as a number (`total >= 40`, `price * 0.9`) is a
  number slot; `--with examples/shop` is a CI check (0 cases worse). Left:
  other uses (text or list operations in the body) don't type the slot.

- [ ] **Explicit memory.** Design, accepted:
  [proposals/memory.md](proposals/memory.md). Built: E1 (`eval/memory`,
  `tools/peak_memory.py`), E2–E4, E6, E7, and E5 behind `--strict-memory`
  (state.md has the numbers). Left:
  - [ ] **Decide, then make E5 the default.** Under it `for each i in the
    numbers from 1 to 15:` and `show total with 2 decimals` fail (a new
    list or text is made inside another statement), as do 10 lines of the
    examples. Options: (a) as the proposal says, the author stores each
    first; (b) the compiler frees a statement's own temporaries right
    after the statement (nothing the author names is freed, no new value
    is hidden, and the loops above stay one line). Needs the owner's
    choice. Then migrate the prelude, examples, eval programs and tests,
    and flip `strict_memory`.
  - [ ] Checked mode: `show`/`strlen` of a freed text through C's `strlen`
    isn't caught; messages don't name the variable; lists of lists and
    named memories' values aren't followed by the strict pass.
  - [ ] An example with a named memory and `clear` (`examples/`).
- [x] Ovis Core `for-down` counts down; `the numbers from A down to B`.
  A block phrase `count down from N to M as i:` is not written.
- [x] `random` is splitmix64 seeded from the clock, process and address,
  with rejection so every value is equally likely.
- [x] Whole-number `+ - *` and negation stop the program on overflow
  (`llvm.*.with.overflow`). Division by zero was checked already.
  `INT64_MIN div -1` is not checked.
- [x] `-DOVIS_PORTABLE=ON` (CI and the embedded-model workflow use it):
  `GGML_NATIVE` off, AVX/AVX2/FMA/F16C on for x86-64, NEON on ARM. Source
  builds keep NATIVE on.
- [x] Determinism holds only on the same machine and build: documented
  (architecture-spec §10, README).

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
    say goodbye` and its synonyms `the action`/`function`/`method`/…, `call f with …`), prototypes on import.
  - [ ] R6 Headers beside the header and types its functions reach.
  - [ ] R7 Phrases, examples (zlib streaming, `readdir`), docs.
  - [ ] R8 Warning for a dropped result; `a copy of x` (deep).

- [ ] Maps (dictionaries). Records are M2. Needs a Core type with a key and
  a value type (`value_type` has one element), a runtime hash table and
  inference slots for both; then phrases (`set`, `get`, `has`, `remove`,
  `keys`, a printed form).
- [x] Text: split, find, replace, case and number parsing existed;
  `is a whole number` / `is a number` check text before converting it.
- [x] Lists of lists: Core types (`value_type::inner_lists`), codegen,
  printing, `keep`/`copy`, and phrases (`list of lists of whole numbers`).
- [ ] Callbacks into C (e.g. `qsort`, `sqlite3_exec` with an Ovis
  function). Today a callback parameter can only be given `null`.
- [ ] C structs: pass and return by value, read fields. Today these
  functions are skipped on import.
- [ ] C function-like macros, and declarations in headers that the imported
  header includes (only the header itself is read today).
- [x] Ownership of memory C functions return: the author's, freed with
  `free` ([proposals/memory.md](proposals/memory.md) §7; c-libraries.md).
- [x] **Building with CMake** ([proposals/cmake.md](proposals/cmake.md)):
  K1–K6 are built (`--object`, `--link-by-caller`, `@FILE`, `--locked`,
  `--depfile`, `ovis runtime`, `find_package(Ovis)`, `ovis_add_executable`,
  `examples/cmake` in CI, docs in c-libraries.md). Not run on macOS or with
  a shared C library yet.

## P2: model and tooling

- [x] Phrase files loaded at run time (`phrases/` beside a folder program's
  `main.ovis`, or `--phrases DIR`): `.phr`, `.core`, `.generic`; hashes in
  the lockfile with a warning on an edit; errors at `file:line`; a phrase
  worded like a built-in is refused; `ovis_eval questions --with FOLDER`
  reads them. Not done: `ovis phrases` doesn't list them.
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
- [x] A meaning the lockfile doesn't know warns (not fails) when it loses a
  name or number the line has (the rewrites' check).
- [x] `test/test.cpp` is split (`core_test`, `model_test`, `docs_test`,
  `c_import_test`, `modules_test`; helpers in `support`).
- [ ] Windows support and testing, then add it to CI.
- [x] macOS in CI: build, `test_ovis` and offline examples (`ci.yml`).
- [x] macOS: system headers are looked for in the SDK
  (`system_include_directories`, `xcrun --show-sdk-path`); the
  real-C-library test uses the same list. Not run on a Mac yet.
