# Remaining tasks

Ordered by priority. Current status is in [state.md](state.md), and the
design these tasks change is in [architecture-spec.md](architecture-spec.md).
Move a task to state.md's history when it is done. Dropped tasks of the old
pipeline: [dream/remaining-tasks](dream/remaining-tasks/INDEX.md).

## P0: the phrase pivot

The design is in [proposals/phrase-grammar.md](proposals/phrase-grammar.md)
and [proposals/types.md](proposals/types.md) (accepted). A deterministic,
English-like parser reads the program; the model only picks between
readings that type-check. Milestones M0–M4 below follow the proposal's
phases.

Rules for the transition:
- The new front end is a separate namespace, `ovis::phrase`, in
  `src/ovis/phrase/` and `include/ovis/phrase/`. It lowers to the existing
  `core::program`, so elaborate, check, codegen, runtime, `use-c` and
  modules are reused, not rewritten.
- Until M1 is finished it runs with `ovis run --phrases`. The old pipeline
  stays the default and CI stays green.
- The old pipeline is removed in one change at the end of M1 (M1.13), with
  the spec, CLAUDE.md and README updated in the same commit.
- `architecture-spec.md` describes what is built, so each task updates it
  when it lands, not before.

### M0: model scoring and ranking

What the questions need from the model. Done: M0.1 `choose`, M0.2 speed,
M0.3 default size (3B, switched in M1.13) and M0.4 ranking, measured by `ovis_eval questions eval/questions.txt`
(state.md). Embeddings for ranking come only if M1.14 shows the right
reading outside the 8 shown too often.

- [ ] **M0.5 GPU.** On a Mac: `ovis_eval questions eval/questions.txt
  --compare-cpu` asks each question on the GPU and the CPU, and reports the
  largest difference in a probability and any case decided differently.
  Record it and the time per question.

### M1: phrase front end, no records

Proposal phase 1: §2–§7 of phrase-grammar.md. Done: M1.1 lexer, M1.2
template files, M1.3 parser, M1.4 scope and type filter, M1.5 lowering,
M1.6 questions, M1.7 resolution (state.md history). Still to come: the
lockfile, modules, `use-c`, the library, the CLI, the examples. How it works is in
[phrase-front-end.md](phrase-front-end.md).

- [ ] **M1.8 Write-back**: `Read as:` lines rendered under their line,
  `Compiler:` notes kept as now. Rebuilding gives byte-identical files (the
  lesson in state.md). Lockfile entries for questions, with the model
  fingerprint, keyed by a hash of line, span, context and candidate
  readings, tried after `Read as:` lines and before rules (M1.7's step 2).
- [ ] **M1.9 Phrase library v1**: about 100 templates over what Ovis Core
  has (lists, text, input/output, maths, randomness), plus an Ovis Core
  prelude for multi-step phrases. Every template has a test line.
- [ ] **M1.10 Modules and C**: helper files may only define (phrases,
  functions, `use-c`, constant lets), as now. `use-c` functions are called
  as `name(args)` (phrase-grammar §7). Reuse `modules.cpp` for name
  collisions.
- [ ] **M1.11 CLI**: `ovis run/build --phrases`, `--model-budget`;
  questions the model couldn't settle are printed with their options.
- [ ] **M1.12 Port the examples** (hello, fizzbuzz, primes, statistics,
  temperature, wordcount, shop) to phrases, with lockfiles. `vague.ovis`
  becomes a program whose question the build reports. CI builds them all
  offline and fails if any file changes.
- [ ] **M1.13 Switch and remove**: phrases become the only front end.
  Remove the detail pass, translation, repair, explanations, their prompts
  and grammars, `english.cpp` paragraphs, the old lockfile entries and
  `eval/detail-cases.txt`. Rewrite architecture-spec.md §2–§5; update
  CLAUDE.md's description and README. Make Qwen2.5-Coder-3B Q4_K_M the
  default model (`ovis model pull`, lookup, `embedded-model.yml`'s default,
  README), unless M1.14 shows a smaller one settles as many. Then dream.
- [ ] **M1.14 Evaluation**: about 20 programs in `eval/programs/`, written
  the way a person would, extending `eval/questions.txt`. `ovis_eval questions` reports per program:
  questions asked, how each was settled (rule, ranking, model, author),
  model accuracy against hand labels, and time. Provisional targets:
  - median questions per program ≤ 2;
  - confident model picks right ≥ 90%;
  - a cold build with the model under 5 s on 4 CPU cores.
  Add aliases first, templates second, for the gaps it shows.

### M2: records

types.md §1.

- [ ] **M2.1 Ovis Core:** `record`, `new`, `field`, `set-field` in the AST,
  elaborator (record types unify by name), checker and codegen (an LLVM
  struct behind a pointer, heap-allocated). `print` of a record shows its
  fields.
- [ ] **M2.2 Surface:** `a NAME has:` blocks with optional types and
  defaults; `a NAME with FIELD VALUE, ...` (a missing field without a
  default is an error); `F of X`, `X's F`, `X.F`, all settable.
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

- [ ] Checker messages cite Ovis Core line numbers ("line 3: …"). They are
  fine for the model, but confusing if shown to the author through
  `--verbose`.
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

- [ ] `ovis fmt`: re-render `Read as:` and `Compiler:` lines without
  building.
- [ ] Editor support: highlight phrases, `Read as:` lines and questions;
  jump from errors.
- [ ] Split `test/test.cpp` (about 770 lines) by area.
- [ ] Windows support and testing, then add it to CI.
- [ ] macOS: builds and tests pass with LLVM 22. The real-C-library test
  should find zlib/SQLite headers in the SDK (`xcrun --show-sdk-path`),
  not only `/usr/include`. Then add macOS to CI.
