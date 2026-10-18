# Proposal: functions, records and traits

Status: **sketch; decided with the author.** It continues
[phrase-grammar.md](phrase-grammar.md), which defines the phrase grammar, templates and
questions this builds on. Nothing here is built.

## 1. User functions and records

Agreed with the author. This extends "User phrases" in
[phrase-grammar.md](phrase-grammar.md) §3.

### Functions

- **Call-syntax name.** It is built from the phrase's fixed words:
  `to pad (text) to (width):` is also `pad_to(text, width)`. Writing
  `to pad (text) to (width) as pad:` overrides the name.
- **Types.** Each function gets one type, inferred by the elaborator as
  now. Optional annotations settle what inference can't, for example
  `(width: whole number)`.
  - When inference can't decide, the build fails with "say what type
    `width` is". The model is never asked.
  - Generic code stays in built-in templates (`T`). Per-type copies are
    added only if test programs need them.
  - The exception is a parameter typed by a trait, which accepts any type
    that implements it (§2).
- **Overlapping phrases.** They are checked where the phrase is defined.
  The new phrase's canonical readings are parsed against the other
  templates.
  - The more specific phrase, the one with more fixed words, wins.
  - A tie is an error at the definition, so lines that use the phrase stay
    unambiguous.
- **Model.** It has no new job. User phrases are candidates for
  `unknown_phrase` like built-in ones: `pad name out to 10` can be read as
  `pad name to 10`.

### Records

Records are declared as a block. A type is optional where it can be
inferred, and a default can follow it:

```
a book has:
    title (text)
    pages (whole number)
    price (decimal, 0)

b = a book with title "Dune", pages 412
price of b = 9.5
show b's title, b.pages
```

- **Creating one:** `a NAME with FIELD VALUE, ...`. A missing field without
  a default is an error, never a silent zero.
- **Fields:** `F of X`, `X's F` and `X.F` are built in, and all three can be
  set with `=`. The type of `X` decides that `of` means a field. That keeps
  it apart from phrases such as `average of xs`; the Earley parser's type
  filter ([phrase-grammar.md](phrase-grammar.md) §2) settles which is meant.
- **By reference**, as in Python and like lists. Records are heap-allocated.
  C structs passed by value (an open task) are converted at the C boundary.
- **No methods and no inheritance.** Functions that take a record cover
  that (`to discount (book) by (percent):`), so phrases stay the only way
  to define behaviour. Traits (§2) add shared behaviour without breaking
  this, because a trait's methods are phrases too.
- **Fields are never inferred from use.** A typo would create a new field,
  and the record's shape would be hidden.

**New in Ovis Core:**
- `(record NAME (FIELD TYPE)...)`
- `(new NAME (FIELD VALUE)...)`
- `(field X NAME)`
- `(set-field X NAME VALUE)`

In LLVM a record is a struct type behind a pointer. The elaborator and the
checker get a record type, unified by name.

### Field slots in templates

There is a new slot type, `field of T`. It lets a phrase written once work
for every record:

```
expr  {xs: T[]} sorted by {f: field of T}                       : T[]
expr  total {f: field of T} of {xs: T[]}                        : num
expr  the {x: T} in {xs: T[]} with the (highest|lowest) {f: field of T} : T
```

So `show total price of books` and
`cheapest = the book in books with the lowest price` need no loop and no
question.

### Questions involving records

These are the existing question kinds ([phrase-grammar.md](phrase-grammar.md) §4) with
new candidates:
- **`unknown_name` for a field:** in `cost of b`, the candidates are the
  fields of `book`, and the model picks `price`.
- **`reference` by type name:** for "the book", the candidates are the
  variables of type `book` in scope. If only one is in scope, it is used
  without asking.

## 2. Traits and impls

Agreed with the author. These are Rust-like traits, kept within the
phrase grammar: a trait's methods are phrases, with `it` as the value
itself. This is a later phase than records (§3 lists the order).

```
a shape can:                          # trait
    area of (it) gives a decimal
    describe (it)                     # no body: every shape must provide it
    show (it) twice:                  # default method, written once
        describe it
        describe it

a circle has:
    radius (decimal)

a circle is a shape:                  # impl
    area of (it): give back pi * radius of it * radius of it
    describe (it): show "a circle of radius", radius of it

to show total area of (shapes: list of shape):
    show total of (area of each shape in shapes)
```

A trait's phrases are templates whose slot accepts any type that
implements the trait. `area of c` therefore parses like any other phrase.

### Dispatch: dynamic in meaning, static where proven

- **Meaning.** A value typed by a trait is dynamic: a record pointer plus a
  vtable. Mixed lists such as `list of shape` work.
- **No `impl` versus `dyn` choice for the user.** It doesn't fit an English
  surface.
- **Optimisation.** Where every call site passes one concrete type, the
  compiler may call the method directly. This is invisible to the user and
  can be left out at first, since LLVM often removes such indirect calls
  itself.

### Built-in traits

Records get first-class access to built-in phrases by implementing these:

| Trait | Phrase to provide | Lets the record use |
| --- | --- | --- |
| `showable` | `text of (it)` | `show b`, `"…" + b` |
| `comparable` | `(a) comes before (b)` | `sort books`, `smallest of books` |
| `equatable` | `(a) is the same as (b)` | `=`, `… contains …` |

The template slot types (`num`, `any`, ...) become built-in traits, so the
template library and user traits are one mechanism. The library is written
against trait slots.

### Trait-typed parameters

A trait-typed parameter may be annotated (`(shapes: list of shape)`). When
it isn't, the elaborator infers the trait if the phrases used on the
parameter belong to **exactly one** trait. Otherwise the build fails and
asks the author for the annotation. The model is never asked.

### Rules

- **One impl per trait and type**, checked deterministically. No orphan
  rule is needed, because a program has no external crates.
- **A missing implementation is an error.** If `a circle is a shape` lacks
  `describe`, the error lists the missing phrases. A trait's default methods
  fill in what isn't written.
- **Left out at first:**
  - associated types;
  - generic traits (`Add<Rhs>`);
  - traits requiring other traits.
- **Model.** It has no new job. Trait phrases are candidates for
  `unknown_phrase` like any other phrase.

### New in Ovis Core and the compiler

- **Ovis Core:** `(trait NAME (PHRASE SIGNATURE [DEFAULT])...)` and
  `(impl TRAIT TYPE (fn ...)...)`.
- **Elaborator:** a generic type that stands for "any type that is a
  shape".
- **Codegen:** a vtable per impl. A trait value is a record pointer plus a
  vtable pointer.

## 3. Phases

1. The phrase parser, templates and questions
   ([phrase-grammar.md](phrase-grammar.md) §2–§7), with no records.
2. Records (§1).
3. Built-in traits, with the template library rewritten against trait
   slots.
4. User traits and impls.
