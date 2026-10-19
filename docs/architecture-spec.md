# Ovis architecture spec

This describes how Ovis works and the rules its parts must keep. It describes
what is built, not plans; plans are in [remaining-tasks.md](remaining-tasks.md)
and progress is in [state.md](state.md). The design came from
[proposals/phrase-grammar.md](proposals/phrase-grammar.md).

## 1. Goal

A programming language whose source code is plain English, compiled through
LLVM to native code.

Principles:
- **A deterministic parser reads the program.** Lines are made of phrases
  from a library (and the program's own `to …:` phrases), with fixed
  operators and Python-like blocks.
- **The model only chooses.** Where a line reads several ways, or uses
  words no phrase has, the compiler lists the readings that type-check and
  the built-in model picks one, or none. It never writes code.
- **Nothing is hidden.** Every choice is written under its line as a
  `Read as:` line, which the author can edit; it is authoritative.
- **Reproducible.** `Read as:` lines and the lockfile settle every question
  the model answered, so rebuilds never need the model.
- **Unsure means ask.** A question the rules, the ranking and the model
  can't settle confidently stops the build, listing the readings.

## 2. Pipeline

```
program (file, or folder with main.ovis)       load_phrase_program
  ▼
lines of tokens, blocks, Read as: notes        lexer.cpp
  ▼
`to …:` headers and `use "x.h"` of every file  front_end.cpp
  ▼
each line: shape, chart parse, type filter     parser.cpp
  │ one reading → lowered to Ovis Core text
  │ none/several → question: Read as:, lockfile, rule, ranking shortcut,
  │                 model (choose), else reported          questions.cpp
  ▼
Ovis Core units (one per top-level line) + prelude
  ▼
join modules, import C headers                 modules.cpp, c_import.cpp
  ▼
elaborate, check                               elaborate.cpp, checker.cpp
  ▼
write Read as: lines back + lockfile           main.cpp
  ▼
LLVM IR → -O2 → object → cc link with runtime + libs     llvm_codegen.cpp, runtime.cpp
```

## 3. Programs and lines

### Programs and modules (`load_phrase_program`)

- **A folder with a `main.ovis` is a program.** Every `.ovis` file in the
  folder and its subfolders is a module. Subfolders nest:
  `pricing/tax.ovis` is the module `pricing.tax`, and `main.ovis` is the
  module `main`.
- Left out: hidden folders, and subfolders with their own `main.ovis`
  (those are separate programs).
- `ovis run folder/` and `ovis run folder/main.ovis` are the same. Any other
  `.ovis` file is a program on its own, with no module name.
- **Order:** helper files sorted by path, then `main.ovis`. Helpers' data is
  initialised before `main`'s statements run.
- **Only `main.ovis` does things.** A helper may contain only phrases
  (`to …:`), `use` lines and top-level variables whose values need no
  input, randomness or calls (checked by `resolve_modules`).
- Each file has its own lockfile and its own `Read as:` lines. Reports name
  the file: `pricing/tax.ovis, line 3: …`.

**Lexer** (`split_lines`): logical lines with tokens, block depth and
annotations. Python-like blocks (a deeper line only under a `:` line; no
tabs; dedents must line up); open brackets continue a line; `"…"`, `'…'`,
`“…”` texts; `'s` after a word is a symbol, `don't` one word, `2nd` a word.
A line with a `:` outside brackets and text after it (`to double (n): give back n * 2`) is split by the lexer into the header and one body line at the next depth (`split_one_line_blocks`); notes written under it belong to the body.
`Read as:`/`Compiler:` lines are one-line annotations of the line above,
indented under it.

## 4. Phrases

**Templates** (`read_templates`, `.phr`): `expr PATTERN : TYPE => CORE` or
`stmt PATTERN [: BLOCK] => CORE`, then indented `alias:` lines. Patterns:
words, `[optional]`, `(a|b c)`, `word[suffix]`, `{name: type}`, one
`{name: type, ...}`. Types: `int float num str bool any record type`, `[]`,
uppercase variables, a `type` slot's name, `[TYPE] field of R` (R a
variable or `type` slot; a result may name a field slot: that field's
type). Checked (errors at `file:line`): one Core form, every `$name` a
slot and every slot used (a `type` slot may only type others),
`$name...` for the variadic one, `BLOCK` iff a block, an always-present
word, a word between slots, result variables and fields' records bound by
a slot.
- `phrases/*.phr`, the Ovis Core prelude `phrases/*.core` (functions
  multi-step phrases call) and `phrases/*.generic` are embedded at build
  time.
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
- **Field slots** match one word that is some record's field;
  `build_phrase` checks it against R's record once the other slots bind R
  and expands `$f` to `RECORD:FIELD`. Their Core calls generic functions,
  `(prelude_NAME:$f …)`: `phrases/*.generic` defines `(fn NAME:$T:$F …)`,
  and `compile_phrase_program` copies each one used, per record and field,
  into the prelude unit as `prelude_NAME__RECORD__FIELD`.
- The library: 93 templates in `core.phr` (output, input, prompts,
  conversions), `lists.phr`, `numbers.phr`, `text.phr` (positions count
  from 1), `records.phr` (`sorted by`, `sort … by`, `total … of`, `the T
  in xs with the highest/lowest F`, `X is nothing`). `contains`,
  `position of` and `count of` use the `equatable` trait; other
  element-typed phrases have a template and a `prelude_*` function per
  element type: Core has no generics. `test/phrase_library.txt` has a program and its output for
  every template; `templates_used` in the result shows which were used.

## 5. Reading a program

**Reading** (`compile_phrase_program`, `compile_phrases` for one text;
`parser.cpp`, `questions.cpp`, `front_end.cpp`). A line's
first words decide its shape (`x =`, `if`, `otherwise`, `while`, `for
each`, `repeat … times`, `stop`, `skip`, `to …:`, `give back`, `at the end[,]
STATEMENT` / `at the end:` (or `defer`), else a statement phrase); its span is read by a chart parser (fixed
operator precedence, phrases, optional articles, plurals of words of 3+
letters) filtered by
types. A reading brackets a phrase with a slot at its edge that fills a
slot at an expression's edge (`(add tax to x) with 2 decimals`), so
different groupings never read the same. One reading: lowered. Several, or none: a question (below), whose
answer is written as `Read as: "SPAN" is READING` and applied by
whole-word text substitution on later builds, or dropped once SPAN isn't
in the line. Each top-level line is one
unit; variables first set in a block are hoisted to a `let` at the top of
the program or phrase. `to WORDS (p[: TYPE]) … [as NAME]:` headers are read
first, so a phrase works on any line, as a statement and an expression; it
is an Ovis Core `fn` named by its words (`pad_to`; commas may separate
parameters, and match `,` in lines), also called as
`pad_to(a, b)` like C functions. Assigning in a phrase makes a local.
Variables named like Ovis Core words get a `_`; `;@LINE` comments in the
Core map checker problems back to source lines. Reports quote the line
under the problem and speak the program's words (`in_authors_words`):
types in English (`describe_in_english`, done by the checker), phrases by
their header (`'greet (who)'`), `prelude_*` as "a built-in phrase", Core
operations as phrases (`len` → `length of`), renamed variables without
their `_`. A variable first set in a
block is declared `(let x TYPE)` (or `?`) at the top. A `for each`
variable ends with its loop. A variadic slot's items are split at commas,
and also with neighbouring pieces kept together (up to 4 commas), for
phrases that have commas.

**Records.** `a NAME has:` (top level; NAME is words, `order_item` in
Core) is read in every file before headers and lines, then its fields:
one per indented line, `F`, `F (TYPE)`, `F (TYPE, DEFAULT)` or `F
(DEFAULT)`. Types are written as for parameters (`whole number`, `list
of books`, a record's name). A default is read alone, with no names, and
its Core goes into each `(new …)` leaving the field out; it decides a
field's type when none is written. A field whose line can't be read on the
first pass (a default `a point with …` of a record declared further down)
is read again after the others (`read_records`). Lowered to `(record …)`.
In lines:
- `a NAME with F V, F V and F V`: `(new …)`. Items split at commas, and at
  `and`/`with` before a field; an item may start with `and`, `with`, an
  article, and put `of`/`being`/`is`/`as`/`=` before V (`, and with the age
  of 12`); `named V`/`called V` first gives a `name` field; a field is given once. A missing field without a default is the
  checker's error. Lists and variadic slots try groupings, so an item may
  have commas; past 4 commas, a comma before a field after `with` stays.
- `F of X`, `X's F`, `X.F`: `(field X F)` when X's record has F; if X's
  type isn't known (a parameter), any record with F (typed if only one).
- `TARGET = VALUE` with a field target: `(set-field …)`.
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
whose call is `(area_of x)`. Lowered to `(trait …)` and `(impl …)`. A list
of different records that are all one trait (`[a circle …, a square …]`) is
a list of that trait (`vocabulary::common_trait`, and the elaborator); with several shared traits, the first by name. A value of one trait may be asked for a phrase of another, or a list of it passed as a list of another, when some record implements both (`can_assign`, `check_method_call`); the dispatch checks the table entry when the program runs (`ovis_method_check`).

**Programs and C.** All files share one `program_context`: the vocabulary, the asker and its
budget, phrase names (a phrase is defined in one file only), and the
top-level variables of the files read so far (assigning one in a later
file makes that file's own). Units carry their module; the prelude is
module `prelude`; then `resolve_modules` (helper rule, names) and
`resolve_c_imports` run. `use [words] "x.h" [words "lib"]` lowers to
`(use-c …)` and imports the header while headers are collected, so its
functions (`name(args)`) and constants work on every line; `ref x` as a
call argument is `(ref x)`. Header hashes go in each file's lockfile, with
a warning when one changed.

**Questions** (`question_of`, `questions.cpp`). The unclear span is the
shortest one nothing reads, in a reading of the rest. Kinds and their rule:
- `ambiguous_parse`: several readings; candidates are the readings.
- `unknown_name`: one word, no name: candidates are the names in scope with
  which the line reads, most recently set first; rule: the one close
  spelling (`core::closest_name`). In a longer unclear span a word that is
  no phrase word and a close spelling of a name is fixed first.
- `reference`: `it`/`them`, same candidates; rule: only one fits. Also a
  record's name (`the book`, with its article): the variables holding one.
- A word where a field's name goes that no field is called (a hole with
  its record, `cost of b`, `b's titl`, `total cost of books`): an
  `unknown_name` whose candidates are the record's fields with which the
  line reads; rule: the one close spelling.
- `unknown_phrase`: templates whose slots take the span's arguments (in
  any order the types allow), ranked lexically against its other words
  (same word 3, a shared 4-letter start 2, trigrams 1, function words ×0.2;
  aliases count as the template's words). Also, ranked 1 lower, with one
  recent name as a left-out argument of a fixed slot (`missing_argument`),
  kept only if it ranks above every reading of the arguments as written.
  A span starting with a record's name also gets `a NAME with F V, …` (each
  value a field its type fits, written order; record words 3 each).
  Only readings with which the whole line still reads are offered (the
  span replaced by the reading). A name that is also one of the span's
  words (`how many items in items`) may be part of the verb instead. The
  9 best are kept, and any as good as the 9th (then a run-off).

Resolution: the lockfile (below); else the rule; else the ranking shortcut
(a reading covering every content word of the span, best score ≥ 3 and 3
above the next); else `choose` over at most 9 readings plus "none of
these" (`0` when there are 9; run-offs for more, a group of one goes on
unasked; a run-off's final makes "none" less likely, so it is avoided), accepted at p ≥ 0.6 and margin ≥ 0.3, within
`--model-budget`; else a report listing the options. All but the report
write a `Read as:` line. `front_end_timing` counts how each was settled.
`ovis_eval questions` and `ovis_eval programs` measure ranking, the model and
whole programs; their options (`--orders`, `--embeddings-only`, `--min-first`,
`--min-shown`, `--with`) are in
[dream/architecture-spec](dream/architecture-spec/INDEX.md).

## 6. Ovis Core

S-expressions (`;` comments) with `int float bool str ptr`, records,
traits and lists; top-level `fn extern link use-c record trait impl` and
statements (run in `main`); names across modules resolved by
`resolve_modules`; `defer`; by-reference records and dispatched trait
values. The full reference is [ovis-core.md](ovis-core.md); the canonical
list of built-ins is `builtin_names()` in `libc.cpp`.

## 7. Elaboration (`elaborate.cpp`)

Deterministic, run on the whole program before checking. Each decision is
emitted as a note, which becomes a `Compiler:` line in plain English.

1. **Type inference.** A slot per variable, parameter, return value and
   expression.
   - Union-find for equality.
   - Flow edges for "value goes into" relations, with widening
     int → float and numbers → text for `+`.
   - Pinned slots for stated types.
   - Records unify by name. Each declared field has a slot; `field` and
     `set-field` join their value to it once X's record is known. If
     nothing else says what X is, it is the only record with that field
     (`(fn shift (p) …(field p x)…)`). Field types left out are inferred
     from `new` and `set-field` values, with a note.
   - Fallback hints apply only to slots left unconstrained.
   - Unconstrained types default to `int`. A function with no valued
     `return` is `void`.
2. **Name repair.** An unknown name is read as the single closest known
   name: equal ignoring case, `_` and `-`, or within edit distance 1 (names
   of 4–6 characters) or 2 (7 or more). If two names tie, nothing is
   changed.
3. **Implicit return:** the last line's value, when the function declares no
   `return`.
4. **Number as condition** means `!= 0`.
5. **Missing trailing pointer argument** of a non-variadic C function becomes
   `null`.

The checker adds notes for printf conversions (float shown by `%d`, int
shown by `%f`).

## 8. Code generation and runtime

- Type mapping:
  - `int` → i64, `float` → double, `bool` → i1
  - `str` and `ptr` → ptr
  - a list → ptr to `{i64 length, i64 capacity, char* data}`. Lists are
    heap-allocated, have reference semantics, and are bounds-checked. Bools
    are stored as bytes inside lists.
  - a record → ptr to the struct `ovis.record.NAME` of its fields,
    allocated zeroed by `ovis_record_new`. A record variable declared
    without a value is null; reading or setting a field checks for that
    (`ovis_record_check`, a runtime error).
- `defer`: `generate_block` keeps a frame of the defers reached so far,
  copied (last first) to every exit of the block: falling off its end,
  `break`/`continue` (down to the loop body's frame depth), `return` (after
  its value is worked out), the end of `main`. While a frame is emitted
  the frames above it are cut off, so a nested defer belongs to its body.
  The surface writes `Compiler: runs at the end of every pass of the loop.`
  (or "of this branch"/"of this block") under an `at the end` that is not
  in a function or at the top level (`note_defer`; `written_source` keeps it
  byte-identical on rebuilds). Names first set at the top of an `at the
  end:` block are its own, never hoisted.
- `print` of a record writes it as the surface creates one: `a book with
  title "Dune", pages 412`; inside a list or another record it is
  bracketed, text is quoted, and null is `nothing`. Each record gets an
  `ovis.print.NAME` function, so records holding their own kind work.
- User functions have internal linkage and are named `ovis.NAME`, so they
  never clash with libc. Top-level code becomes `int main()`, which calls
  `ovis_init()` first.
- The C runtime (`runtime.cpp`) is embedded as a string, written next to the
  object file at link time, and compiled by `cc`/`clang`/`gcc`. It provides:
  - lists
  - printing (floats print `%.1f` when whole, otherwise `%.15g`)
  - text helpers
  - input, sorting and randomness
  - stdio handles and checked integer division
- Calls into C convert between Ovis and C types (`c_type` in `libc.hpp`):
  - C `int` is 32 bits: arguments are truncated, results sign-extended;
  - `float` is narrowed and widened;
  - a bool passed as a C `int` is zero-extended.
- printf-family calls with a literal format get every integer conversion
  rewritten to `ll`, and arguments converted to what each conversion shows.
- Linking flags (`libraries_of`): `-lm`, every `(link ...)`, and the flags
  of imported C libraries.

### C libraries

`(use-c "zlib.h" "z")` reads the header with libclang; types, `ref`, flags
and what is skipped are in [c-libraries.md](c-libraries.md).

## 9. Model backends

`language_model::choose(messages, n)` gives the probability of each option
`1`..`n` and `embed(texts)` unit vectors; backends (built-in llama.cpp, the
daemon `ovis build`/`run` use, an endpoint) and weights lookup are in
[model-backends.md](model-backends.md). Answers are bit-identical across
the daemon and in-process, so lockfiles don't depend on who answered.

## 10. Lockfile

`<file>.lock`, `ovis::phrase::lockfile`, version 3 (the old pipeline's
version 2 is refused): `model`, `model_fingerprint`,
`c_headers`, and `readings` of `{key, span, reading}` for questions the
model settled. The key is an FNV-1a hash of the line, the span, the names
the model is told about and the candidate readings, so any change asks
again; an entry is used only if its reading is still a candidate. Entries
are kept while the question was asked or its `Read as:` line is still in
the source, sorted by key, so rebuilding changes neither file. `ovis run` saves it when it changed, after a failed build only if the
model answered something.

## 11. Invariants

1. Building a program with its `Read as:` lines and lockfile changes
   neither file and needs no model (idempotent). The tests check this on
   every example.
2. The same weights, request and machine give the same `choose`
   probabilities.
3. Every reading offered is one the line reads with and that type-checks
   in the parser's filter; the elaborator and checker run once on the
   whole program after.
4. A `Read as:` line wins over everything; one whose span is no longer in
   its line is dropped.
5. The source file is replaced atomically (write to a temporary file, then
   rename), and only when its text changes. `--keep-source` disables
   writing it.
6. Reports cite line numbers of the file as it is on disk after the build.

## 12. Code map

| Path | Role |
| --- | --- |
| `src/main.cpp` | CLI: run/build/ir on a file or folder, `-I`/`-L`, `model path|pull|embed|serve|stop`, the daemon or in-process model, writing sources and lockfiles back |
| `src/ovis/phrase/` | the front end: `lexer`, `templates`, `parser` (chart parser, line shapes), `questions` (candidates, `choose`), `front_end` (programs, reading, lowering), `lockfile` |
| `phrases/*.phr`, `*.core`, `*.generic` | the phrase library, its Ovis Core prelude and generic functions, compiled in by `cmake/embed_phrases.cmake` |
| `src/ovis/language_model.cpp` | `choose` and its softmax, the HTTP endpoint model |
| `src/ovis/builtin_model.cpp` | llama.cpp inference, model lookup, weights embedded in `ovis`, `embed` |
| `src/ovis/model_daemon.cpp` | `ovis model serve`, `daemon_model`, the socket's place and name |
| `src/ovis/sexpr.cpp`, `ast.cpp` | Ovis Core reader and lenient parser |
| `src/ovis/elaborate.cpp` | type inference and deterministic fill-ins |
| `src/ovis/checker.cpp` | name/type/format checking, messages with types in English |
| `src/ovis/libc.cpp` | libc binding table, C constants, built-in names, printf format parsing |
| `src/ovis/c_import.cpp` | libclang header import, pkg-config flags |
| `src/ovis/modules.cpp` | joining modules: naming, resolution, the helper-file rule |
| `src/ovis/llvm_codegen.cpp` | IR generation, optimisation, object emission, linking |
| `src/ovis/runtime.cpp` | the embedded C runtime |
| `tools/eval.cpp` | `ovis_eval questions` and `programs`: the model and ranking measured |
| `test/` | Catch2 tests: Core and codegen (`test.cpp`, records in `record_test`, traits in `trait_test`), the front end (`phrase_test`, `function_test`, `question_test`, `program_test`, `record_phrase_test`, `trait_phrase_test`, `defer_test`), the library (`library_test` with `phrase_library.txt`), `choose` (`choice_test`, with a fake HTTP server) |
| `test/clib/` | a small C library the tests import (every type-mapping case) |
| `tools/make_tiny_model.py` | tiny random GGUF for testing inference plumbing |
| `eval/` | `questions.txt` and `programs/`, labelled with the readings meant |
| `examples/` | example programs; `statistics.ovis` has a model's reading, `vague.ovis` a question, `shop/` several files |
