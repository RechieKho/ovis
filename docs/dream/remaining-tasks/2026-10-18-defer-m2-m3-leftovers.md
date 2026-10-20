# Finished: D1-D3 (at the end), the M2 and M3 leftovers

Moved word for word out of `docs/remaining-tasks.md` on 2026-10-18, once
done (the commits are on `claude/english-llvm-frontend-pso2fc`: "D1-D3",
"M2: …", "M3: …").

### M2: records

Done (types.md §1): Ovis Core (M2.1), the surface (M2.2), `field of T`
slots (M2.3), questions (M2.4), an example and eval programs (M2.5). See
state.md history, architecture-spec §4–§6 and
[dream/remaining-tasks](dream/remaining-tasks/INDEX.md). Left from it:

- [ ] A record field of its own kind (`next (node)`) can't be given a value
  in phrases: there is no `nothing` for records yet (Core has null).
- [ ] Defaults are read before other records' fields, in file order, so a
  default can't be `a point with …` of a record declared later.
- [ ] Unknown-field candidates in `a NAME with …` still offer fields
  already given in the line.

### M3: built-in traits

Done (types.md §2, "Built-in traits"): M3.1 and M3.2. See state.md history,
architecture-spec §4 and [dream/remaining-tasks](dream/remaining-tasks/INDEX.md).
Left from it:

- [ ] A record without the phrase (`sort books`, `books contains b`) gives
  an "unclear" report offering unrelated readings (`show books`). Say
  instead: "a book can't be sorted: write `to (a: book) comes before (b:
  book):`".
- [ ] `position of` and `count of` still have a template per element type;
  records can't use them (they need `equatable` and a generic each).
- [ ] A list of records prints structurally even when the record has
  `text of`; `+` and `as text` of a list of records don't join.
- [ ] `text of`, `comes before` and `is the same as` are written only as
  `to (a: book) …:` with stated record types; `a NAME is a TRAIT:` writes the impl of a user trait (M4.2); built-in
  traits are still implemented by `to (a: book) …:`.


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

