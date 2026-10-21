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

- [ ] A 4-byte record tag (B5), if a program's records show it matters.

### Generics

Done (G1–G8, accepted decisions in
[proposals/generics.md](proposals/generics.md)): state.md history and
[dream/remaining-tasks](dream/remaining-tasks/INDEX.md). The built-in list
and map stay until `item` is inlined and the table is packed (state.md,
"collections measured").

## P2: model and tooling

- [ ] Windows support and testing, then add it to CI (and an ovisup for
  it, as rustup-init.exe).
- [ ] `architecture-spec.md` is near 500 lines with little inactive: move
  §8's memory and checked builds into their own document, as
  model-backends.md was.

### Deployment

Done: ovisup, relocatable packages, the release workflow
([deployment.md](deployment.md), state.md history). Next:

- [ ] Merge to main so the workflow's first run publishes `nightly`; fix
  what the macOS and arm64 Linux packaging find there.
- [ ] Tag `v0.1.0` (the version in CMakeLists.txt) once nightly installs
  everywhere, so `stable` exists.
- [ ] A toolchain file in a project (`ovis-toolchain`, as
  `rust-toolchain.toml`), read by the shim walking up from the current
  folder, so a project builds with the toolchain its lockfile was made by.
- [ ] macOS x86-64 packages, if anyone needs them (a `macos-15-intel`
  runner); musl or older-glibc Linux packages (build in an older container)
  if glibc 2.39 proves too new.
- [ ] Smaller packages: libLLVM is 118 MB of the 220 MB unpacked, and
  libclang needs it as a shared library; strip the bundled libraries, or
  build LLVM with only the targets and libclang parts Ovis uses.
