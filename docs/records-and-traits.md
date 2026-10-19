# Records and traits

How records, built-in traits and user traits are read and lowered. Part
of [architecture-spec.md](architecture-spec.md) (§4 phrases, §5 reading);
the design is [proposals/types.md](proposals/types.md) and the Ovis Core
forms are in [ovis-core.md](ovis-core.md).

## Built-in traits in templates

- **Traits** (M3): slot types `showable`, `comparable`, `equatable` and
  `T:comparable` (a variable bound to a trait); `num` and `any` are traits
  too (`vocabulary::implements`). Numbers and text implement `comparable`
  and `equatable` (and `bool`, `equatable`); everything but a C pointer is
  `showable`. A record implements one by a phrase whose words are `comes
  before` (`comparable`), `is the same as` (`equatable`) or `text of`
  (`showable`), with stated parameters of that record: `to (a: book)
  comes before (b: book):`. It is then no phrase of its own but a function
  `comes_before__book` (one per record), found by `vocabulary::impls`.
  Dispatch is static. `<` `>` `<=` `>=` `=` `!=` and `+` with text lower to
  those functions (`operation_core`). A template with trait slots holding a
  record uses its `for records:` Core instead (all trait slots must hold
  one record; `=> records` makes a template for records only): `$slot`
  after a `_` or `:` is the record's name, or `int`, `float`, `str` for
  numbers and text, so `prelude_contains_$xs` is `prelude_contains_int`
  and `prelude_sort_comparable:$xs` a copy of `phrases/traits.generic`
  for the record. `showable` slots use `text of` on the record's value
  before expanding; a list of such records becomes `[a, b]` through the
  generic `prelude_list_text` (`shown_by_author`), also for `+` and `as
  text`.
- **`a book is comparable:`** (also `equatable`, `showable`) holds the
  trait's one phrase written as `(it) comes before (other):`; every
  parameter without a type is the record, so it is the same as `to (a:
  book) comes before (b: book):` (`declare_builtin_impl`).
- **`nothing`** is Ovis Core's `null`, typed `ptr`: a record's place
  (field, variable, `=`) accepts it (`assignable`, the checker's `is_null`).
  `X is nothing` / `X is not nothing` take a `record` slot.
- A line no phrase reads because a record lacks `comes before` / `is the
  same as` is reported with what to write (`missing_phrase_hint`: the line
  is read again with every record assumed comparable and equatable).

## Records and user traits in lines

**Records.** `a NAME has:` (top level; NAME is words, `order_item` in
Core) is read in every file before headers and lines, then its fields:
one per indented line, `F`, `F (TYPE)`, `F (TYPE, DEFAULT)` or `F
(DEFAULT)`. F is one or more words (`number of wheels`): in Ovis Core
they are joined by `_` (`number_of_wheels`, also accepted after `.`), and
a record prints them as written (`_` as a space). Lines match the longest
field name at a place (`record_info::field_at`, `vocabulary::field_lengths`). Types are written as for parameters (`whole number`, `list
of books`, a record's name). A default is read alone, with no names, and
its Core goes into each `(new …)` leaving the field out; it decides a
field's type when none is written. A field whose line can't be read on the
first pass (a default `a point with …` of a record declared further down)
is read again after the others (`read_records`). Lowered to `(record …)`.
In lines:
- `a NAME with F V, F V and F V`: `(new …)`. Items split at commas, and at
  `and`/`with` before a field; an item may start with `and`, `with`, an
  article, and put `of`/`being`/`is`/`are`/`as`/`=`/`equals`/`set to`/
  `equal to` (after `which`/`who`/`that`) before V (`, and with the age of 12`); a list field takes items
  joined by `and`, or one item (`people which are betty and winnie`);
  `named V`/`called V` first gives a `name` field; a field is given once. A missing field without a default is the
  checker's error. Lists and variadic slots try groupings, so an item may
  have commas; past 4 commas, a comma before a field after `with` stays.
- `F of X`, `X's F`, `X.F`: `(field X F)` when X's record has F; if X's
  type isn't known (a parameter), any record with F (typed if only one).
- `TARGET = VALUE` with a field target: `(set-field …)`.
- In words (`updates`): `set|change T to V`, `increase|decrease|multiply|divide
  T by V`, `add V to T`, `subtract V from T`, T a field or a variable already
  set (of a known type, so `add x to xs` stays the list phrase's); the
  verb may follow the target (`count decrease by 1`, `count increased by
  1`: read as the line with the verb moved first). `set x to
  V`, `change x to V`, `let x be V` with one word are assignments (`shape_of`).
- Parameters may be typed by a record (`(b: book)`), which the type
  filter sees as `any`. The parser's `value_kind` has a `record` base.

**User traits** (M4). `a NAME can:` (top level; NAME is words) declares a
trait: one indented line per phrase, written like a `to` header with the
value itself as a parameter called `it` (anywhere in the phrase; it is the
first parameter in Ovis Core) and no type: `area of (it) gives a decimal`
(`gives` is the result; other parameters need a stated type). A line
ending in `:` with a body is a default phrase; the others every impl must
write. `a CIRCLE is a SHAPE:` has one indented `HEADER:` + body per phrase
(no `gives`; the trait's types are used). Like records, traits are types
(`(s: shape)`, `list of shape`), known in every file before anything else;
their phrases are known before the impls and lines of every file
(`declare_traits`, then `declare`), so an impl may come before its trait. A
phrase is a template like any `to …:` phrase (result type from `gives`)
whose call is `(area_of x)`. Lowered to `(trait …)` and `(impl …)`.
A phrase may leave out `(it)`: its parameters without a type are then the
value (`(a) comes before (b) gives a truth value`), the first picks the
impl and the others are `(b same)` in Ovis Core: an impl takes them as its
record, and a dispatched call checks they are the same record when the
program runs (`ovis_same_record_check`). A trait phrase whose words are
`comes before`, `is the same as` or `text of` is that built-in trait's:
its records get it (`comes_before__circle`), and the trait a function
that dispatches (`comes_before__shape`), so `<`, `sort` and `show` work
on its values. A line `is comparable and a thing` in a trait requires
those traits of its records (a built-in one is its phrase, which a record
may also write for itself, `a circle is comparable:`; another is Core
`(requires thing)`, checked for every impl). Numbers and text can't implement a trait: the error
suggests a record holding the value. A list
of different records that are all one trait (`[a circle …, a square …]`) is
a list of that trait (`vocabulary::common_trait`, and the elaborator,
also when an item is a variable: two records meeting in one slot become a
trait both implement); with several shared traits, the first by name. A value of one trait may be asked for a phrase of another, or a list of it passed as a list of another, when some record implements both (`can_assign`, `check_method_call`); the dispatch checks the table entry when the program runs (`ovis_method_check`), and the checker when it is built where it knows the list's records (`known_records`: a list written out or a variable given one, forgotten once a call may change it). A template's type variable bound to a record takes a trait the record implements (`append c to shapes`).

