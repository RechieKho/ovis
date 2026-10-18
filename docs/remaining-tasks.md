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

What the questions need from the model. It can be built and measured
before the parser exists. Done: M0.1 `choose` (state.md history).

- [ ] **M0.2 Speed.** First numbers are in state.md (1.5B ~250 ms, 3B
  ~500 ms per question on 4 CPU cores). Still to do: 0.5B, and a repeatable
  measurement in `embedded-model.yml` with the M1.14 questions.
- [ ] **M0.3 Default model size**, by the M1.14 evaluation: the smallest
  model whose confident picks are right at least 90% of the time.
- [ ] **M0.4 Ranking.** Ovis has **no embedding similarity** today, despite
  the proposal's wording (fix it there). Start lexical: shared word stems
  between the verb and a template's words, plus `alias:` lines, plus
  character-trigram similarity for near spellings. Add model embeddings
  (llama.cpp embeddings mode, mean-pooled, same model) only if M1.14 shows
  the right template outside the top 8 too often.
- [ ] **M0.5 GPU.** Measure `choose` on a Mac (Metal) and check its
  probabilities match the CPU ones closely enough that decisions agree.

### M1: phrase front end, no records

Proposal phase 1: §2–§7 of phrase-grammar.md. Done: M1.1 lexer, M1.2
template files (state.md history). A **minimal slice** of M1.3–M1.8 and
M1.11 runs end to end (`src/ovis/phrase/front_end.cpp`, `--phrases`); the
tasks below stay open for what it leaves out: user functions (`to … :`),
lockfile entries, modules, `use-c`, the ranking shortcut, and the M1.4
rule that a reading the elaborator would accept is never dropped. architecture-spec.md is near 500 lines
until M1.13 removes §2–§5; keep §13 additions short.

- [ ] **M1.3 Parser** (`parser.cpp`): Earley over the templates, the
  built-in statements (`=`, `to … :`, `give back`, control flow of
  phrase-grammar §7) and expressions with fixed precedence. Articles are
  optional, words match case-insensitively and singular/plural. Returns
  every parse of a line. Tests: each line of the ported examples gives the
  expected parses.
- [ ] **M1.4 Scope and type filter** (`scope.cpp`):
  - first pass: collect every `to … :` header, so phrases and functions are
    visible in any order, as functions are now;
  - in order: a scope of names and types, inferred from assignments, with
    unknown types left as variables;
  - drop parses with names not in scope or slot types that don't fit.
  The full elaborate and check still run once on the whole lowered
  program; this filter only needs to be fast and never reject a parse they
  would accept.
- [ ] **M1.5 Lowering** (`lower.cpp`): parses to `core::program` AST
  directly, not to text. Blocks to `if`/`while`/`each`/`for`; `to … :` to
  `fn` named from its fixed words (`pad_to`, or `as NAME`); `give back` to
  `return`; `f(args)` to a call. Core diagnostics map back to source lines
  (fixes the P1 task about Ovis Core line numbers for this front end).
- [ ] **M1.6 Questions** (`questions.cpp`): `struct question` and
  `struct candidate` as in phrase-grammar §4, and one generator per kind:
  `unknown_phrase` (arguments + verb, slot-type match, M0.4 ranking, top 8),
  `ambiguous_parse`, `unknown_name`, `reference`, `missing_argument`. The
  deterministic rule of each kind runs first. Tests with no model: exact
  candidate lists for about 15 lines.
- [ ] **M1.7 Resolution** (`resolve.cpp`), in this order:
  1. a `Read as:` line under the source line (authoritative);
  2. the lockfile, keyed by a hash of line, span, context and candidate
     readings;
  3. the ranking shortcut, when its margin is wide;
  4. `choose`, accepted at `p ≥ 0.6` and a margin `≥ 0.3` (tuned in M1.14);
  5. otherwise a report listing the options ("Write one of them in the
     source").
  `--model-budget SECONDS` (default 10): questions left when it runs out
  are reported like unclear ones. Tests use a scripted model that returns
  given probabilities.
- [ ] **M1.8 Write-back**: `Read as:` lines rendered under their line,
  `Compiler:` notes kept as now. Rebuilding gives byte-identical files (the
  lesson in state.md). Lockfile entries for questions, with the model
  fingerprint.
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
  CLAUDE.md's description and README. Then dream.
- [ ] **M1.14 Evaluation**: about 20 programs in `eval/programs/`, written
  the way a person would. `ovis_eval questions` reports per program:
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
