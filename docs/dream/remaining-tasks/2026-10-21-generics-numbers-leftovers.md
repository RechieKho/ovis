# Finished: generics G1–G8, numbers leftovers

Archived from docs/remaining-tasks.md → "P2: language features" on 2026-10-21 (dream at 5dd263d). Generics were finished in the commits up to adae140 and 809c0ef (docs) but never ticked here; ticked on archiving (state.md "Status at a glance" and history). The numbers leftovers were finished in dd41abc.

## Sized numbers and bits (left from N1–N7)

- [x] Lists of lists of numbers of a size keep it, in Core and in
  English (`list of lists of bytes`).
- [x] Map keys of a size (`map of bytes to text`).
- [x] A stated result type for a `to` phrase (`gives an int32`).

## Generics

Accepted (decisions G1–G8): [proposals/generics.md](proposals/generics.md).
Build its plan (§10) in order; each step leaves the suite green.

- [x] G1: Core generic functions (`forall`, `(var T)`), rigid slots,
  instantiation at calls, bounds checked once, the monomorphise pass.
- [x] G2: generic records (`(record stack int)`), a record and tag per
  use; traits on generic records.
- [x] G3: phrases: `any NAME`, bounds, `a stack of (item) has:`, type
  words `stack of books`; `examples/stack.ovis`.
- [x] G4: numbers and text implement traits statically.
- [x] G5: typed memory for type variables (`alloc`, `alloc-more`).
- [x] G6: the library on generics (per-type prelude, `.generic` files,
  `for records:` lines, `copy_for_sized_lists`).
- [x] G7: `examples/collections/` (a growing list, a hash table),
  measured against the built-ins; then decide on replacing them.
- [x] G8: C macros as generic C phrases, copied on demand by the
  arguments' C types; any arity, `...`, type and field parameters,
  statement macros with `ref`.
