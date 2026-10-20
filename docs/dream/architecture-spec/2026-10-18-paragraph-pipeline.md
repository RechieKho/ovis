# The paragraph pipeline (detail, translation, repair)

Archived from architecture-spec.md → §1–§5, §8 "What the model sees", §10, §11–§13 on 2026-10-18 (dream at `d0444d5`). M1.13 removed this pipeline: the phrase front end replaced it. Kept word for word.

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

- **What the model sees:** `relevant_c_declarations` picks, deterministically,
  the imported functions and constants sharing words with the paragraph.
  The candidates come from imported libraries and from headers named in any
  paragraph's steps ("(header zlib.h)"). Up to 10 go to the detail pass and
  15 to translation. The repair grammar allows every imported name.

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

The replacement for §2–§5, behind `--phrases` until M1.13: described in
[phrase-front-end.md](phrase-front-end.md).
