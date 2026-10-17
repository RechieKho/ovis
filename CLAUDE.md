# Ovis

A compiler whose source code is plain English: a built-in LLM details and
translates paragraphs into Ovis Core, then LLVM compiles it.

Before working on this repository, read:
- `docs/state.md`: what is built, what is verified, environment notes.
- `docs/remaining-tasks.md`: prioritised open work.
- `docs/architecture-spec.md`: how it works and the invariants to keep.

When you finish a piece of work, update `docs/state.md` (status, test counts,
history) and `docs/remaining-tasks.md` (tick or add tasks). If you change how
something works, update `docs/architecture-spec.md` in the same commit.
To check these documents against the code and consolidate them, use the
`dream` skill (`.claude/skills/dream/SKILL.md`).

Build and test:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
./build/test_ovis
```

Code style: snake_case, `auto f() -> T`, parameters prefixed `p_`, comments
explain why. Match the surrounding code.
