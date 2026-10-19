# Ovis Core

The S-expression language the phrases lower to: its types, forms, statements and expressions. Part of [architecture-spec.md](architecture-spec.md) §6, moved here to keep the spec under 500 lines.

Ovis Core is written as S-expressions; `;` starts a comment.

**Types:** `int` (i64), `float` (f64), `bool`, `str` (char*), `ptr` (void*),
records `(record NAME)` (a bare name would read as a variable in
`(let x y)`), and lists `int[] float[] bool[] str[]` (also `(list int)`,
`(list (record NAME))`). Any type can be left out or written `?`, and the
elaborator infers it.

**Top-level forms:**
- `(fn NAME (PARAM...) [RET] BODY...)`, where a PARAM is `x`, `(x)` or `(x TYPE)`
- `(extern NAME (CTYPE... [...]) CTYPE)`
- `(link "lib")`
- `(use-c "HEADER.h" ["LIBRARY"])` imports a C library (§8)
- `(record NAME (FIELD [TYPE])...)`: a record type, by name for the whole
  program (two declarations are an error). A field's type may be left out.
- `(trait NAME (METHOD (it PARAM...) [RET])... (fn METHOD (it PARAM...)
  BODY...)...)`: a trait. A `METHOD` list is a phrase every impl must
  write (a missing `RET` is nothing; other parameters need types); a `fn`
  is a default, whose `it` is a value of the trait. Methods are global
  names: a method may not share its name with another trait's, a function
  or a built-in.
- `(impl TRAIT RECORD (fn METHOD (it PARAM...) BODY...)...)`: one impl per
  trait and record (the type must be a record). Each `fn` becomes the
  function `METHOD__RECORD`; its `it` is the record, and unwritten types
  are the trait's (`pin_trait_types`). A parameter written `(b same)` in
  a trait (a method or a default's `fn`) is the same record as the first:
  an impl's is its record, and a dispatched call checks it. The checker reports missing
  methods (listed), methods the trait lacks, and types that differ.
- any statement. Top-level statements run in order in `main`, and top-level
  `let`s are globals.

**Names across modules** (`resolve_modules`, before elaboration):
- A name defined by one module keeps its name. A name defined by several is
  renamed `module.name` in each.
- A reference resolves to its own module's definition first, then to the
  only module defining the name.
- When several other modules define it, the build reports the choices. Any
  tail of the module path picks one: `tax.add_tax` or `pricing.tax.add_tax`.
- Locals are never rewritten.

**Statements:** `let set put set-field print if(then/else) while
for(inclusive, up) for-down(inclusive, down) each break continue return pass`, plus calls.
`(set-field X FIELD VALUE)` sets a field of the record X.
`(for-down NAME FIRST LAST BODY...)` counts from FIRST down to LAST.
Memory ([proposals/memory.md](proposals/memory.md)): `(with-new-memory
BODY...)` makes a memory for the block and frees it however the block is
left; `(with-memory M BODY...)` makes things in the memory M (type
`memory`, from `(new-memory)`) for the block; `(clear-memory M)` frees what
M holds and keeps it; `(free X)` frees text, a list (not its items), a
record, a C pointer or a memory; `(keep X)` copies what the innermost `with`
made into the memory outside it, once for what is shared or circular, and
gives back X itself for what is already outside. Freeing text written in
the program does nothing.
`(defer STATEMENT...)` ("at the end", Zig's `defer`): the statements run
whenever control leaves the block it is in, last deferred first, if its
line was reached. The checker rejects `return`, and `break`/`continue`
aimed at a loop outside it, inside one; a helper file's top level may not
use it. The elaborator's implicit return skips trailing defers (the value
statement moves after them). The Core reader still accepts the loose spellings a model used to write
(`let` as `set`, an `if` without `then`, operator aliases ...):
[dream/architecture-spec](dream/architecture-spec/INDEX.md).

**Expressions:**
- arithmetic: `+ - * / div %`. `/` is always real division; `div` is
  integer division. `+` with text joins the values as text.
- comparisons and logic: `= != < <= > >=` (text is ordered as by
  `strcmp`), short-circuit `and`/`or`, `not`
- conversions and maths: `int float sqrt pow floor abs min max`
- lists: `list array get len append remove-at sort reverse`
- text: `concat substring char_at chr to_str buffer` (a chain of `+` or
  `concat` is one allocation, numbers and truth values as text written
  into it), `(set-char TEXT INDEX CODE)` writes into text Ovis made (the
  prelude fills buffers with it)
- memory: `new-memory clear-memory free keep`
- input and randomness: `read_int read_float read_line random`
- calls to user functions, libc, externs and imported C functions
- records: `(new NAME (FIELD VALUE)...)` gives every field once, in any
  order (defaults are the surface's); `(field X FIELD)`. Records are by
  reference: copies, arguments and list items share one record. `=`
  compares identity unless the record implements `equatable`. A record is
  printed as written, joined to text only with `text of`, and sorted only
  with `comes before` (built-in traits, §4).
- **Traits as types:** a trait's name is a type, written like a record's:
  `(record shape)`. A value of it is any record that implements it
  (`can_assign`), also as an item (`append`, `put`, a list written out:
  its items may be of different records) but not a whole list (`list of
  circle` is not `list of shape`). It has no fields and can't be made.
  Calling a method (`(area x)`) on a record calls its impl directly (the
  checker renames the call `area__circle`, or leaves the default's name);
  on a value of the trait it is dispatched when the program runs: every
  record starts with a hidden number (its place among the records), and
  each trait method has a table of functions indexed by it
  (`ovis.dispatch.METHOD`: the impl's, else the default, else null); a
  method only one record answers is called directly after a tag check.
  A trait value is therefore the record pointer itself; the table and tag
  are the "pointer plus vtable". Printing a trait value prints the record
  it is. Where nothing says what an untyped parameter or variable is and
  only phrases of one trait are used on it, it is that trait (`hint`s in
  the elaborator); phrases of several traits are an error asking for its
  type. An impl's version of a default has the default's types.
- `(ref X)`: the address of a variable, a field or a list item, typed
  `(ptr T)` (a truth value in a list is a `u8`). Given straight to a C
  function, a variable goes through a temporary of the C type when sizes
  differ (`sqlite3_open(path, &db)`).
- **Pointers** (references.md §3, §5): `ptr` is C's `void *`; `(ptr T)`
  one value, `(many T)` several, `(cptr T)` what C gives. T is a C number
  (`i8 u8 i16 u16 i32 u32 u64 f32`, also `byte`, `int32`…; `int`/`i64`
  and `float`/`f64` are Ovis's 64-bit slots), an Ovis type's slot, or
  `(struct NAME)`. A typed pointer goes where `ptr` does, and the three
  kinds of the same T where each other does. `(load T P)` reads (a C
  number as a whole number or decimal; a struct gives the record at P),
  `(store T P V)` writes, `(offset P N [T])` moves by N of T (bytes for
  `u8`; P's own T without one), `(cast TYPE X)` changes a pointer, record
  or text into another, or to and from its address as a whole number,
  `(size-of T)`. Checked builds fail on reading or writing through null.
- **C arrays** (references.md §4, Zig's slice as a list): `(list u8)` is a
  list whose items are kept as that C number (`value_type::pointee` on a
  list), read as whole numbers or decimals; `(array u8 N)` makes N zeroed,
  `(view T P N)` is N items over memory C has (it can't grow, and freeing
  it frees only its header). A list or array goes where C takes a pointer
  as its items (`ovis_list_data`; valid until it grows) when they are what
  C keeps there (`double *` a list of decimals, `char *` an array of
  bytes, `void *` any); `(cast ptr xs)` / `(cast str xs)` give them too.
  Arrays of C numbers can't be sorted yet.
- constants: `null EOF pi stdin stdout stderr RAND_MAX CLOCKS_PER_SEC`

The canonical list of built-ins is `builtin_names()` in `libc.cpp`.

