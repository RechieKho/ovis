# Ovis architecture spec

This describes how Ovis works and the rules its parts must keep. It describes
what is built, not plans; plans are in [remaining-tasks.md](remaining-tasks.md)
and progress is in [state.md](state.md).

## 1. Goal

A programming language whose source code is plain English. A small language
model inside the compiler turns the English into a tiny strict language. A
deterministic compiler takes it from there to native code through LLVM.

Principles:
- **The model never writes machine-level code.** It writes concrete English
  steps and then Ovis Core (about 40 simple forms). Everything after that is
  ordinary, deterministic compiler code.
- **Nothing is hidden.** Every decision, whether the model's or the
  compiler's, is written into the source file next to the sentence it is
  about.
- **Reproducible.** Every model output is cached in the lockfile, and builds
  from a lockfile never need the model.
- **Fail with an explanation.** Unclear English stops the build with a
  reason and a suggested rewrite. Unclear English is never guessed at.

## 2. Pipeline

```
source.ovis
  │ split on blank lines → paragraphs                    english.cpp
  ▼
reconcile each paragraph against the lockfile            compiler.cpp
  │ (keep / remove leftover steps / detail / revise / review)
  ▼
detail pass (LLM, detail or review grammar)              compiler.cpp, prompt.cpp
  │ ok → steps + assumptions │ note │ too_vague / irrelevant / mismatch → STOP
  ▼
translation (LLM, Ovis Core grammar) keyed by steps      compiler.cpp, prompt.cpp
  ▼
parse (sexpr → AST)                                      sexpr.cpp, ast.cpp
  ▼
join modules into one namespace                          modules.cpp
  ▼
import C headers of (use-c ...)                          c_import.cpp
  ▼
elaborate (deterministic fill-ins)                       elaborate.cpp
  ▼
check (names, types, printf formats)                     checker.cpp
  │ problems → repair (LLM, restricted grammar) → loop, up to --attempts
  │ still failing → explanation (LLM) → STOP
  ▼
write detailed source back + lockfile                    main.cpp
  ▼
LLVM IR → -O2 → object → cc link with runtime + libs     llvm_codegen.cpp, runtime.cpp
```

## 3. Source format

### Programs and modules (`load_program`)

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
- **Only `main.ovis` does things.** A helper may contain only:
  - functions, `extern`, `link`, `use-c`;
  - top-level `let`s whose values need no input, randomness or function
    calls.

  Anything else is an error, and the detail pass is told the paragraph is in
  a helper file.
- Each file has its own lockfile and its own detail written into it.
  Reports name the file: `pricing/tax.ovis, paragraph at line 3`.

### Paragraphs

- A program file is UTF-8 text. **Blank lines separate paragraphs**; that is the
  only structure.
- A paragraph has the author's own lines (the *intent*), optionally followed
  by an indented **detail block** that Ovis writes:

```
Work out the average of the scores and show it.        ← intent (column 0)
    1. Add up every score into total, starting at 0.   ← step (indent 4, "N." or "N)")
    2. Print "Average: " followed by total divided by
       the number of scores, with two decimals.        ← wrapped line (indent > 5)
    Assumption: the scores are whole numbers.          ← model's assumption
    Compiler: `total` holds a decimal number, because  ← elaborator/checker note
        it is given one later on.
```

Parsing (`parse_detailed`):
- The intent is every line up to the first **detail item**: a line indented
  by 1–5 that starts with `N.`/`N)` and a space, `Assumption:` or
  `Compiler:`. Everything after it is detail. Other indented lines are the
  author's, such as the body of Python-like code.
- Paragraphs are split at blank lines, except that a blank line followed by
  an indented non-item line continues a paragraph whose last line is
  indented or ends with `:` (a Python-like block), unless the paragraph
  already has a detail block.
- In the detail block:
  - an indented `N.` / `N)` line starts a step;
  - `Assumption:` and `Compiler:` start notes;
  - lines indented by more than 5 continue the previous item;
  - unnumbered lines with nothing to continue are steps.

Rendering (`render_detailed`): items are wrapped at 80 columns. Steps are
renumbered from 1. Rendering is fully deterministic, so rendering what was
parsed gives the same text again.

## 4. Detail pass

One request per paragraph that needs it. The reply format is enforced by a
GBNF grammar (`detail_grammar`) and parsed by `parse_detail`:

| Status | Meaning | Effect |
| --- | --- | --- |
| `ok` | concrete steps (+ assumptions) | written under the paragraph |
| `note` | only describes the program | left as is, compiles to `(pass)` |
| `too_vague` | asks for something unclear | build stops: REASON + SUGGESTION |
| `irrelevant` | not about the program | build stops: REASON + SUGGESTION |
| `mismatch` | (review only) edited steps don't do what the sentence says | build stops: REASON + SUGGESTION |

The model is told what the other paragraphs' steps are, so names stay
consistent.

Python-like pseudo-code is part of the language: the detail and
translation prompts say it means what it would in Python (with a mapping to
Ovis Core for translation), that it need not be valid, and that where the
paragraph's code and the steps disagree, the code wins.

Two deterministic guards, both from the first real-model trial:
- **Commands are never notes.** If a sentence starts with a command verb,
  optionally after a lead-in such as "Finally" (`starts_with_command`), or
  the paragraph looks like code (`looks_like_code`: a block header such as
  `def f(x):`, a call such as `print(x)`, or `x = 1`), the grammar leaves
  out `note`. The 1.5B model otherwise called "Say hello to
  the world." a note, and the program silently did nothing.
- **Every line of text is capped** (240 characters; 300 for explanations)
  and there are at most 8 assumptions. Uncapped, the model sometimes rambled
  in one line until it ran out of tokens.

### Reconciliation rules (no model involved)

**Invariant: a block of steps never outlives the sentence it was written
for.** For each paragraph:

| Situation | Action |
| --- | --- |
| no intent, steps whose origin is in the lockfile | **remove** the block, warn showing what was removed |
| no intent, steps of unknown origin | error: "no sentence above them" |
| intent in lockfile, steps equal to the lockfile's | use them, keeping the file's assumptions; a cached mismatch verdict stands |
| intent in lockfile, steps differ | **review**: model checks the edited steps (never rewrites them); ok → refresh assumptions; mismatch → stop |
| intent not in lockfile, steps have an origin | **revise**: detail again, giving the model the old steps to keep what fits and drop the rest; warn |
| intent not in lockfile, steps of unknown origin | **review** (author wrote both) |
| no steps, intent in lockfile | use the lockfile's detail |
| no steps, intent unknown (or `--retranslate`) | **detail** |

*Origin* (`lockfile::find_origin`) is the lockfile entry sharing the most
steps with the block, with at least half of them in common (comparison after
whitespace normalisation).

## 5. Translation, repair, explanation

- **Request:** system prompt (the Ovis Core reference), 4 few-shot examples,
  the declarations made by other paragraphs (`describe_declarations`), then
  the paragraph with its numbered steps.
- **Grammars:**
  - First attempt: `core_grammar`, generic S-expressions. Functions defined
    by later paragraphs must still be callable.
  - Repair attempts: `restricted_core_grammar(names)`. A form may only start
    with a keyword, a built-in, a libc function, or a function/extern the
    program defines.
- **Context** lists what other paragraphs define in plain text, never as
  Ovis Core (``square: a function, used as `(square n)` where n is int;
  gives back int``), because models paste Ovis Core declarations into their
  code.
- **Copy guards** (small models copy what they are shown):
  - after detailing, steps defining a function another paragraph of the
    same file already defines, and their "In NAME:" steps, are dropped
    (`drop_copied_definitions`);
  - a text literal in the code that shares no word (3+ letters) with the
    paragraph or its steps is a problem for the repair loop
    (`foreign_text`); `use-c`, `link` and `extern` strings are exempt.
- **Repair** adds the previous attempt and the checker's messages to the
  request. Checker messages are written to be handed to the model as they
  are (e.g. "Unknown function 'sqr'. Known functions: square").
- After `max_attempts` (default 3), every paragraph still failing gets an
  **explanation** request (`explanation_grammar`: REASON + SUGGESTION).
  `--verbose` adds the Ovis Core and raw problems to the report.
- **Suggestions are shown only when concise** (`concise_suggestion`), both
  for failures and for too_vague/irrelevant/mismatch verdicts: at most 200
  characters, no Ovis Core, and not ending in `:` (an introduction to code
  that never came). Otherwise the reason is shown alone. The prompts ask
  for one or two short sentences, or one short line of Python-like code.

## 6. Ovis Core

Ovis Core is written as S-expressions; `;` starts a comment.

**Types:** `int` (i64), `float` (f64), `bool`, `str` (char*), `ptr` (void*),
and lists `int[] float[] bool[] str[]` (also `(list int)`). Any type can be
left out or written `?`, and the elaborator infers it.

**Top-level forms:**
- `(fn NAME (PARAM...) [RET] BODY...)`, where a PARAM is `x`, `(x)` or `(x TYPE)`
- `(extern NAME (CTYPE... [...]) CTYPE)`
- `(link "lib")`
- `(use-c "HEADER.h" ["LIBRARY"])` imports a C library (§8)
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

**Statements:** `let set put print if(then/else) while for(inclusive, up)
each break continue return pass`, plus calls. Leniencies:
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
- **What the model sees:** `relevant_c_declarations` picks, deterministically,
  the imported functions and constants sharing words with the paragraph.
  The candidates come from imported libraries and from headers named in any
  paragraph's steps ("(header zlib.h)"). Up to 10 go to the detail pass and
  15 to translation. The repair grammar allows every imported name.

## 9. Model backends

`language_model::chat(messages, grammar)` has three implementations.
`choose(messages, n)` gives the probability of each option `1`..`n` (at
most 8, one digit token each) for the phrase front end (§13). Its default
asks `chat` under the grammar `[1-n]` and is certain of the answer.
- **`builtin_model`**: llama.cpp `b11200`, statically linked.
  - Every layer runs on the first GPU llama.cpp finds (Metal on macOS;
    CUDA or Vulkan if llama.cpp is built with them), else on the CPU.
    `--cpu` forces the CPU. Reproducibility comes from the lockfile, not
    the device: a GPU may break a near-tie differently, but only changed
    paragraphs are translated again.
  - Reports where it loaded (device, or CPU and threads) through the
    progress log.
  - Loads lazily, so lockfile-only builds never load it.
  - Greedy decoding. The grammar is checked on the top token first, and the
    whole vocabulary is scanned only if that token is refused; the result
    is identical either way.
  - Prompt KV is reused only in whole batches, which start only at
    multiples of `n_batch` (512) and at the end of the system message.
    Generated tokens are never reused, so a reply never depends on earlier
    requests. The second start lets questions share a short system message.
  - `choose`: one decode of the prompt, then a softmax over the logits of
    the option tokens. Nothing is generated.
  - Threads: `--threads`, or the performance cores (macOS) or all cores. Context 8192, up to 1024 tokens
    per reply.
- **`endpoint_model`**: an OpenAI-compatible `/chat/completions` server. It
  sends `grammar` (a llama-server extension) unless `--no-grammar` is given.
  `choose` asks for one token with `top_logprobs` 20 and no grammar, and
  falls back to the default when no option is among them.
- **`missing_model`** (in `main.cpp`): stands in when no weights are found.
  It explains how to get a model only when a paragraph actually needs one.

**Weights lookup order:**
1. `--model-path`
2. `$OVIS_MODEL_PATH`
3. weights embedded in the executable
4. `<exe dir>/` or `<exe dir>/../share/ovis/` + `qwen2.5-coder-1.5b-instruct-q4_k_m.gguf`
5. `$XDG_CACHE_HOME/ovis/models/` or `~/.cache/ovis/models/`

**Embedding format:** the executable, padding to a 4096-byte boundary, the
GGUF, then a 32-byte trailer:
- offset (u64, little-endian)
- size (u64, little-endian)
- `OVIS-EMBEDDED-1\0`

It is loaded with `llama_model_load_from_file_ptr` at that offset.
Re-embedding replaces the old weights.

## 10. Lockfile (`<file>.lock`, JSON, version 2)

```json
{
  "ovis_lock_version": 2,
  "model": "qwen2.5-coder-1.5b-instruct-q4_k_m.gguf",
  "model_fingerprint": "size N, fnv <hex of first+last MiB>",
  "details": [
    {"english": "<intent>", "status": "ok|note|too_vague|irrelevant|mismatch",
     "steps": [...], "assumptions": [...], "reason": "...", "suggestion": "..."}
  ],
  "translations": [ {"steps": "1. ...\n2. ...", "core": "(...)"} ],
  "c_headers": { "zlib.h": "<fnv hash of the header>" }
}
```

- Details are keyed by the normalised intent. Translations are keyed by the
  normalised numbered steps.
- Entries for paragraphs no longer in the source are pruned on every build.
  On a failed build, translations are only replaced if the model was used.
- `c_headers` is written by a successful build. If a header's hash differs
  from the one recorded, the build warns.
- Version 1 lockfiles (from before the detail pass) load as empty.

## 11. Invariants

1. Building a fully detailed source with its lockfile changes neither file
   and needs no model (idempotent). The tests check this on every example.
2. The same weights, request and machine give the same model reply and
   the same `choose` probabilities.
3. A detail block never outlives its sentence (§4).
4. A failed build never rewrites `Compiler:` lines; they reflect only
   programs that built.
5. The source file is replaced atomically (write to a temporary file, then
   rename), and only when its text changes. `--keep-source` disables
   writing it.
6. Reports cite line numbers of the file as it is on disk after the build.

## 12. Code map

| Path | Role |
| --- | --- |
| `src/main.cpp` | CLI: run/build/translate/explain/ir on a file or folder, `-I`/`-L`, `model path|pull|embed`, writing sources back |
| `src/ovis/compiler.cpp` | orchestration: reconcile → detail → translate/repair → explain → render |
| `src/ovis/english.cpp` | paragraphs, detail block parse/render, `replace_paragraphs`, lockfile, HTTP client |
| `src/ovis/prompt.cpp` | all prompts, few-shot examples, grammars, reply parsers |
| `src/ovis/builtin_model.cpp` | llama.cpp inference, model lookup, embedding |
| `src/ovis/sexpr.cpp`, `ast.cpp` | Ovis Core reader and lenient parser; `describe_in_english` |
| `src/ovis/elaborate.cpp` | type inference and deterministic fill-ins |
| `src/ovis/checker.cpp` | name/type/format checking |
| `src/ovis/libc.cpp` | libc binding table, C constants, built-in names, printf format parsing |
| `src/ovis/c_import.cpp` | libclang header import, pkg-config flags, relevance ranking of C declarations |
| `src/ovis/modules.cpp` | joining modules: naming, resolution, the helper-file rule |
| `src/ovis/llvm_codegen.cpp` | IR generation, optimisation, object emission, linking |
| `src/ovis/runtime.cpp` | the embedded C runtime |
| `test/test.cpp` | Catch2 tests, including a scripted model that routes replies by grammar |
| `test/clib/` | a small C library the tests import (every type-mapping case) |
| `src/ovis/phrase/` | the phrase front end, in progress (§13) |
| `phrases/*.phr` | the built-in phrase library, compiled in by `cmake/embed_phrases.cmake` |
| `test/phrase_test.cpp`, `test/choice_test.cpp` | front-end and `choose` tests (the latter with a fake HTTP server) |
| `tools/make_tiny_model.py` | tiny random GGUF for testing inference plumbing |
| `examples/` | detailed example programs with hand-written reference lockfiles; `shop/` has several files |

## 13. Phrase front end (in progress, `ovis::phrase`)

The replacement for §2–§5: a deterministic parser of English-like lines,
where the model only picks between readings that type-check. Design in
[proposals/](proposals/phrase-grammar.md), plan in remaining-tasks.md
(M0–M4). Not wired into the CLI yet.

**Lexer** (`split_lines`): logical lines with tokens, block depth and
annotations. Python-like blocks (a deeper line only under a `:` line; no
tabs; dedents must line up); open brackets continue a line; `"…"`, `'…'`,
`“…”` texts; `'s` after a word is a symbol, `don't` one word, `2nd` a word.
`Read as:`/`Compiler:` lines are one-line annotations of the line above,
indented under it.

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

**Front end** (`compile_phrases`, `ovis run --phrases`; minimal: one file,
no functions or lockfile). A line's first words decide its shape (`x =`,
`if`, `otherwise`, `while`, `for each`, `repeat … times`, `stop`, `skip`,
else a statement phrase); its span is read by a chart parser (fixed
operator precedence, phrases, optional articles, plurals) filtered by
types. One reading: lowered. Several, or none (then the shortest span
nothing reads): a question, `choose` over at most 7 readings plus "none of
these" (run-offs for more), accepted at p ≥ 0.6 and margin ≥ 0.3, written
as `Read as: "SPAN" is READING` and applied by text substitution on later
builds, or dropped once SPAN isn't in the line. Each top-level line is one
unit; variables first set in a block are hoisted to a top-level `let`.
