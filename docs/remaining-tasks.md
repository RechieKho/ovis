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

## P2: language features

### Sized numbers and bits

Done (N1–N7, decisions B1–B7): state.md history and
[proposals/numbers.md](proposals/numbers.md). Left from it, small:

- [x] Lists of lists of numbers of a size keep it, in Core and in
  English (`list of lists of bytes`).
- [x] Map keys of a size (`map of bytes to text`).
- [x] A stated result type for a `to` phrase (`gives an int32`).
- [ ] A 4-byte record tag (B5), if a program's records show it matters.

### Generics

Accepted (decisions G1–G8): [proposals/generics.md](proposals/generics.md).
Build its plan (§10) in order; each step leaves the suite green.

- [ ] G1: Core generic functions (`forall`, `(var T)`), rigid slots,
  instantiation at calls, bounds checked once, the monomorphise pass.
- [ ] G2: generic records (`(record stack int)`), a record and tag per
  use; traits on generic records.
- [ ] G3: phrases: `any NAME`, bounds, `a stack of (item) has:`, type
  words `stack of books`; `examples/stack.ovis`.
- [ ] G4: numbers and text implement traits statically.
- [ ] G5: typed memory for type variables (`alloc`, `alloc-more`).
- [ ] G6: the library on generics (per-type prelude, `.generic` files,
  `for records:` lines, `copy_for_sized_lists`).
- [ ] G7: `examples/collections/` (a growing list, a hash table),
  measured against the built-ins; then decide on replacing them.
- [ ] G8: C macros as generic C phrases, copied on demand by the
  arguments' C types; any arity, `...`, type and field parameters,
  statement macros with `ref`.

## P2: model and tooling

- [ ] Windows support and testing, then add it to CI.
