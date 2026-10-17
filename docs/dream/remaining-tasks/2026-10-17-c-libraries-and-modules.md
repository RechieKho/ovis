# Finished: C library import and modules

Archived from `docs/remaining-tasks.md` → "P2: language features" on
2026-10-17 (dream at `ff0234b`).

- [ ] `(include "header.h")`: import C declarations with libclang instead
  of `extern`.
- [ ] Modules: more than one source file per program.

Done in:
- `d0fa4d1`: `(use-c "header.h" "library")` with libclang, instead of
  `(include ...)` as first proposed.
- `ff0234b`: modules. A folder with `main.ovis` is the program, and
  subfolders are nested modules.
