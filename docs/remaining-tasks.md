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


### M4: user traits and impls

Done (types.md §2): M4.1–M4.4, and one-line phrase bodies (M5). See state.md history, architecture-spec §5–§6
and [dream/remaining-tasks](dream/remaining-tasks/INDEX.md). Left from it
(designs decided 2026-10-19, state.md decisions):


### Defer: `at the end`

Done (D1 Ovis Core `(defer …)`, D2 surface with the `Compiler:` note, D3
`examples/notes.ovis` and `eval/programs/tidy.ovis`): state.md history,
architecture-spec §5–§6 and [proposals/defer.md](proposals/defer.md).


## P1: correctness and robustness

- [ ] Ranking: the right reading is first in 11 of 24 (ties in library
  order). An embedding tie-break raised that to 21 but not the model's
  right picks (state.md, "questions measured"; code in `8ffe5c3`, which
  `git revert` of its revert restores). Measure ranking by right picks,
  not rank, over several orders (`ovis_eval questions --orders 6`: 3B
  22–23 of 26). Next: try asking in two orders and averaging, against the
  order bias and the confident "none" misses (done: steadier, +0.2/+0.6,
  twice the time, an option; state.md); and revisit choosing the 9
  shown by embedding (a third fewer asks). Embeddings alone (0.5B) come
  within 3 cases of 3B asked but never say "none" (state.md): decide
  whether the model stays a matchmaker or gets a generative job
  (composing readings, proposing `to …:` phrases), checked by the types
  and the lockfile. Owner: no generative job; GPU first (7B with a GPU,
  3B without) and cheaper questions (done: run-off header reuse).
- [ ] Measure 7B on a GPU (the owner's Mac): time per question and the
  hard set with `--orders 4`, to confirm GPU-first. Then CUDA/Vulkan
  builds in CI, if a runner has a GPU.
- [x] Cheaper questions: fewer tokens per option (a repeated argument
  said once lost 2 right of 26) and skipping a run-off one sure group
  settles (saved 1% of asks): measured, neither kept (state.md).

## P2: language features

- [x] **Pointers, C structs and references**
  ([proposals/references.md](proposals/references.md), accepted). Covers
  the C struct, callback and included-header tasks below and the P0
  `fgets` buffer task. Each step leaves the suite green:
  - [x] R1 Core: C number types, Zig's pointer kinds, `load store offset
    cast size-of`, `ref` of anything (phrases: R7).
  - [x] R1b Non-null records, `or nothing`, narrowing (lists were never
    null; the prelude, examples and eval programs needed no change).
  - [x] R2 Import structs as C records (layout from clang), `a new T`
    (the header's own structs; `struct tm` from `time.h` needs R6).
  - [x] R3 Arrays (slices) in Core: `(list u8)`, `(array u8 N)`, `(view T
    P N)`, lists given to C as their items (phrases: R7).
  - [x] R4 Structs by value through C wrappers.
  - [x] R5 Callbacks, phrase values and prototypes on import, in Core
    (`phrase-of`, `call`, `(phrase …)`, `(c-phrase …)`; phrases: R7).
  - [x] R6 Headers beside the header and types its functions reach.
  - [x] R7 Phrases (`pointers.phr`, type words, phrase values) and docs.
  - [x] R8 Warning for a dropped result; `a copy of x` (deep): built with
    memory E2–E7 (`core::warn`, Core `copy`).

- [ ] Maps: keys of other types (records, decimals); `m at k` beside an
  operator asks (`counts at word + 1` reads two ways).
- [ ] C function-like macros other than one call (expressions, several
  statements): left out today.

## P2: model and tooling


- [ ] Windows support and testing, then add it to CI.

- [ ] macOS: libclang is now given its resource directory and the SDK
  (`platform_flags`) and the real-C-library test is unskipped. macOS CI
  builds now (`<algorithm>`); `qsort` and `gmtime` were missing because
  the SDK declares them in private `_*.h` files beside `stdlib.h` and
  `time.h`, which now count as the header. Confirm on the macOS CI job.
