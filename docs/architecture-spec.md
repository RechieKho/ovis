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
`Read as:`/`Compiler:` lines are one-line annotations of the line above,
indented under it.

## 4. Phrases

**Templates** (`read_templates`, `.phr`): `expr PATTERN : TYPE => CORE` or
`stmt PATTERN [: BLOCK] => CORE`, then indented `alias:` lines. Patterns:
words, `[optional]`, `(a|b c)`, `word[suffix]`, `{name: type}`, one
`{name: type, ...}`. Types: `int float num str bool any type`, `[]`,
uppercase variables, a `type` slot's name. Checked (errors at
`file:line`): one Core form, every `$name` a slot and every slot used,
`$name...` for the variadic one, `BLOCK` iff a block, an always-present
word, a word between slots, result variables bound by a slot.
- `phrases/*.phr` and the Ovis Core prelude `phrases/*.core` (functions
  multi-step phrases call) are embedded at build time.
- The library: 90 templates in `core.phr` (output, input, prompts,
  conversions), `lists.phr`, `numbers.phr`, `text.phr` (positions count
  from 1). Element-typed phrases (`contains`, `position of`, `count of`)
  have a template and a `prelude_*` function per element type: Core has
  no generics. `test/phrase_library.txt` has a program and its output for
  every template; `templates_used` in the result shows which were used.

## 5. Reading a program

**Reading** (`compile_phrase_program`, `compile_phrases` for one text;
`parser.cpp`, `questions.cpp`, `front_end.cpp`). A line's
first words decide its shape (`x =`, `if`, `otherwise`, `while`, `for
each`, `repeat … times`, `stop`, `skip`, `to …:`, `give back`, else a
statement phrase); its span is read by a chart parser (fixed
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
- `reference`: `it`/`them`, same candidates; rule: only one fits.
- `unknown_phrase`: templates whose slots take the span's arguments (in
  any order the types allow), ranked lexically against its other words
  (same word 3, a shared 4-letter start 2, trigrams 1, function words ×0.2;
  aliases count as the template's words). Also, ranked 1 lower, with one
  recent name as a left-out argument of a fixed slot (`missing_argument`),
  kept only if it ranks above every reading of the arguments as written.
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
`ovis_eval questions eval/questions.txt` measures ranking and model, and
`ovis_eval programs eval/programs` whole programs. `--orders N` asks each
question with its readings in N orders (as ranked, reversed, seeded
shuffles; "none of these" last), since the order alone moves the model's
picks; the run reports the spread. `--embeddings-only` settles every
question by `language_model::embed` instead (the experiments
`order_ties_by_embedding` and `choose_by_embedding` in
`front_end_options`, both off). `--min-first` and
`--min-shown` fail the questions run when fewer cases rank the right
reading first or show it; CI sets them to the current numbers so adding
phrases can't quietly make the ranking worse. `--with PROGRAM` reads every
case with and without a program's phrases (helper files whole, `to …:`
blocks and `use` lines of `main.ovis`), by the ranking alone, and lists
each case that gets worse: the right reading loses first place, drops out
of the 9 shown, or the line stops asking (it now reads as the new phrase).
It exits 1 if any does, 2 if the phrases don't read on their own.

## 6. Ovis Core

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
for(inclusive, up) each break continue return pass`, plus calls.
`(set-field X FIELD VALUE)` sets a field of the record X. Leniencies:
- `let` of an existing variable acts as `set`, and `set` of a new name acts
  as `let`;
- an `if` without `then`;
- operator aliases (`==`, `mod`, `length`, `//`→`div`, …);
- a bare value as a function's last line.
- `(let NAME VALUE STATEMENT...)` is the let followed by the statements;
- a top-level `fn` with no body is skipped: the model copied the declaration
  of a function another paragraph defines from its context.

**Expressions:**
- arithmetic: `+ - * / div %`. `/` is always real division; `div` is
  integer division. `+` with text joins the values as text.
- comparisons and logic: `= != < <= > >=`, short-circuit `and`/`or`, `not`
- conversions and maths: `int float sqrt pow floor abs min max`
- lists: `list array get len append sort reverse`
- text: `concat substring char_at chr to_str buffer`
- input and randomness: `read_int read_float read_line random`
- calls to user functions, libc, externs and imported C functions
- records: `(new NAME (FIELD VALUE)...)` gives every field once, in any
  order (defaults are the surface's); `(field X FIELD)`. Records are by
  reference: copies, arguments and list items share one record. `=`
  compares identity. A record can be printed but not joined to text or
  sorted (built-in traits, M3).
- `(ref NAME)`: a variable's address. It is only allowed as an argument of
  a C function, for output parameters like `sqlite3_open(path, &db)`.
- constants: `null EOF pi stdin stdout stderr RAND_MAX CLOCKS_PER_SEC`

The canonical list of built-ins is `builtin_names()` in `libc.cpp`.

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

### C libraries (`c_import.cpp`)

- `(use-c "zlib.h" "z")` reads the header with **libclang**. Only
  declarations written in that header itself are imported:
  - functions;
  - enum values;
  - `#define`s that are a plain number (`N`, `-N`, `(N)`, `(-N)`, with
    suffixes).
- **Type mapping:**

  | C type | Ovis |
  | --- | --- |
  | 8/16/32-bit and unsigned integers, enums, `long`, `size_t` | `int`. Narrower values are sign- or zero-extended as their C type says. |
  | `float`, `double` | `float` |
  | `char *` (signed or unsigned) | `str` |
  | any other pointer (handles, `T **`, callbacks) | `ptr`; only a handle or `null` can be given for a callback |
  | struct by value, `long double` | not imported; listed in `c_library::skipped` |
- **Mixing text and pointers:** text may be passed where C takes a data
  pointer, and `null` where C takes text.
- **`pointees`:** for pointers to a single value (`int *`, `double *`,
  `T **`) the importer records the value's type. `(ref x)` then passes `x`
  directly when the sizes match. Otherwise it goes through a temporary of
  the C type, which is copied back after the call. Inference also uses the
  pointee's type for `x`.
- **Flags:** from `pkg-config` (the library name, then the header's name),
  falling back to `-lLIBRARY`. Headers are searched in `-I` directories,
  pkg-config's, then system directories. `-L` adds library directories.
- Imports are cached per process. They are resolved on every evaluation.
## 9. Model backends

`language_model::choose(messages, n)` gives the probability of each option
`1`..`n` (at most 10, one digit token each: `1`–`9`, then `0`): what
questions use. Its default asks `chat(messages, grammar)` under the grammar
`[1-n]` (`[0-9]` for 10) and is certain of the answer. `embed(texts)` gives unit
vectors (empty if unsupported). There are four implementations.
- **`builtin_model`**: llama.cpp `b11200`, statically linked.
  - Every layer runs on the first GPU llama.cpp finds (Metal on macOS;
    CUDA or Vulkan if llama.cpp is built with them), else on the CPU.
    `--cpu` forces the CPU. Reproducibility comes from `Read as:` lines
    and the lockfile, not the device; on a Mac (Metal, 1.5B) the
    probabilities differ from the CPU's by at most 0.02 and every decision
    agreed.
  - Reports where it loaded (device, or CPU and threads) through the
    progress log.
  - Loads lazily, so lockfile-only builds never load it. Weights are
    memory-mapped and not repacked for the CPU's vector units
    (`use_extra_bufts = false`): repacking 3B took ~7 s per load, and a
    build asks few questions, each ~0.3 s slower without it.
  - Greedy decoding. The grammar is checked on the top token first, and the
    whole vocabulary is scanned only if that token is refused; the result
    is identical either way.
  - Prompt KV is reused only in whole batches, which start only at
    multiples of `n_batch` (512), at the end of the system message, and
    before the last line starting `1. ` (a question's options). Generated
    tokens are never reused, so a reply never depends on earlier requests.
    The second start lets questions share a short system message; the
    third lets a run-off's questions share their line, names and question,
    so its later questions decode only their options (about 40% fewer
    tokens).
  - **Prompt cache:** the state after a system message is saved in
    `~/.cache/ovis/prompts/<hash>.state` (`llama_state_seq_save_file`,
    written to `.part` and renamed) and loaded by later processes. The hash
    covers the llama.cpp version, the weights' fingerprint, the device, the
    threads, the CPU features and the tokens, so a loaded state is what
    computing it would give.
  - `choose`: one decode of the prompt, then a softmax over the logits of
    the option tokens. Nothing is generated.
  - Threads: `--threads`, or the performance cores (macOS) or all cores. Context 8192, up to 1024 tokens
    per reply (`chat`).
  - `embed`: last-token hidden states, normalised, from a second context
    (`LLAMA_POOLING_TYPE_LAST`, all texts in one decode) so the chat's
    cached prompt stays.
- **`daemon_model`** (`model_daemon.cpp`): what `ovis build`/`run` use, so
  loading is paid once per session. `ovis model serve` holds a
  `builtin_model` and answers `choose`, `chat` and `embed` on a Unix socket
  (one request per connection, length-prefixed fields, numbers as bytes)
  until `--daemon-idle` minutes unused (15) or `ovis model stop`. The socket,
  in `$XDG_RUNTIME_DIR/ovis/` or `/tmp/ovis-<uid>/` (0700 and owned, else no
  daemon), is named by a hash of `ovis`, the weights and every setting
  answers depend on. The first request finding none starts it (file lock,
  double fork, `ready` on a pipe; stderr to `<socket>.log`). Any failure
  loads the model in-process, said once; `--no-daemon`/`OVIS_NO_DAEMON=1`
  skip it. Name and fingerprint come from the file and answers are
  bit-identical to in-process (`[model]` test), so lockfiles don't depend
  on which process answered.
- **`endpoint_model`**: an OpenAI-compatible `/chat/completions` server. It
  sends `grammar` (a llama-server extension) unless `--no-grammar` is given.
  `choose` asks for one token with `top_logprobs` 20 and no grammar, and
  falls back to the default when no option is among them.
- **`missing_model`** (in `main.cpp`): stands in when no weights are found.
  It explains how to get a model only when a question actually needs one.

**Weights lookup order:**
1. `--model-path`
2. `$OVIS_MODEL_PATH`
3. weights embedded in the executable
4. a default model (`default_models()`) in `<exe dir>/`,
   `<exe dir>/../share/ovis/` or `$XDG_CACHE_HOME/ovis/models/`
   (`~/.cache/ovis/models/`): Qwen2.5-Coder-7B-Instruct Q4_K_M first when
   `has_gpu()` and no `--cpu`, else 3B first, then the other. 7B reads hard
   wording much better but takes ~2.3× as long per question, which only a
   GPU makes small. `ovis model pull [3b|7b]` downloads the same choice.

**Embedding format:** the executable, padding to a 4096-byte boundary, the
GGUF, then a 32-byte trailer:
- offset (u64, little-endian)
- size (u64, little-endian)
- `OVIS-EMBEDDED-1\0`

It is loaded with `llama_model_load_from_file_ptr` at that offset.
Re-embedding replaces the old weights.

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
| `phrases/*.phr`, `phrases/*.core` | the phrase library and its Ovis Core prelude, compiled in by `cmake/embed_phrases.cmake` |
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
| `test/` | Catch2 tests: Core and codegen (`test.cpp`, records in `record_test`), the front end (`phrase_test`, `function_test`, `question_test`, `program_test`), the library (`library_test` with `phrase_library.txt`), `choose` (`choice_test`, with a fake HTTP server) |
| `test/clib/` | a small C library the tests import (every type-mapping case) |
| `tools/make_tiny_model.py` | tiny random GGUF for testing inference plumbing |
| `eval/` | `questions.txt` and `programs/`, labelled with the readings meant |
| `examples/` | example programs; `statistics.ovis` has a model's reading, `vague.ovis` a question, `shop/` several files |
