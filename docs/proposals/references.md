# Proposal: pointers, C structs and references

Status: **accepted** (decisions in §12), not yet built; the plan is
R1–R8 (§11). It covers the P2 tasks on C structs, callbacks and
headers a header includes, the P0 task "buffers for C can't be made in
phrases", and phrases that change a caller's number. It builds on
[memory.md](memory.md) (who frees what) and [c-libraries.md](../c-libraries.md)
(the importer today).

## Why

The owner's aim: Ovis must be able to use any C library, and most of them
need pointers beyond an opaque handle. Today `use "x.h"` imports functions
whose types are numbers, text or `ptr` (an untyped `void *`); `ref x`
passes a variable's address as an output parameter. That covers SQLite's
handles and zlib's one-shot `compress`, but none of these:

| Library code | Needs | Today |
| --- | --- | --- |
| `struct tm *t = localtime(&now); t->tm_hour` | read a field through a returned pointer | `ptr`, no fields |
| `struct dirent *e = readdir(d); e->d_name` | a `char[256]` field read as text | same |
| `z_stream s = {0}; s.next_in = …; deflate(&s, Z_FINISH)` | make a struct, set fields, pass its address | can't make one |
| raylib `DrawCircleV(Vector2 center, float r, Color c)` | structs by value | skipped on import |
| `qsort(xs, n, sizeof *xs, compare)`, curl's write callback | an Ovis function as a C function pointer | only `null` |
| `fread(bytes, 1, n, f)`, `double *samples` | a typed array of C numbers, indexing | `buffer` in Core only |
| `png_bytep row = rows[y]; row[x * 4 + 1]` | pointer arithmetic, reading a byte at an offset | nothing |
| `struct tm` is in `time.h`, SDL across 80 headers | types from headers the header includes | the header itself only |

And one without C: `to increase (n) by (k):` can't change the caller's
`score`, because numbers are passed by value.

## Principles

- **Nothing is hidden.** A read or write through a raw pointer is written
  as one; copies at the C boundary are the calling convention's, not new
  values the author must free.
- **Typed first, raw when needed.** What the header says (a struct, an
  `int *`) is used, so most code reads fields and items. Raw reads,
  offsets and casts exist for the rest, as plain phrases.
- **Same words as records.** A C struct is read and set like a record
  (`t's tm_hour`), so authors learn one thing.
- **Memory follows [memory.md](memory.md).** What Ovis makes is in the
  current memory and freed with `free` or a memory; what C returns is the
  author's per the library's documentation (§7 there). Checked mode checks
  pointers into memory Ovis made.

## 1. C structs as types

| Option | Pros | Cons |
| --- | --- | --- |
| A. Opaque only (today) | Nothing to build | Most C libraries can't be used |
| **B. Import struct definitions as C records**: a complete struct (or its typedef) becomes a type with the C layout (clang's offsets), read and set with `F of X`, `X's F` and `X.F`; incomplete structs stay handles | Automatic; exact layout from libclang; same field syntax as records | Importer work: nested structs, arrays in structs, unions, bitfields |
| C. The author declares the layout (`a z_stream is C: …`), as Python's ctypes | No importer work | Layouts written by hand go wrong silently, per platform |

**Recommend B.** Details:
- A C record is a type like an Ovis record, by reference: its value is a
  pointer to the struct. So `struct tm *` *is* a `tm`, `struct tm **` is a
  pointer to a `tm` (§3). One a header returns is a `C pointer to a tm`,
  so it may be null; one Ovis makes is not.
- It has no hidden tag, so it can't implement a trait with dispatch (trait
  values need the tag, decision 1 of 2026-10-19); direct calls only.
- Fields: C numbers read as whole numbers or decimals (narrower types
  converted as for function results today); `char *` as text; `char[N]`
  as text read up to its NUL, set by copying at most N−1 bytes; other
  arrays as a C array (§4); a nested struct as a C record that points into
  its parent, valid while the parent is; a pointer field as a pointer (§3).
- Unions: imported, each member a field at offset 0. Bitfields: skipped
  (the struct imports without them, listed in `skipped`); later if needed.
- Names: `struct tm` and `typedef struct {…} Vector2` are `tm` and
  `Vector2`; a clash with an Ovis record is an error naming both.
- Making one: `stream = a new z_stream` gives a zeroed struct in the
  current memory, a new value under memory.md §2; `a new Vector2 with x
  3 and y 4` sets fields, as records do. `size of z_stream` gives
  `sizeof`.

## 2. Structs by value

| Option | Pros | Cons |
| --- | --- | --- |
| **A. Copy at the boundary**: a C record passed where C takes the struct by value is loaded from its pointer; a struct returned by value is copied into a new C record | Records stay by reference everywhere in Ovis; one model | A returned struct is a new value to free (or a memory); a small allocation per call |
| B. Value types in Ovis (copied on `=`) | No allocation; like C | Two kinds of record that behave differently on `=`; against types.md's "by reference" |

**Recommend A.** The C calling convention for structs differs per platform
(x86-64 splits small structs into registers, AArch64 has its own rules, Win64
passes by pointer), and LLVM leaves it to the front end:

| ABI option | Pros | Cons |
| --- | --- | --- |
| i. Our own rules per target | No new dependency | Easy to get subtly wrong; three rule sets to keep |
| ii. clang's CodeGen ABI (`clang::CodeGen::arrangeFreeFunctionCall`) | Exact | Links libclang-cpp (large), C++ API changes with every LLVM |
| **iii. A C wrapper per such function**, written by Ovis (`void ovis_w_DrawCircleV(Vector2 *c, float r, Color *col) { DrawCircleV(*c, r, *col); }`) and compiled with the C compiler Ovis already links with | Exact on every platform the C compiler knows; small | Builds that use them need the C compiler's headers for the library (they already need the header) |

**Recommend iii.** Only functions with structs by value get a wrapper.

## 3. Pointer types (decided: Zig's kinds)

| Option | Pros | Cons |
| --- | --- | --- |
| A. Only `ptr` (today), with the type written at each read | Simple types | Every read restates what the header already said; no checking |
| B. Typed pointers, one kind (`pointer to T`), as C | Simple | One-item, many-item and with-length look the same; no bounds |
| **C. Zig's kinds**: one item, many items, a slice (with a length), text, optional, C's | Each says what it may do; the build checks it | More kinds to learn |

Decided by the owner (2026-10-19): **C, Zig's pointer kinds**, with null
the middle path below. What an Ovis value already is, in these terms:
a record or C record variable is a pointer to one; a list is a pointer to
its header (`length`, `capacity`, `data`), like Zig's `ArrayList`; text is
`[*:0]u8`.

| Zig | Ovis type | May do | Core |
| --- | --- | --- | --- |
| `*T` | `pointer to a T` (a record or C record is one already) | read and set the value or its fields | `(ptr T)` |
| `[*]T` | `pointer to many T` | `item N of p`, `p moved by N`; no length, no bounds | `(many T)` |
| `[]T` | `array of T`: a pointer and a length (§4) | items checked against the length, `for each`, `length of` | `(array T)` |
| `[*:0]u8` | text | as today | `str` |
| `?*T` | `T or nothing` (any of the above) | only `is nothing`; inside `if p is not nothing:` it is a `T` | `(maybe K)` |
| `[*c]T` | `C pointer to T`: what headers give | can be null, one or many: everything, unchecked at build time | `(cptr T)` |
| `*anyopaque` | `pointer` (C's `void *`) | pass, compare, `as` | `ptr` |

**Null, the middle path.** Ovis's own pointers, records and lists are
never null unless the type says `or nothing`; pointers from C headers are
`C pointer`s, which may be, as in Zig's `[*c]T`.

- Narrowing: after `if p is nothing: give back …` or inside `if p is not
  nothing:`, `p` is the non-null type, as Zig's `if (p) |q|`. Giving a `C
  pointer to T` where a `pointer to a T` is wanted needs that check or
  `p as a pointer to a T` (checked: null is a run-time error at that line).
- Reading through a `C pointer` that is null is a run-time error with the
  line in checked mode.
- **Change to today:** a record variable declared without a value is null
  now. It becomes a build error unless its type says `or nothing` (`best
  = a book or nothing`); `is nothing` needs an `or nothing` type or a C
  pointer. Records' fields that may be missing are `or nothing` too.
  The prelude, examples and eval programs move to it (R1b, §11).
- Converting is written: `p as a pointer to many int32s`, `p as a tm`,
  `p as a pointer`. A `pointer to a T` and a `pointer to many T` go where
  C takes `T *`; any pointer goes where C takes `void *`.

## 4. Arrays and buffers (decided: Zig's slice)

`array of T` is the slice: a pointer, a length, an item type.

- Made by `a new array of 256 bytes` (zeroed, in the current memory, a new
  value under memory.md §2) or seen over memory already there: `the 100
  int32s at p` (from a `pointer to many` or a C pointer), `part of xs
  from 10 to 20` (a slice of a slice, no copy).
- `item N of xs` and `for each` are checked against the length; it passes
  to C as its pointer (the length is the author's to pass, as C needs).
- An Ovis list gives a slice of its items, `the items of xs` (Zig's
  `list.items`), when the item types match C's (64-bit whole numbers,
  decimals as `double`, text as `char *`). It is valid until the list
  grows (`add` may move the items: memory.md's checked mode catches its
  use after). A list of whole numbers where C wants `int *` is an error
  that suggests an array of int32s.
- `text of xs` reads a byte array as text up to its NUL or length. It
  replaces Core's `buffer` in phrases (the P0 task on `fgets`).

## 5. Raw pointer operations

What C code does with a pointer, in Ovis. The question is where they are
allowed:

| Option | Pros | Cons |
| --- | --- | --- |
| A. Ovis Core only | Phrases stay safe | Authors can't port C code that needs them; the owner's aim fails |
| **B. Phrases, everywhere** | Any C library can be used from a program | Wrong offsets crash, as in C |
| C. Phrases, only inside a marked block (`using raw pointers:`) | Shows where the danger is, as Rust's `unsafe` | Ceremony around every small read; the words already say it |

**Recommend B.** The phrases (Core in brackets):

| Phrase | Meaning | Core |
| --- | --- | --- |
| `the int32 at p` | read a C value of the type at `p` (any C number type, a typed pointer, or a C record) | `(load i32 p)` |
| `the int32 at p = 5` | write it | `(store i32 p 5)` |
| `p moved by 3 int32s`, `p moved by 12 bytes` | pointer arithmetic, scaled by the item size or in bytes | `(offset p 3 i32)` |
| `p as a pointer to a tm`, `p as a whole number` | change the type; the address as a number (to print or compare) | `(cast …)` |
| `ref x`, `ref t's tm_hour`, `ref item 3 of xs` | an address (§6) | `(ref …)` |
| `size of tm`, `size of int32` | `sizeof` | `(size-of T)` |
| `p is nothing`, `p = q` | null and identity | today's |

C number types, by header name (`uint8_t`, `int32_t`, `size_t`, `uLong`)
or by word: `byte` (u8), `int8 … int64`, `uint8 … uint64`, `float32`,
`float64`. A read gives a whole number or a decimal; a write converts and
fails the build if a literal doesn't fit.

Checked mode (memory.md §6): a pointer into memory Ovis made is checked
against that allocation's bounds and freed state on every read and write;
a pointer into C's memory isn't (Ovis can't know its size), except through
a C array's length. `--fast` drops the checks.

## 6. Addresses (`ref`) (decided: C's)

| Option | Pros | Cons |
| --- | --- | --- |
| A. Variables, as C arguments only (today) | Can't dangle | No `&s.avail_out`, no `&xs[3]` |
| B. Also fields and items, which may be stored; a variable's address only as a C argument | No dangling address of a local | Two rules (stored or not) |
| **C. Any address anywhere, as in C and Zig** | One rule; pointers are ordinary values | A local's address can outlive it |

Decided by the owner (2026-10-19): **C, for C's simplicity.** `ref x`
takes the address of a variable, a field or an item; the result is a
typed pointer (§3), a value like any other: stored, given back, passed to
C or to an Ovis phrase. Zig does the same (`&x`, with a stack address
outliving its function undefined, unchecked).

- Giving back the address of the phrase's own variable is a build error
  (what C compilers warn about); other ways a local's address outlives it
  are the author's to avoid, as in C.
- Checked mode catches an address into Ovis memory used after it is freed;
  a stale stack address is not caught.
- `ref item 3 of xs` points into the list's items, which move when the
  list grows, as a pointer into Zig's `ArrayList` does: checked mode
  reports its use after the move (the old items count as freed).

## 7. Callbacks

| Option | Pros | Cons |
| --- | --- | --- |
| **A. A `to …:` phrase given as a C function pointer**: Ovis writes a C-compatible function for it (converting `int` ↔ whole number, `const void *` ↔ pointer, by the header's prototype); no captured variables; C's `void *user_data` carries an Ovis value, read back with `as` | Covers `qsort`, `sqlite3_exec`, curl, GLFW; nothing hidden | Captured variables must go through user data |
| B. Closures (capture variables) via trampolines | Natural | Generated executable memory or libffi; lifetimes of captured variables unclear |
| C. Only C functions as callbacks (today, plus C function names) | Nothing to build | Ovis can't be called back |

**Recommend A.** The phrase is named by its words with `_` for its slots,
as an argument where C takes a function pointer: `qsort(numbers, count,
8, compare _ and _)`. The build checks its parameters and result against
the prototype and fails with both shown. `book as a pointer` and
`user_data as a book` pass a record through `void *`; in checked mode `as
a book` checks the hidden tag (records have one), so a wrong cast is an
error, not a crash. The importer records function-pointer prototypes
(today they are a plain `ptr`).

## 7b. Phrases as values

§7 gives a phrase to C. This section makes a phrase a value inside Ovis
too, and lets Ovis call function pointers C gives it (a struct field, a
`dlsym` result, SQLite's `xFunc`). Zig's model: `*const fn (i32) i32` is
a pointer to a function, stored, passed and called; no closures.

**Naming a phrase (decided by the owner, 2026-10-19).** Bare words are
always a call; a phrase as a value is written so the line shows it:

| Option | Example | Pros | Cons |
| --- | --- | --- | --- |
| A. The type decides | `atexit(say goodbye)` | No syntax | When the result also fits (a `void *`), both readings type-check and the line doesn't say which |
| **B. `_` for slots, `the phrase` when there are none** | `compare _ and _`, `the phrase say goodbye` | Never ambiguous; English | Two words for a phrase without slots |
| C. An empty marker | `say goodbye _` | Short | `_` would mean "no slot" here |

Decided: **B**. A phrase with slots is named with `_` (`the phrase` in
front is allowed); one without slots needs `the phrase`. `atexit(say
goodbye)` calls it, which is a type error that suggests `the phrase say
goodbye`.

**The type.**

| Option | Pros | Cons |
| --- | --- | --- |
| A. Only for C (§7): no phrase values in Ovis | Small | No `apply (f) to each of xs`; C's function pointers can't be called |
| **B. A phrase type, two conventions**: `phrase (whole number, whole number) giving a whole number` for Ovis phrases; `C phrase (int32) giving int32` for what headers give (C's types and calling convention) | Typed and checked; an Ovis phrase called by Ovis needs no conversion; C's pointers are callable | Two related types |
| C. One type in C's convention only | One type | Every Ovis call through a phrase value converts 64-bit numbers to C's |

Recommend **B**. Core: `(phrase (ARG…) RET)` and `(c-phrase (CTYPE…)
CTYPE)`.
- A phrase value is a function pointer: `f = compare _ and _`, a
  parameter `(f: phrase (whole number) giving text)`, a field, a list
  item. Never null unless `or nothing` (§3); a `C phrase` from a header
  may be null, as a C pointer.
- Calling: `call f with 3 and 4`, `call f` without arguments; it gives
  what the phrase gives. Calling a `C phrase` converts as for C functions.
- Giving an Ovis phrase where a `C phrase` is wanted makes, at build
  time, one C-convention wrapper per phrase and C type (§7); no closures,
  so no generated code at run time. The reverse, a `C phrase` where an
  Ovis `phrase` is wanted, needs `as`, which wraps it the other way.
- Types check by slots and result; a mismatch shows both.
- The trait dispatch tables (`ovis.dispatch.METHOD`) stay internal.



| Option | Pros | Cons |
| --- | --- | --- |
| A. The header only (today) | Small namespace | `struct tm`, `size_t` and most of SDL are elsewhere |
| B. Everything it includes | Complete | Thousands of libc names; clashes with phrases and Ovis names |
| **C. The header, the headers beside it (the library's own), and the types its functions reach** in any other header | What the library offers, plus what its functions need | A rule to explain |

**Recommend C.** Function-like macros (`deflateInit`) stay out (P2 task);
the error for one names the function it calls (`deflateInit_`), when the
macro is a single call.

## 9. A phrase that changes its caller's number (decided: no new rule)

Decided by the owner (2026-10-19): no marker for changed parameters.
Records, lists and text are shared, so a phrase changes them already; a
number is changed as in C, through a pointer (§6):

```
to increase (n: pointer to a whole number) by (k):
    the whole number at n = the whole number at n + k

increase ref score by 5
```

Most phrases give back the new value instead (`score = score plus 5`). A
statement that drops the result of a phrase which gives one back and
changes nothing it was given (no pointer, record or list argument) gets a
warning, so `increase score by 5` written against a value-returning phrase
isn't silently nothing.

## 10. Copying is explicit (decided)

Decided by the owner (2026-10-19): **copying is explicit, as in Rust.**
Nothing is ever copied behind the author's back; `a copy of x` (Rust's
`clone`) is the only way to get a second value.

- `=`, arguments, list items and fields **share** text, lists and records,
  as today; that is not a copy. Numbers and true/false are copied, as
  Rust's `Copy` types are.
- `a copy of x` is **deep**: the copy shares nothing with `x`. A value
  reachable twice inside it is copied once, so sharing within it and
  cycles survive (the same walk as memory.md's `keep`). It is a new value
  in the current memory (memory.md §2).
- A C record's copy is its struct's bytes; pointers in it still point
  where they did, since Ovis can't know what C owns.
- Moves (Rust's `b = a` leaving `a` unusable) are not taken: Ovis shares
  instead, so it needs no borrowing (memory.md rejects ownership).
- The C boundary's own copies (a struct passed by value, a `ref` through
  a temporary of another size) are the calling convention's, not values
  the author holds, so they stay.

## 11. Plan

Each step leaves the suite green; R1–R5 are Core first, then phrases.

| Step | Work | Test |
| --- | --- | --- |
| R1 | Core: C number types, the §3 pointer kinds, `load store offset cast size-of`, `ref` of fields and items; codegen; checked mode hooks when memory E6 exists | test clib: read and write through pointers, offsets, casts |
| R1b | Non-null records and lists, `or nothing`, narrowing after `is nothing` (§3); prelude, examples and eval programs moved | the suite, examples build offline twice |
| R2 | Import struct definitions (§1): layout from clang, fields, nested, `char[N]`, unions; `a new T`; field read/set; typed pointers from headers (§3) | test clib `ovt_point`, a nested struct, a union; `localtime`, `readdir` |
| R3 | C arrays (§4); lists passed when the item types match; `text of` a byte array | `fread`/`fgets` into a byte array; a list of decimals passed as `double *`; an out-of-bounds item fails |
| R4 | Structs by value through C wrappers (§2) | `ovt_point_sum`; a struct returned by value |
| R5 | Callbacks (§7), phrase values and `call` (§7b), prototypes on import as C phrases | `qsort` with an Ovis comparison; `sqlite3_exec` with a row callback; `ovt_visit` |
| R6 | Headers beside the header and reached types (§8) | `struct tm` from `time.h` through a library header |
| R7 | Phrases for §5–§7, the type words, `ovis phrases pointer`; `examples/` with zlib streaming (`z_stream`) and `readdir`; c-libraries.md, ovis-core.md, architecture-spec | examples build offline twice |
| R8 | the §9 warning for a dropped result, §10 `a copy of` | phrase tests: a copy shares nothing, a cycle survives |

## 12. Decisions

Decided (2026-10-19): §3 C and §4, Zig's pointer kinds and slices, with
null only in `or nothing` types and C pointers; §6 C, addresses as in C; §9, numbers change
through pointers, with a warning for a dropped result; §10, copying is
explicit and `a copy of x` is deep.

Also decided (2026-10-19), as recommended: §1 B, C structs imported as
C records; §2 A, structs by value copied at the boundary through a C
wrapper per function (iii); §5 B, raw pointer phrases everywhere; §7 A,
`to …:` phrases as C callbacks, no captured variables; §8 C, the header,
the library's headers beside it, and the types its functions reach.

§7b: naming a phrase decided (B: `_` for slots, `the phrase` without
slots). Recommended, not yet decided: the phrase type B (an Ovis and a C
convention, `call f with …`).
