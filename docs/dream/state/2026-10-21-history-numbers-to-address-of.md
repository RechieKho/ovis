# History: numbers of a size to `address of`

Archived from docs/state.md → "History" on 2026-10-21 (dream at 5dd263d).

| Commit | Summary |
| --- | --- |
| (P2) | `address of x` reads as `ref x`, like `the address of x` (`pointers.phr` alias `[the] address of {x}`; `phrase_library.txt`) |
| (P2) | Maps take decimals as keys (`map of decimal numbers to text`; `-0.0` is `0.0`) |
| (P2) | C macros of one expression, as Zig reads them: tried by clang with whole numbers, decimals and text, each fitting combination a variant (`OVT_MAX__if`) the elaborator picks per call (replaced by G8's copies on demand) |
| (P2) | **Numbers of a size and bits** (numbers.md N1–N7, B1–B7): sized Core types (`i8`…`u64`, `f32` in `pointee`), `common_number`, soft number literals in inference, `as`/`wrap-to`/`clamp-to`, overflow per size, checked narrowing at stores and C arguments; bit operations, `0x`/`0b`/`0o`/`_` literals, hex and binary text; English type words and `{t: size}` slots; sized record fields (the module gets the target data layout first), packed records; lists of a size (items, sort, per-element copies of list phrases), map values; `wrapping`/`saturating`, `float-bits`, bytes; `fread`/`fwrite`; `examples/bits.ovis`, `examples/hash.ovis`, `eval/programs/checksum.ovis` |
| (P2) | **Bare blocks**, as Zig's: Core `(block [:LABEL] …)` (`do`, `begin`), a scope whose defers run when it is left; `(leave [:LABEL] [VALUE])`; `(let x (block …))` gives it a value. English `do:`, `NAME = do:`, `leave`, `leave with VALUE` (`block_test`) |
