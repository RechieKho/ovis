# M3: built-in traits (finished)

Archived from remaining-tasks.md → "M3: built-in traits" on 2026-10-18 (dream at 87cdf33).

### M3: built-in traits

types.md §2, "Built-in traits".

- [ ] **M3.1** Template slot types (`num`, `any`, ...) become built-in
  traits; rewrite the library against trait slots, with no change to
  existing programs (all eval programs still build identically).
- [ ] **M3.2** `showable`, `comparable`, `equatable`: a record implementing
  one works with `show`, `sort`, `smallest of`, `=` and `contains`.
  Concrete types are known here, so dispatch is static. Today `sort {xs: T[]}`
  accepts a list of records and the checker rejects it later; field
  slots' type constraint (`num field of T`) is a
  stand-in for traits.
