# Ovis

A compiler whose source code is plain English: a deterministic parser reads
lines made of phrases into Ovis Core, a built-in LLM chooses between
readings when a line is unclear (or rewrites it in words the parser reads,
which is checked), then LLVM compiles it.

Before working on this repository, read the tracking documents:
- `docs/state.md`: what is built, what is verified, environment notes.
- `docs/remaining-tasks.md`: prioritised open work.
- `docs/architecture-spec.md`: how it works and the invariants to keep
  (its model backends in `docs/model-backends.md`, records and traits in
  `docs/records-and-traits.md`, C libraries in
  `docs/c-libraries.md`, the Ovis Core reference in `docs/ovis-core.md`,
  questions and rewrites in `docs/questions.md`, release packages and
  ovisup in `docs/deployment.md`, projects and packages in
  `docs/projects.md`, memory at run time in `docs/memory.md`).

Inactive information is archived in `docs/dream/<document>/`. Search
`docs/dream/*/INDEX.md` when you need history; don't read archives by
default.

Rules:
- When you finish a piece of work, update `docs/state.md` (status, test
  counts, history) and `docs/remaining-tasks.md` (tick or add tasks). If you
  change how something works, update `docs/architecture-spec.md` in the same commit.
- **After implementing a feature, dream.** Follow
  `.claude/skills/dream/SKILL.md`: archive what is no longer active.
- **Every tracking document must stay under 500 lines.** The test suite
  fails otherwise.

Build and test:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
./build/test_ovis
```

Code style: snake_case, `auto f() -> T`, parameters prefixed `p_`, comments
explain why. Match the surrounding code.
