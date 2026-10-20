# M1.10 modules and C, M1.11 CLI

Archived from remaining-tasks.md → "M1: phrase front end, no records" on 2026-10-18 (dream at `1ce0e5c`). Finished in the commit after `1ce0e5c`: `load_phrase_program`, `compile_phrase_program`, `use "x.h" from "lib"`, `ref x`, `ovis run/build --phrases FILE|FOLDER` (see phrase-front-end.md).

- [ ] **M1.10 Modules and C**: helper files may only define (phrases,
  functions, `use-c`, constant lets), as now. `use-c` functions are called
  as `name(args)` (phrase-grammar §7). Reuse `modules.cpp` for name
  collisions.
- [ ] **M1.11 CLI**: `ovis run/build --phrases`, `--model-budget`;
  questions the model couldn't settle are printed with their options.
