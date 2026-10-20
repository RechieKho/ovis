# Proposal: sized numbers and bits

Status: **accepted and built** (N1–N7, 2026-10-20; decisions at the end).
Built as written, except where "Built:" notes say otherwise. It builds on
the C numbers of [references.md](references.md) (`int32`, `byte`, `(list
u8)`, `(load i32 P)`) and on the checked arithmetic of checked builds.

## Why

Ovis had two numbers: a whole number (`int`, i64) and a decimal (`float`,
f64). That keeps programs simple, but the author couldn't say how much
memory a value takes:

- a record of a pixel (`red`, `green`, `blue`, `alpha`) took 32 bytes of
  fields, not 4;
- a list of a million samples was 8 MB where `float32`s are 4 MB and
  `int16`s 2 MB;
- hashes, checksums, file formats and network protocols need 32-bit
  unsigned arithmetic that wraps, shifts, masks and bit counts, none of
  which Ovis had (no `xor`, no shift, no hex literal).

## 1. Principle: simple by default, sized where stated

Three layers. A program uses only as many as it needs.

1. **Nothing written.** `whole number` and `decimal` stay 64 bits with
   overflow checked. Every program keeps its meaning.
2. **A size written where a value is kept.** A field, a list's items, a
   parameter or a variable (`x = 0 as an int32`) may say `int32`, `byte`,
   `float32`… The value is kept at that size, and arithmetic on it is
   checked at that size.
3. **Bits.** Phrases for masks, shifts, rotations and counts, wrapping and
   clamped arithmetic, hex and binary literals and output, bytes.

A size is never inferred from a value's magnitude, and never chosen by the
model: it is either written or it is 64 bits.

## 2. The sized types

| In words | Core | LLVM |
| --- | --- | --- |
| `int8`, `8-bit whole number`, `signed byte` | `i8` | i8 |
| `int16`, `16-bit whole number`, `short` | `i16` | i16 |
| `int32`, `32-bit whole number` (or `integer`) | `i32` | i32 |
| `int64`, `whole number`, `long`, `64-bit whole number` | `int` | i64 |
| `uint8`, `byte`, `unsigned byte`, `unsigned 8-bit whole number` | `u8` | i8 |
| `uint16`, `unsigned short` | `u16` | i16 |
| `uint32`, `unsigned 32-bit integer` | `u32` | i32 |
| `uint64`, `unsigned long` | `u64` | i64 |
| `float32`, `32-bit decimal` | `f32` | float |
| `float64`, `decimal`, `64-bit decimal` | `float` | double |

`number_type_words` (`parser.cpp`) reads them in every type position;
`32-bit` is a number, `-` and `bit`. Plurals follow the existing rule:
`list of int16s`, `3 bytes`. In Core a sized number is `base_type::integer`
or `real` with its C number in `value_type::pointee`, as a C array's items
already were. Not planned: 128-bit, `float16`, big integers (§7).

### Where a size is written

```
a pixel has:
    red (byte)
    alpha (byte, 255)

samples = a new array of 5 int16s
to brighten (p: pixel) by (amount: byte): …
count = 0 as an int32
```

- **Fields, list items, parameters:** in the type positions that exist.
- **Variables:** `x = VALUE as an int32`; inference gives the variable its
  type (B7). An untyped variable takes the type of the values it is given,
  joined as inference does (`x = b` then `x = n` is a whole number).
- Built: a `to` phrase has no stated result type (`gives` is only for a
  trait's phrases); `give back n as an int32` says it.

## 3. Arithmetic and conversions

1. **Same type in, same type out.** `int32 + int32` is an `int32`, computed
   in 32 bits, overflow checked at 32 bits (the error names the type and
   its range). Unsigned `/`, `div`, `%`, `<`, `min` use the unsigned
   instructions. The smallest signed number divided by -1 is an overflow.
2. **Mixed sizes widen** to the smaller type that holds both (`common_number`
   in `ast.cpp`): `int8 + int32` is `int32`, `uint32 + int32` is `int64`
   (B2). A number written in the program (or arithmetic of only such
   numbers, `2 * 3`) takes the other side's type if it fits (`b + 1` stays
   a byte) and is a build error if it doesn't (`300 doesn't fit in a byte
   (0 to 255)`); in a comparison it widens instead (`b < 256`).
3. **`uint64` with a signed type** has no common type: a build error asking
   for `as a whole number` or `as a uint64`.
4. **Decimals:** `float32` with `float32` or any whole number stays
   `float32`; with a decimal it is a decimal. `/` of whole numbers is a
   decimal.
5. **Narrowing where a value is stored** (a field, an item, an argument, a
   variable, a C function's argument) is allowed and **checked** (B1): in
   checked builds a value that doesn't fit stops the program naming the
   value and the type; `--fast` truncates. Numbers written in the program
   are checked when it is built. A decimal into a `float32` rounds.
6. **Conversions:** `x as an int16` (Core `(as i16 X)`, checked; also
   `x as a decimal`, `x as a whole number`, which replaced the two
   templates of their own), `x wrapped to a byte` (`(wrap-to u8 X)`, the
   low bits), `x clamped to a byte` (`(clamp-to u8 X)`, the nearest value).
   A decimal to a whole number goes toward zero.
7. **Wrapping and clamped arithmetic** are written, never a mode:
   `((h bitwise xor b) * 16777619) wrapping` (Core `(wrapping E)`: E's `+`,
   `-`, `*` keep the low bits) and `(a + b) clamped` (`(saturating E)`).
   Built: one form around an expression instead of `+%`, `*%`, `+|`.

Printing a sized whole number prints its value (`255`, not `-1`, for a
byte; a `uint64` as unsigned); a `float32` prints the fewest digits that
read back as it (`0.1`).

## 4. Bits

On any whole number type; the result has the operand's type (two operands,
rule 2's). Bits are numbered from 0, the lowest (B3). Core names are
hyphenated, so no program's phrase can take them.

| Phrase | Core |
| --- | --- |
| `a bitwise and b` (`a masked with b`), `a bitwise or b`, `a bitwise xor b` (`a exclusive or b`) | `bit-and` `bit-or` `bit-xor` |
| `bitwise not x`, `x with its bits flipped` | `bit-not` |
| `x shifted left by n [bits]`, `… right …` (arithmetic for signed, logical for unsigned) | `shift-left` `shift-right` |
| `x rotated left by n`, `… right …` | `rotate-left` `rotate-right` |
| `bit n of x`, `x has bit n set` | `bit-test` |
| `x with bit n set` / `cleared` / `flipped` | `bit-set` `bit-clear` `bit-flip` |
| `the low n bits of x`, `bits a to b of x` | `bit-field X LOW HIGH` |
| `the number of set bits in x`, `the leading zeros of x`, `the trailing zeros of x` | `count-ones` `leading-zeros` `trailing-zeros` |
| `x with its bytes reversed` | `byte-swap` |
| `the bits of d`, `the decimal with bits b` | `float-bits` `bits-float` |
| `the bytes of x [lowest first]`, `… highest first`, `the bytes of text` | `to-bytes` `to-bytes-big` |
| `the int32 at byte i of bytes [highest first]` (from 1) | `from-bytes` `from-bytes-big` |
| `x in hex [with n digits]`, `x in binary …`, `text read as hex` | `to-hex` `to-binary` `from-hex` |

A bit's place outside the number is an error in checked builds (LLVM gives
poison) and masked in `--fast`. Built: `set bit n of x` (a statement) is
left out; `x = x with bit n set` says it.

**Literals:** `0x1F`, `0b1010_0001`, `0o17`, and `_` between digits
(`1_000_000`), read by `read_whole_number`; one above the largest signed
64-bit number is a `uint64`. **Output:** a number of a size shows all its
bits in hex or binary (a byte two digits), a whole number only those it
needs.

## 5. Memory layout

- **Records** lay out sized fields at their size, in the order written,
  with C's alignment (the module has the target's data layout before code
  is generated, so `size of` is the allocated size). A pixel is 4 bytes of
  fields after the 8-byte hidden tag (B5): 16 with padding.
- **Packed records** (B6): `a header has, packed:` (Core `(packed-record
  NAME …)`): no gaps between fields, and no hidden tag, so its memory is
  exactly its fields (`size of` a uint16, a byte and a uint32 is 7) and it
  can be written to a file as it is (`fwrite(h as a pointer, …)`). It
  can't implement a trait. Built: the tag was left out so that the
  record's address is its bytes.
- **Lists** are the C-number lists of references.md: a `list of int16s`
  keeps its items at 2 bytes and reading one gives an `int16`. They sort
  (`ovis_list_sort_number`). A phrase written for a list of whole numbers
  or decimals (the prelude's `sum of`, `largest of`, `contains`…, or a
  program's own) called with one runs a copy made for its items
  (`prelude_sum_int__i16`, by the checker).
- **Maps** keep 8-byte keys and values; values may be any size (widened in,
  narrowed out). Keys stay whole numbers, decimals or text.

## 6. The front end and the library

- **Type words** in every type position (§2); a parameter `(n: int16)`
  keeps its size.
- **Slots:** a template slot `int` accepts any whole number type and
  `float` any decimal. A `size` slot (`{t: size}`) takes a type of number
  only, so `x as an int32` doesn't read like `x as text`.
- **Questions:** sizes add no question kind. A size word misspelled
  (`int23`) gets edits with the number types within two letters
  (`5 as an int32`, `5 as an int16`…).
- **C:** a number given to C is narrowed by rule 5 (checked). Built: C's
  results and C records' fields stay whole numbers and decimals of 64 bits,
  as before: the author wrote no size for them (§1).

## 7. Considered and rejected

- **A program-wide default size** (`whole numbers are 32 bits`). It
  changes the meaning of every line and of the prelude.
- **Storage-only sizes** (every computation in 64 bits). `uint64` can't be
  computed in a signed i64, and wrapping hashes would need a conversion on
  every line.
- **Implicit wrapping** (C's unsigned arithmetic).
- **Inferring a size from values.** It hides the layout.
- **Keeping C's result types** (an `unsigned` result a `uint32`): sizes the
  author never wrote would make `rand() * 1000` overflow.
- **128-bit integers, `float16`, arbitrary precision:** not until a program
  needs them.

## Decisions

Decided with the owner on 2026-10-20:

| # | Question | Decision |
| --- | --- | --- |
| B1 | Narrowing at a store | Implicit, checked when the program runs; numbers written in the program checked when it is built |
| B2 | `uint32 + int32` | A whole number of 64 bits |
| B3 | Bit numbering | From 0 |
| B4 | English size words | Yes: `N-bit whole number`/`integer`/`decimal`, `byte`, `short`, `long`, `unsigned`/`signed …` |
| B5 | The hidden record tag | Stays 8 bytes |
| B6 | Packed records | Now, with no tag |
| B7 | An untyped variable given a sized value | That size, joined with its other values |
