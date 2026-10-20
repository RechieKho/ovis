# Proposal: generics (records and phrases over any type)

Status: **accepted** (decisions at the end), not yet built; the plan is
§10. It builds on [types.md](types.md) (records, traits; "Core has no
generics", left out of M2–M4), [references.md](references.md) (typed
pointers) and [c-libraries.md](../c-libraries.md) (C macros, §9).

## Why

A program can write a linked list or a growing array today, but only for
one item type: a `node` whose `value` is a whole number is a different
record from one whose `value` is text. What Ovis does for every type
lives in the compiler instead:

- `list of T` and `map of K to V` are Ovis Core forms backed by the C
  runtime, not code a program could have written;
- an element-typed phrase is written once per type: `prelude_contains_int`,
  `_float`, `_str`, `_bool` in `prelude.core`, plus a `for records:` line
  and a copy in `traits.generic` for records;
- three copy mechanisms each cover one case: `.generic` files (a record
  and a field, text replaced by a regex in `instantiate_generics`),
  `copy_for_sized_lists` (lists of a size, in the checker), and the
  `for records:` lines of templates.

Generics let a program write a stack, a queue, a sorted set or a hash map
once and use it for numbers, text and records, and let the library do the
same with one mechanism. They are the first step towards lists and maps
written in Ovis (§8).

## 1. Principles

- **Copies, not boxes.** A generic record or phrase is copied for each
  type it is used with (monomorphised, as in Rust, C++ and Zig). The
  generated code is what the author would have written by hand; nothing
  is boxed, and codegen and the runtime don't change.
- **Checked once, where it is written.** A generic phrase is checked
  against what its type variables promise (their traits), not once per
  copy, so an error points at the definition or at the call, never inside
  a copy (unlike C++ templates).
- **Stated, not guessed.** A type variable is written where it is
  introduced. An untyped parameter keeps one type, as now (§3).
- **The model has no new job.** Generic phrases are candidates for
  `unknown_phrase` like any other.

## 2. Generic records

```
a stack of (item) has:
    items (list of item)
    size (whole number, 0)

a table from (key) to (value) has:
    keys (list of key)
    values (list of value)
```

- The header is `a NAME has:` with words in parentheses for its type
  variables, as a `to …:` header has them for parameters. Any fixed words
  may join them: `a stack of (item)`, `a table from (key) to (value)`, `a
  pair of (first) and (second)`.
- A type variable is a type inside the record: a field `(item)`, `(list
  of items)`, `(table from item to whole number)`, `(item or nothing)`.
  Plurals are read as for records (`list of items`).
- A type of it is the header's words with types in the parentheses:
  `stack of whole numbers`, `stack of books`, `table from text to whole
  numbers`, `stack of stacks of text`. These are type words everywhere a
  type is written (parameters, fields, `empty list of …`).
- Making one names the types unless a field given decides them:
  `a stack of whole numbers with items [1, 2]` or `a stack with items [1,
  2]` (`item` is a whole number); `a new stack` alone is an error, "say
  what a stack holds: `a new stack of …`", as `empty list of` asks today.
  An empty list given for a field doesn't decide it.
- A type variable's name may not be a record's or a trait's (an error at
  the header), and a generic record may not be called `list`, `map`,
  `dictionary` or `array`.
- Recursive records work as now: `a node of (item) has: value (item),
  next (node of item or nothing)`.

## 3. Generic phrases

```
to push (x: any item) onto (s: stack of item):
    add x to items of s
    increase size of s by 1

to pop from (s: stack of any item):
    x = last item of items of s
    remove item (size of s) of items of s
    decrease size of s by 1
    give back x

to the largest of (xs: list of any comparable thing):
    best = first item of xs
    for each x in xs:
        if best comes before x:
            best = x
    give back best
```

- **`any NAME`** introduces a type variable at its first mention in the
  header; later mentions, in the header and the body, are the bare word.
  The order of the parameters doesn't matter: `(s: stack of any item)`
  introduces `item` as well as `(x: any item)` does.
- **Bounds** are traits between `any` and the name: `any comparable
  thing`, `any equatable and showable key`, `any shape`. With none, the
  body may only move the value: store it, give it back, put it in a list
  or record, compare it with `is nothing` when it is `item or nothing`.
  Each use needs the trait that allows it, checked in the body once:
  `=` and `contains` need `equatable`, `<` and `sort` need `comparable`,
  `show` and `+` with text need `showable`, arithmetic needs `number` (the
  templates' `num`), and a user trait's phrases need that trait.
- **A call** works out the type variables from its arguments (the
  elaborator's slots, §6); a variable the arguments can't decide is an
  error at the call, "say what `item` is", answered by typing a variable
  or writing the type in the value (`an empty stack of whole numbers`).
- **The result** is inferred as now; it may be a type variable or contain
  one (`list of item`).
- **Untyped parameters stay one type.** `to swap (a) and (b):` used with
  whole numbers and then text is an error, as now, but its message names
  the fix: "`a` is a whole number on line 4 and text on line 9; write
  `(a: any thing)` for both". Making untyped parameters generic silently
  (Hindley–Milner generalisation) is a decision (G2).
- **Traits on generic records**: `a stack of (item) is showable:` and
  `a stack of (item) is a container:` implement a trait for every stack,
  and may require the item's traits: `a stack of (comparable item) is
  comparable:`. Each copy of the record gets its own impl.

## 4. Numbers and text in bounds

Bounds are only useful if numbers and text meet them. Today a number or
text can't implement a user trait, because a trait *value* is a record
pointer with a tag (types.md, state.md 2026-10-19). Generics only need
the trait *statically*: a copy for whole numbers calls the whole-number
impl directly, with no tag.

- `a whole number is hashable:` (also `text`, `decimal`, `int32`…)
  becomes allowed, with the same body rules as a record's impl.
- Such a type meets bounds (`any hashable key`) and may be called directly
  (`hash of 5`), but is still not a value of the trait: `list of
  hashable` holding a number stays an error, until boxing (memory E2–E3).
- The built-in traits already hold for numbers and text, so `any
  comparable thing` takes them with no impl.

## 5. Typed memory for generic code

A growing array written in Ovis needs memory for `n` items of a type
variable. references.md has the pieces for a known type: `pointer to
many int32`, `the int32 at p`, `p moved by 3 int32s`, `size of int32`.
Generics let T be a type variable there, and add one allocation phrase:

- `room for (n) items` / Core `(alloc T N)`: zeroed memory for N items of
  T in the current memory, a `pointer to many T`; `(alloc-more P T OLD
  NEW)` grows it (copies, in the same memory). `free` frees it.
- `the item at p`, `set the item at p to v`, `p moved by i items` with
  `item` a type variable: Core `load`, `store`, `offset` with a type
  variable, made concrete in each copy.
- Checked builds check these against the allocation's bounds and freed
  state as for C pointers (references.md §5); `keep` and `a copy of`
  follow a `pointer to many T` only when it is a field of a record with a
  `number of items` it can trust, so the first version refuses `keep` of
  a record holding one (an error naming the field), as for C pointers.

## 6. Ovis Core and the compiler

**Core forms.**
- `(record NAME (TYPEVAR...) (FIELD TYPE)...)`: a generic record; its
  type is `(record NAME T...)`, `(record stack int)`.
- `(fn NAME (forall (T BOUND...)...) (PARAM...) [RET] BODY...)`: a
  generic function. A type variable is written `(var T)` in types:
  `(x (var item))`, `(list (var item))`, `(record stack (var item))`.
- `(impl TRAIT (record stack (var item)) (forall …) (fn …)...)`, and
  `(impl TRAIT int …)` for numbers and text (§4).
- `(alloc T N)`, `(alloc-more P T OLD NEW)` (§5).

**Elaboration** (`elaborate.cpp`). The inference's slot kinds gain
`variable` (a named, rigid type: it unifies only with itself). A generic
function's body is elaborated once with its variables rigid. Each call
gets fresh slots for the callee's variables (instantiation), unified with
the arguments; a slot left undecided is the "say what `item` is" error.

**Checking** (`checker.cpp`). A generic body is checked once: a variable
allows exactly what its bounds allow (§3). A call is checked against the
bounds with the types found: `the largest of` a list of records lacking
`comes before` is an error at the call naming the missing phrase
(`missing_phrase_hint`).

**Monomorphising** (a new pass after checking, `monomorphise.cpp`). From
`main` and every non-generic function, each call of a generic function
with concrete types is a copy `NAME__T…` (`push__int`,
`push__record_book`, `largest__str`), made once, its body's types
substituted, and its own calls followed (a worklist). Each concrete use
of a generic record is a record `stack__int` with its own tag, so trait
tables, `keep` and printing see ordinary records. Codegen and the runtime
see no generics; checked builds' tables and `--report-leaks` name the
copy's source line, which is the generic's.

**Phrases** (`front_end.cpp`, `parser.cpp`). `any`, bounds and record
headers with parentheses in the declaration readers; `stack of X` read
wherever a record's name is read as a type; `value_kind`'s
record base carries the type arguments so the type filter keeps `push 3
onto names` (a stack of text) from reading.

**Names across modules** are unchanged: copies are named after the
resolved name (`tax.push__int`).

## 7. What it replaces in the library

Each step is a refactor with no change in behaviour, checked by
`test/phrase_library.txt`:

- per-type prelude functions (`prelude_contains_int/float/str/bool`,
  `prelude_sum_*`, `prelude_position_*`, `prelude_count_*`…) become one
  generic function each;
- `phrases/traits.generic` and `fields.generic` become generic functions
  in `prelude.core` (a field slot stays a template feature: its copy is
  per record and field, `$F`, which a type variable doesn't cover);
- templates' `for records:` lines go, since one generic call covers
  numbers, text and records;
- `copy_for_sized_lists` becomes a monomorphised copy (`list of any
  number`);
- the C macros' variants become copies too (§9).

`instantiate_generics` stays for field slots only, or is folded into the
new pass with the field as a second kind of argument (G6).

## 8. Towards lists and maps written in Ovis

With §2–§5 a program can write its own `growing list of (item)` and
`hash table from (any hashable key) to (value)`. Making the built-in
`list of` and `map of` such code (the runtime's `ovis_list_*` and map
table moved to an Ovis prelude) also needs:

- **Iteration**: `for each x in s` over a program's type. Proposed: a
  record with `number of items in (it)` and `item (i) of (it)` is
  iterable (a built-in trait like `comparable`, by its phrases' words),
  and `for each` lowers to a counted loop over them. A generic trait
  `iterable of (item)` (associated types) stays out (types.md §2).
- **Literals**: `[1, 2, 3]` and `{"a": 1}` stay built-in syntax that
  makes the built-in types; a library type has `a stack of …` and `with`.
- **Bounds checks, `keep`, `a copy of`, showing as `[a, b]`**: written
  in the type's Ovis code (`text of`) or derived per record, as now.
- **Speed**: the C runtime's list is inlined by codegen (`get`, `len`).
  An Ovis list must match it on the `churn` and `join_loop` benchmarks
  (state.md) before replacing it.

So this proposal stops at generics and §5; replacing the built-in list
and map is a later decision (G7), measured, not assumed.

## 9. C function-like macros

A macro of one C expression (`MAX(a, b) ((a) > (b) ? (a) : (b))`) is
already generic, read as Zig's translate-c reads it: each parameter is
tried up front with `long long`, `double` and `char *` (3^n tries, up to
4 parameters), and each try clang accepts is a variant (`MAX__if`) the
elaborator picks per call (c-libraries.md). That leaves out what C
programs use macros for most, and computes on the wrong types:

- `ROTL32(x, n)` on a `uint32` is worked out on a `long long`, so its
  shifts and wrapping differ from C's;
- pointers and C records can't be arguments: `TAILQ_FIRST(head)`,
  `container_of(p, struct item, link)`, `ARRAY_SIZE(xs)`;
- macros of 5 or more parameters, `...`, types as arguments
  (`offsetof`, `va_arg`), and statements (`do { … } while (0)`) are left
  out;
- `_Generic` macros (`tgmath.h`'s `sqrt`) pick the `double` version for
  a `float32`.

With generics a macro is a **generic C phrase**: each parameter is a type
variable, and its copies are made per call from the arguments' real
types, as for an Ovis generic phrase (§6), instead of being guessed up
front.

- **Copies on demand.** The elaborator works out a call's argument types
  (the solver `wait` it has now), then asks clang for the result type of
  that exact combination, `__typeof__(ROTL32((uint32_t)a0, (int)a1))`:
  sized numbers, text, typed pointers, C records (`struct item *`),
  `pointer to many T`. The calls waiting in one round are asked in one
  file; a result decides more types, so it repeats until no call waits
  (one or two rounds in practice). A combination clang rejects is a
  build error at the call, with clang's message. Copies are named by
  their types (`ROTL32__u32_i32`) and get a wrapper only when used, as
  now. Results are cached per header hash, which the lockfile already
  records, so a locked build asks nothing new.
- **Exact semantics.** The wrapper takes and gives back the C types the
  call has, so `ROTL32` wraps at 32 bits and `sqrt` of a `float32`
  through `tgmath.h` is `sqrtf`.
- **Any number of parameters, and `...`**: a copy per number and types of
  arguments given, as `printf`'s calls are now.
- **Types and fields as arguments.** A parameter clang reads as a type
  (tried when an expression doesn't parse) is a type parameter, given a
  type word: `offsetof(a point, y)`, `va_arg(args, int32)`; a parameter
  used after `.`/`->` is a field, given the field's words, as a field
  slot (G6). `container_of(p, an item, link)` then gives `an item`.
- **Statement macros** (`do { … } while (0)`, or several statements) are
  a wrapper returning nothing. A parameter the macro assigns to (clang
  rejects the try with it `const`) is passed by address and written
  `ref x` in the call (references.md §9: no hidden reference):
  `SWAP(ref a, ref b)`.
- **In generic Ovis phrases** a macro called on a type variable is
  checked per copy, the one exception to "checked once" (§1): C states no
  bounds. Its error names the generic's line and the type it was copied
  for.
- **Left out:** `#` and `##` (they make names and text from tokens, not
  values), and macros that expand to a declaration or an unbalanced
  brace (`BEGIN_DECLS`).

## 10. Plan

Each step leaves the suite green.

| Step | Work | Test |
| --- | --- | --- |
| G1 | Core: `(var T)`, `forall`, rigid slots, instantiation at calls, checking against bounds, the monomorphise pass | `generic_test`: `identity`, `swap`, `largest` over numbers, text, records; an unmet bound at a call; "say what `item` is" |
| G2 | Core: generic records, `(record NAME T…)`, a record per use with its tag; traits on generic records | a `pair`, a `node of item` list, `stack of stacks`; trait dispatch over a list of `stack of …` |
| G3 | Phrases: `any NAME`, bounds in headers, `a NAME of (T) has:`, type words `stack of books`, values naming types | `examples/stack.ovis`; the type filter; questions with generic candidates (scripted model) |
| G4 | Numbers and text implement traits statically (§4) | `a whole number is hashable:`; `any hashable key` with numbers and text; `list of hashable` holding a number still an error |
| G5 | Typed memory: `(alloc …)`, `load`/`store`/`offset` of a type variable, phrases (§5) | a growing array in Ovis, out of bounds and use after free caught in checked builds |
| G6 | The library on generics (§7) | `phrase_library.txt` unchanged; fewer prelude functions |
| G7 | `examples/collections/`: a growing list and a hash table in Ovis, measured against the built-ins | `churn`-like benchmark in state.md |
| G8 | C macros as generic C phrases (§9): copies on demand by the arguments' C types, any arity and `...`, type and field parameters, statement macros with `ref` | `c_import_test`: `ROTL32` on a `uint32`, `TAILQ_FIRST`, `container_of`, `offsetof`, `SWAP(ref a, ref b)`, `tgmath.h` `sqrt` of a `float32`; a locked build asks clang nothing new |

## 11. Considered and rejected

- **Boxed generics** (every value behind a pointer, one copy of the
  code, as in Java): needs boxing numbers (memory E2–E3) and costs an
  allocation per item; copies match "nothing hidden" and C's speed.
- **Checking each copy** (C++ templates): errors inside the generic's
  body for the caller's types, hard to read in English.
- **Single capital letters** (`T`) as in the `.phr` templates: the
  templates are the library's notation; a program reads in words.
- **Implicit type variables** (an unknown word used as a type is one):
  a misspelt record name would silently become generic.
- **More kinds tried up front for macros** (every C number type and
  pointer): the tries grow as kinds^parameters, and still miss records
  and types; copies on demand ask only what calls use.

## Decisions

Decided with the owner on 2026-10-20: G1–G7 as recommended, G8 at the
owner's request.

| # | Question | Decision |
| --- | --- | --- |
| G1 | How a type variable is introduced in a phrase | `any NAME` at its first mention, bounds between them: `any comparable thing` (§3) |
| G2 | Untyped parameters used with different types | Stay one type, with an error naming `any` (§3); no HM generalisation |
| G3 | Copies or boxes | Copies, checked once against bounds (§1, §11) |
| G4 | Numbers and text implementing traits | Statically now (§4); as trait values after boxing |
| G5 | Typed memory for type variables | Yes, in G5 (§5) |
| G6 | `.generic` files for field slots | Folded into the monomorphise pass, the field a second kind of argument |
| G7 | Replacing the built-in `list of` / `map of` with Ovis code | Not in this proposal; decided after G7's measurements (§8) |
| G8 | C function-like macros | Generic C phrases, copied on demand by the arguments' C types (§9), replacing the variants tried up front |
