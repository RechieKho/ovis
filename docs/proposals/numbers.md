# Proposal: sized numbers and bits

Status: **proposed** (open decisions at the end, each with a
recommendation). The plan is N1–N7 in
[remaining-tasks.md](../remaining-tasks.md). It builds on the C numbers of
[references.md](references.md) (`int32`, `byte`, `(list u8)`, `(load i32
P)`), which already exist at the C boundary, and on the checked
arithmetic of checked builds.

## Why

Ovis has two numbers: a whole number (`int`, i64) and a decimal (`float`,
f64). That keeps programs simple, but the author can't say how much memory
a value takes:

- a record of a pixel (`red`, `green`, `blue`, `alpha`) is 32 bytes, not 4;
- a list of a million samples is 8 MB where `float32`s would be 4 MB and
  `int16`s 2 MB;
- hashes, checksums, file formats and network protocols need 32-bit
  unsigned arithmetic that wraps, shifts, masks and bit counts, none of
  which Ovis has (no `xor`, no shift, no hex literal).

The C numbers of references.md only live behind pointers and in C arrays;
an Ovis record, variable or parameter can't be one.

## 1. Principle: simple by default, sized where stated

Three layers. A program uses only as many as it needs.

1. **Nothing written.** `whole number` and `decimal` stay 64 bits with
   overflow checked. Every program today keeps its meaning, and the
   tutorial never mentions sizes.
2. **A size written where a value is kept.** A field, a list's items, a
   parameter, a result or a variable may say `int32`, `byte`, `float32`…
   The value is kept at that size, and arithmetic on it is checked at that
   size.
3. **Bits.** Phrases for masks, shifts, rotations and counts, wrapping
   arithmetic, hex and binary literals and output, for the programs that
   need them.

A size is never inferred from a value's magnitude, and never chosen by the
model: it is either written or it is 64 bits.

## 2. The sized types

| In words | Also | Core | LLVM |
| --- | --- | --- | --- |
| `int8`, `8-bit whole number` | | `i8` | i8 |
| `int16`, `16-bit whole number` | `short` | `i16` | i16 |
| `int32`, `32-bit whole number` | | `i32` | i32 |
| `int64`, `whole number` | | `int` | i64 |
| `uint8`, `unsigned 8-bit whole number` | `byte` | `u8` | i8 |
| `uint16`, `unsigned 16-bit whole number` | | `u16` | i16 |
| `uint32`, `unsigned 32-bit whole number` | | `u32` | i32 |
| `uint64`, `unsigned 64-bit whole number` | | `u64` | i64 |
| `float32`, `32-bit decimal` | | `f32` | float |
| `float64`, `decimal` | | `float` | double |

The words are the ones `pointer_type_words` reads already
(`parser.cpp`); they move to the general type words so they work in any
type position. `8-bit` needs the lexer to keep a number joined to `-bit`
as one word. Not planned: 128-bit, `float16`, big integers (§7).

Plurals follow the existing rule: `list of int16s`, `3 bytes`.

### Where a size is written

```
a pixel has:
    red (byte)
    green (byte)
    blue (byte)
    alpha (byte, 255)

samples = a new list of float32s
to checksum of (data: list of bytes) gives a uint32:
    ...
count = 0 as an int32
```

- **Fields, list items, parameters, results:** in the type positions that
  exist today.
- **Variables:** `x = VALUE as an int32`. `as` converts (§3) and,
  through inference, gives the variable its type. There is no separate
  declaration syntax.
- **An untyped variable** takes the type of the value it is given, as
  inference does now: `p = first pixel's red` is a byte. Arithmetic that
  mixes it with a whole number widens (§3), so `total = total + p` stays a
  whole number.

## 3. Arithmetic and conversions

Rules, in the order the checker applies them:

1. **Same type in, same type out.** `int32 + int32` is an `int32`,
   computed in 32 bits, overflow checked at 32 bits (`sadd/uadd/…
   with_overflow`, as i64 is now). Unsigned `/`, `div`, `%`, `<` use the
   unsigned instructions.
2. **Mixed sizes widen** to the smaller type that holds both: `int8 +
   int32` is `int32`, `byte + int16` is `int16`, `uint32 + int32` is
   `int64`. A whole number literal takes the other side's type if it fits
   (`b + 1` stays a byte), and is a build error if it doesn't (`b + 300`:
   "300 does not fit in a byte").
3. **`uint64` with a signed type** has no common type: a build error
   asking for `as a whole number` or `as a uint64` on one side.
4. **Decimals:** `float32` with `float32` stays `float32`; with a decimal
   it is a decimal; any whole number with a `float32` is a `float32` (as
   whole numbers stand for decimals now).
5. **Narrowing where a value is stored** (a field, an item, an argument, a
   variable of a stated type) is allowed and **checked**: in checked builds
   a value that doesn't fit is an error naming the line, the value and the
   type, like overflow. `--fast` truncates. Literals are checked at build
   time. A decimal into a `float32` rounds, unchecked.
6. **Explicit conversions:**
   - `x as an int16` (Core `(as i16 X)`): checked as in rule 5;
   - `x wrapped to a byte` (Core `(wrap u8 X)`): keeps the low bits, never
     an error (`300 wrapped to a byte` is 44);
   - `x clamped to a byte` (Core `(clamp u8 X)`): the nearest value that
     fits (`300` is 255, `-4` is 0).
7. **Wrapping and saturating arithmetic** are written, never a mode:
   `h * 16777619 wrapping` (Core `*%`, `+%`, `-%`, as Zig), `a + b
   clamped` (Core `+|`, `-|`). Both keep the operands' type.

Printing a sized whole number prints its value (`255`, not `-1`, for a
byte); a `float32` prints like a decimal from its widened value.

## 4. Bits

On any whole number type; the result has the operand's type (for two
operands, rule 2's). Bits are numbered from 0, the lowest (decision B3).

| Phrase | Core |
| --- | --- |
| `a bitwise and b`, `a bitwise or b`, `a bitwise xor b` | `(bit-and A B)` `(bit-or A B)` `(bit-xor A B)` |
| `bitwise not x`, `x with its bits flipped` | `(bit-not X)` |
| `x shifted left by n`, `x shifted right by n` | `(shl X N)`, `(shr X N)`: arithmetic for signed, logical for unsigned |
| `x rotated left by n`, `x rotated right by n` | `(rotl X N)` `(rotr X N)` (LLVM `fshl`/`fshr`) |
| `bit n of x` (a truth value) | `(bit X N)` |
| `x with bit n set`, `… cleared`, `… flipped`; `set bit n of x` | `(bit-set X N)` `(bit-clear X N)` `(bit-flip X N)` |
| `the low n bits of x`, `bits a to b of x` | `(bits X LOW HIGH)` |
| `the number of set bits in x` | `(popcount X)` (`ctpop`) |
| `the leading zeros of x`, `the trailing zeros of x` | `(clz X)` `(ctz X)` |
| `x with its bytes reversed` | `(bswap X)` |
| `the bits of d` / `d's bits as a uint64`, `the decimal with bits b` | `(bitcast u64 D)`, `(bitcast float B)` |
| `the bytes of x`, `the int32 from bytes bs` (little-endian; `… big-endian`) | prelude functions over `(list u8)` |

A shift by the type's width or more is an error in checked builds (LLVM
gives poison), and is masked in `--fast`.

**Literals:** `0x1F`, `0b1010_0001`, `0o17`, and `_` between digits of
any number (`1_000_000`). The lexer keeps the digits as written; Ovis
never rewrites them to decimal.

**Output:** `x in hex`, `x in binary`, `x in hex with 8 digits` (text);
`hex text to whole number` for input. A byte in hex is two digits by
default, an `int32` eight: the type's width.

## 5. Memory layout

- **Records** lay out sized fields at their size, in the order written,
  with C's alignment, so an Ovis record and a C struct of the same fields
  have the same layout. A pixel is 4 bytes after the hidden record tag
  (decision B5 on the tag's size). Reordering fields to save padding is
  not done: the order written is the layout (packing, B6, later).
- **Lists** reuse the C-number lists of references.md: `value_type::pointee`
  on a list already keeps items at a C size. A `list of int16s` is that
  list; reading an item gives an `int16` (today it widens to a whole
  number).
- **Maps** keep 8-byte keys and values; a sized key or value is widened in
  and narrowed out. No memory saving there, by design (the table is the
  runtime's).
- **Locals** are LLVM values of their type.

## 6. The front end and the library

- **Type words** (`type_words`): the sized words in every type position,
  `value_kind::exact` carrying the Core type as pointers do now.
- **Slots:** a template slot `int` accepts any whole number type and
  `float` any decimal, so the 138 templates keep working. Their Core
  results follow §3: `x squared` on an `int16` is an `int16`.
- **Prelude functions** over lists are per element type (`prelude_sum_int`).
  The `.generic` copies (per record and field now) are extended to copy
  per element type, `prelude_sum__i16`, made only when used.
- **Questions:** sizes add no question kind. `unknown_phrase` candidates
  include the new phrases; a size word misspelled (`int23`) is an
  `unknown_name` with the type words as candidates.
- **C:** a C `int` parameter is an `int32`: passing a whole number narrows
  by rule 5 (checked) instead of truncating silently, and C's results keep
  their type (an `unsigned` stays a `uint32`).

## 7. Considered and rejected

- **A program-wide default size** (`whole numbers are 32 bits` at the top
  of a file). It changes the meaning of every line and of the prelude, and
  a module could not be read alone. Sizes are written per value.
- **Storage-only sizes** (every computation in 64 bits, sizes only for
  fields and items). Simpler, but `uint64` can't be computed in a signed
  i64, and 32-bit wrapping hashes would need a conversion on every line.
- **Implicit wrapping** (C's unsigned arithmetic). Overflow stays an error
  unless the line says `wrapping`.
- **Inferring a size from values** (a field that only holds 0–255 becomes
  a byte). It hides the layout, and a later value would change it.
- **128-bit integers, `float16`, arbitrary precision:** not until a program
  needs them; LLVM has the first two.

## 8. Plan

| Step | What | Done when |
| --- | --- | --- |
| N1 | Bits on whole numbers (64 bits): Core bit forms, §4 phrases, hex/binary/`_` literals, `in hex`/`in binary` | `examples/bits.ovis` (flags, a CRC with masks); `phrase_library.txt` cases |
| N2 | Sized scalars in Core: `value_type` gets a number type (width, signed); codegen, checked overflow per size, unsigned ops, `as`/`wrap`/`clamp`, printing | Core tests per type, overflow and narrowing errors with lines |
| N3 | Sized types in words: type words in every position, lexer `32-bit`, `as an int32`, §3's checker rules and messages | `whole number` programs unchanged (all tests pass); a program of each rule |
| N4 | Records with sized fields at their size and C alignment; Ovis records and C records share layout code | `size of a pixel` is 4 (+ tag); a pixel passed to C by pointer |
| N5 | Lists of sized numbers unified with C-number lists; per-element prelude copies; sorting them (the open "arrays of C numbers can't be sorted") | `sum of`, `sort`, `largest of` on `list of int16s`; memory measured |
| N6 | Wrapping and clamped arithmetic, `bitcast`, bytes and endianness, C results keep their types | `examples/hash.ovis` (FNV-1a 32-bit), a WAV or BMP header written |
| N7 | Eval programs and questions with sizes; the `int23` question; docs | eval suites unchanged or better |

N1 is independent of the rest and useful alone; N2–N3 are the core and go
together; N4–N6 can be done in any order after them.

## Open decisions

| # | Question | Recommendation |
| --- | --- | --- |
| B1 | Narrowing at a store: implicit and checked, or an `as` required? | Implicit, checked (rule 5), with a note sigil only for literals that don't fit |
| B2 | Mixed signed and unsigned of the same size (`uint32 + int32`) | Widen to `int64` (rule 2); only `uint64` with signed is an error |
| B3 | Bit numbering: from 0 (datasheets, `2^n`) or from 1 (like text positions)? | From 0, said in the phrase docs |
| B4 | English size words beyond `int32`: `32-bit whole number`, `short`, `byte` | Yes for `N-bit` and `byte`; no `short`/`long` (C's sizes vary) |
| B5 | The hidden record tag (8 bytes now) | Keep it 8 bytes for now; a 4-byte tag later if layouts show it matters |
| B6 | Packed records (`a header has, packed:`) for file formats | Later, with N6, if a format example needs it |
| B7 | An untyped variable given a sized value: that size, or a whole number? | That size (inference as now), widening on mixed arithmetic |
