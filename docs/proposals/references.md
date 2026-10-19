# Proposal: pointers, C structs and references

Status: **draft**: each section gives options and a recommendation; the
owner decides (§12). It covers the P2 tasks on C structs, callbacks and
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
  pointer to a `tm` (§3), and `nothing` is its null.
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

## 3. Pointer types

| Option | Pros | Cons |
| --- | --- | --- |
| A. Only `ptr` (today), with the type written at each read | Simple types | Every read restates what the header already said; no checking |
| B. Typed pointers only (`pointer to T`) | Checked | `void *` is everywhere in C (user data, `malloc`, `memcpy`) |
| **C. Both**: the header's types become typed pointers; `ptr` is `void *`; changing one into another is written (`p as a pointer to a tm`, `p as a tm`) | Checked where C is typed, open where C is not; casts are visible | One more kind of type to explain |

**Recommend C.** Core: `(ptr T)` and `ptr`. A typed pointer goes where a
`ptr` is expected without a cast, as in C; the other way needs `as`.
Pointers to pointers are `(ptr (ptr T))`. Text goes where C takes a data
pointer (today's rule), and a list where C takes a pointer to its item
type (§4).

## 4. Arrays and buffers

| Option | Pros | Cons |
| --- | --- | --- |
| A. Raw pointers and offsets only | Smallest | Every C array loop is pointer arithmetic, with no bounds |
| B. Pass Ovis lists to C | Natural: `qsort(numbers, …)` | Ovis whole numbers are 64-bit; C's `int *` wants 32-bit, so it needs a copy; C must not keep the pointer to a copy |
| **C. A C array: a pointer, a length and an item type**, made by `a new C array of 256 bytes` (zeroed, in the current memory) or seen over C's memory with `the 100 int32s at p`; `item N of xs` and `for each` checked against the length; it passes to C as its pointer. Plus B where the item types match (64-bit whole numbers, decimals as `double`, text lists as `char **`) | Bounds-checked indexing over C memory; no copies hidden; a byte buffer for `fgets`/`fread` in phrases | A third kind of sequence beside lists and text |

**Recommend C**, with lists passed directly only when the items match (a
list of whole numbers where C wants `int *` is an error that suggests a
C array). `text of xs` reads a byte array as text up to its NUL or length.
This replaces Core's `buffer` in phrases (the P0 task on `fgets`).

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

## 6. Addresses (`ref`)

| Option | Pros | Cons |
| --- | --- | --- |
| A. Variables, as C arguments only (today) | Can't dangle | No `&s.avail_out`, no `&xs[3]` |
| **B. Also fields and items, and those may be stored** (they live in records, C records, lists and C arrays, which are on the heap, so the address is valid while the value is); a variable's address stays a C argument only | Covers C's out-parameters into structs and arrays; still no dangling address of a local | Two rules (stored or not) |
| C. Any address anywhere | Like C | A local's address can outlive it; needs escape analysis or moving locals to the heap |

**Recommend B.** Checked mode catches a stored address used after its
record is freed (it points into Ovis memory).

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

## 8. Which headers are read

| Option | Pros | Cons |
| --- | --- | --- |
| A. The header only (today) | Small namespace | `struct tm`, `size_t` and most of SDL are elsewhere |
| B. Everything it includes | Complete | Thousands of libc names; clashes with phrases and Ovis names |
| **C. The header, the headers beside it (the library's own), and the types its functions reach** in any other header | What the library offers, plus what its functions need | A rule to explain |

**Recommend C.** Function-like macros (`deflateInit`) stay out (P2 task);
the error for one names the function it calls (`deflateInit_`), when the
macro is a single call.

## 9. A phrase that changes its caller's number

| Option | Pros | Cons |
| --- | --- | --- |
| A. No: give back the new value (`score = increase score by 5`) | Nothing to build | `increase score by 5` is the English people write, and it silently does nothing |
| **B. A parameter marked as changed**: `to increase (n: changed whole number) by (k):`; the argument must be something that can be set (a variable, field or item), and the type filter drops readings where it isn't | Reads as English; visible in the header; the same `(ref …)` in Core | One more word in headers |
| C. Every number by reference | No marker | Any phrase may change any argument; surprises |

**Recommend B**, after §1–§7 (not needed for C). Records and lists are
already shared, so they need no marker.

## 10. Copying a shared value

Records and lists are shared; `keep` (memory.md) is the only deep copy.

| Option | Pros | Cons |
| --- | --- | --- |
| **A. `a copy of x`**: a shallow copy, a new value | Small; visible | Shallow surprises with nested records |
| B. Deep only | No sharing left | Cost hidden in one phrase; cycles |

**Recommend A**, also for C records (a `memcpy` of the struct).

## 11. Plan

Each step leaves the suite green; R1–R5 are Core first, then phrases.

| Step | Work | Test |
| --- | --- | --- |
| R1 | Core: C number types, `(ptr T)`, `load store offset cast size-of`, `ref` of fields and items; codegen; checked mode hooks when memory E6 exists | test clib: read and write through pointers, offsets, casts |
| R2 | Import struct definitions (§1): layout from clang, fields, nested, `char[N]`, unions; `a new T`; field read/set; typed pointers from headers (§3) | test clib `ovt_point`, a nested struct, a union; `localtime`, `readdir` |
| R3 | C arrays (§4); lists passed when the item types match; `text of` a byte array | `fread`/`fgets` into a byte array; a list of decimals passed as `double *`; an out-of-bounds item fails |
| R4 | Structs by value through C wrappers (§2) | `ovt_point_sum`; a struct returned by value |
| R5 | Callbacks (§7) and prototypes on import | `qsort` with an Ovis comparison; `sqlite3_exec` with a row callback; `ovt_visit` |
| R6 | Headers beside the header and reached types (§8) | `struct tm` from `time.h` through a library header |
| R7 | Phrases for §5–§7, the type words, `ovis phrases pointer`; `examples/` with zlib streaming (`z_stream`) and `readdir`; c-libraries.md, ovis-core.md, architecture-spec | examples build offline twice |
| R8 | §9 changed parameters, §10 `a copy of` | phrase tests |

## 12. Decisions

Not decided yet. Recommended: §1 B, §2 A with wrappers (iii), §3 C, §4 C,
§5 B, §6 B, §7 A, §8 C, §9 B (after R1–R7), §10 A.
