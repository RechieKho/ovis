# History: M2 to the macOS CI job

Archived from docs/state.md → "History" on 2026-10-19 (dream at `53ad22a`).

| Commit | Summary |
| --- | --- |
| `c043cee` | GPU first: 7B is the default where a GPU runs it, 3B otherwise (`ovis model pull [3b|7b]`, lookup order); run-off questions reuse their header (10% less time a question on 4 CPUs) |
| `1ef7e55` | M2: records in phrases: `a NAME has:` with types and defaults, `a NAME with …`, `F of X`/`X's F`/`X.F` read and set, record-typed parameters; `field of T` slots with generic functions (`sorted by`, `sort … by`, `total … of`, `the T in xs with the highest/lowest F`); questions about unknown fields and `the NAME`; text ordered by `<`; `examples/library.ovis`, 3 eval programs |
| `5e67363` | M3: built-in traits: trait slot types (`T:comparable`, `for records:` Core, `=> records`), library rewritten (`sort`, `smallest of`, `largest of`, `contains`, `show`, `as text`), records implement them by `to …:` phrases; `<`, `=`, `+` call them; `traits.phr`, `traits.generic` |
| `3756d61` | M4.1, M4.3, M4.4: `(trait …)` and `(impl …)` in Ovis Core; a trait is a type; calls on a record call its impl, on a trait value dispatch through a table indexed by the record's hidden tag; mixed lists; untyped values get the trait whose phrases are used on them |
| `63809d4` | M4.2: `a NAME can:` and `a NAME is a TRAIT:` in phrases (phrases known before impls and across files), `examples/shapes.ovis` |
| (M5) | One-line phrase bodies: `to double (n): give back n * 2`, `area of (it): give back …`, split by the lexer (`split_one_line_blocks`) into a header and its one body line; a list of one record is already a list of its trait (test added) |
| (M5) | Mixed lists of records with several common traits: the list takes the first trait by name; the checker lets its items be asked for the others' phrases and the list be passed as a list of them when a record has both; the dispatch table's missing entry is a runtime error (`ovis_method_check`) |
| (P0) | D1–D3 `at the end`: `(defer …)` in Ovis Core (parser, elaborator's implicit return after trailing defers, checker rules, codegen frames copied to every exit), `at the end[,] STATEMENT` / `at the end:` / `defer` in phrases with the `Compiler:` note, `examples/notes.ovis`, `eval/programs/tidy.ovis` |
| (P0) | M2 leftovers: `nothing` and `is nothing` (a `record` slot type), defaults of later records, a field given once; M3 leftovers: hint for a missing `comes before` / `is the same as`, `position of` / `count of` over `equatable`, lists of records shown through `text of`, `a book is comparable:` |
| (P1) | `proposals/memory.md` accepted: explicit, strict memory (`free`, `with new memory:` arenas, `keep`, lost/stale value checks, checked mode; plan E1–E7), replacing a first draft that recommended Boehm GC. Measured the leak: 10 million passes joining text peak at 783 MB |
| (CI) | macOS: build fixes for libc++ (the daemon's `exchange` helper renamed `ask_daemon` so `std::exchange` is not picked through a `path` argument; file times cast from `__int128`; `<unistd.h>` in `daemon_test.cpp`; reals in S-expressions read with `strtod`, since Apple libc++ has no floating-point `from_chars`), and a macOS 15 job in `ci.yml` |
