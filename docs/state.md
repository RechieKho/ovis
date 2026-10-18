# Project state

Last updated: 2026-10-18 (phrase pivot: a minimal front end runs end to end)
(branch `claude/english-llvm-frontend-pso2fc`).
Last dreamed: 2026-10-17 at `ff0234b`. Archived (inactive) entries are in
[dream/state](dream/state/INDEX.md).

Ovis has been rebuilt from an early LLVM expression front end into a
compiler whose source code is plain English. The design is in
[architecture-spec.md](architecture-spec.md) and the open work is in
[remaining-tasks.md](remaining-tasks.md).

## Status at a glance

| Area | Status |
| --- | --- |
| Detail pass (steps, assumptions, verdicts, written into the source) | Built, tested with a scripted model |
| Steps never outliving their sentence (remove / revise / review) | Built, tested with a scripted model |
| Translation, repair loop, explanations of failures | Built, tested with a scripted model and a mock HTTP server |
| Deterministic elaboration (types, name repair, defaults) | Built, tested |
| Checker, LLVM codegen, C runtime | Built, tested by compiling and running programs |
| C standard library calls, `extern`, `link` | Built, tested (math, string, ctype, printf, file I/O) |
| Any C library via `(use-c ...)` (libclang import, `(ref x)`, pkg-config) | Built, tested with a test library, **real zlib and SQLite** |
| Multi-file programs, nested modules, helper-file rule | Built, tested (`examples/shop`, name resolution, loading) |
| Built-in llama.cpp model, embedding, lookup | Built; plumbing and determinism tested with a tiny random model |
| **Quality with a real model** | **Not tested at all.** See "Not verified / known gaps" below. |
| `ovis model pull` | Built and run: downloads the 1.5B model (Hugging Face is now allowed) |
| GPU (Metal etc.) | On by default, `--cpu` to turn off. **Only the CPU path was run** (no GPU here). |
| **Phrase front end** (the pivot) | A minimal slice runs end to end with `ovis run --phrases` (one file, no functions, no lockfile): parse, type filter, questions to the model, `Read as:` write-back, codegen. Tested with a scripted model and run with real ones. |

## Verified

- `./build/test_ovis`: 52 test cases and 547 assertions pass. Two cases
  are skipped unless `OVIS_TEST_MODEL` is set. The C-library cases skip
  themselves when libclang, zlib or SQLite headers are missing. The `[docs]` case keeps the
  tracking documents under 500 lines and checks their links.
- `OVIS_TEST_MODEL=<tiny.gguf> ./build/test_ovis "[model]"` passes. It shows
  the built-in model gives the same reply:
  - from a fresh instance;
  - after an unrelated request (prefix reuse);
  - with embedded instead of separate weights.
- llama.cpp accepts all four grammars (detail, review, core, restricted
  core) and enforces them, checked with the tiny model.
- All 6 runnable examples build offline from their lockfiles. Their sources
  are byte-identical after repeated builds. The 7th, `vague.ovis`, is
  rejected with reasons, as intended.
- One-off manual checks (mock server, CLI scenarios): archived in
  [dream/state](dream/state/INDEX.md).

## Continuous integration

`.github/workflows/ci.yml` runs on every push and pull request, on Ubuntu
24.04 with LLVM 18. It:
- builds the project;
- runs `test_ovis`, uploading `test-results.xml`;
- builds every example offline and fails if any example file changes;
- generates the tiny random model and runs the `[model]` tests with it.

Every step was run in a fresh clone in a cloud session before it was
committed. llama.cpp is built with `GGML_NATIVE=OFF` in CI.

`.github/workflows/embedded-model.yml` is started by hand (1.5B or 0.5B).
It builds `ovis` with the real Qwen2.5-Coder model embedded, and publishes
it as an artifact for 14 days. It also runs the original English of four
examples through the model and reports the results in the job summary.
That is the only place a real model runs, because cloud sessions can't
download it.

## Phrase front end: first measurements

`choose` with real Qwen2.5-Coder models (Q4_K_M, 4 CPU cores), on four
hand-written questions (mean→average, cost→price, add up→sum, "it"→the
list; the first asked twice), after the model was loaded:

| Model | Per question | Right | Right and confident (p ≥ 0.6, margin ≥ 0.3) |
| --- | --- | --- | --- |
| 1.5B | ~250 ms | 4 of 4 | 0 of 4 (best p 0.39–0.55) |
| 3B | ~500 ms | 4 of 4 | 4 of 4 (p 0.86–0.97) |

The repeated question gave identical probabilities, reusing the
system-message batch. These were one-off runs, not a test, and four
questions only hint at M0.3: 3B looks like the smallest model that settles
questions on its own; 1.5B would send them all to the author. Loading took
3 s (1.5B) to 140 s (3B, cold disk cache) and isn't counted.

### End to end (`eval/phrases/statistics.ovis`, 16 lines)

`ovis build --phrases --cpu`, 4 cores of an Intel Xeon @ 2.8 GHz,
llama.cpp with AVX2/FMA (see the lesson below):

| Step | 1.5B | 3B |
| --- | --- | --- |
| Lex, parse, type filter, questions built | < 1 ms | < 1 ms |
| Model loading (warm disk cache) | ~3.6 s | ~9 s |
| Per question | ~0.56 s | ~1.06 s |
| Questions asked (one unclear span; 9 readings, so a run-off of 2 groups and a final) | 3 | 3 |
| Check + LLVM codegen + link | ~0.2 s | ~0.2 s |
| Whole build | 5.5 s | 12.3 s |
| Rebuild from the `Read as:` line, no model | 24 ms | 24 ms |

Both read "mean of numbers" as `average of numbers` (final round 68% and
94%), so 1.5B's answer is only just above the 0.6 threshold. Wording
matters a lot:
- With the variable on the line called `average` (`average = mean of
  numbers`), 3B picked `sum of numbers`, confidently (78% in the final),
  and 1.5B was unsure; 7B picked average (83%).
- The question "Which phrase means the same as …?" beat "… most likely
  means:".
- Lexical ranking can't tell `mean` from `length`, so all 9 one-list
  readings go to the model; better ranking (M0.4) would save 2 of 3
  questions.
- A cold disk cache made loading take 80 s (3B) to 5 min (7B).

## Not verified / known gaps

- **First real-model trial (Qwen2.5-Coder 1.5B, CPU): 0 of 4 examples
  correct.** Findings to fix:
  - Instructions ("Say hello to the world.", "Finally tell whether 97 is
    prime.") are marked as notes, so they compile to nothing, silently.
  - A translation copied the first few-shot example verbatim (fizzbuzz
    printed "Average: 82.33").
  - "Collect every prime below 50" was hard-coded as a list of primes.
  `ovis_eval detail eval/detail-cases.txt` measures the verdicts: 15 of 23
  right at first, **22 of 23** after the command rule, line caps and new
  few-shot examples (the miss calls a vague paragraph irrelevant, which
  still stops the build). The steps are still weak: the model adds work
  nobody asked for (an average after a list of values, copied from an
  example), calls 21.5 a whole number, and muddles counting down.
- **Python-like code** (with the real model): `def square` plus a
  `for i in range(1, 6): print(square(i))` loop builds and prints the right
  squares, but the steps written under the code are poor ("The rest of the
  program already does this.", an invented "print square of 5"). The
  translation follows the code, not the steps. 26 of 27 verdicts with the
  four Python-like eval cases.
- **Model sizes compared** (Qwen2.5-Coder Q4_K_M, 4 CPU cores, before the
  copy guards below; 5 programs = 4 original examples + Python-like squares):

  | Model | Verdicts (27) | Programs right (5) | Eval time |
  | --- | --- | --- | --- |
  | 1.5B | 26 | 2 | 4 min |
  | 3B | 24 | 2 | 7 min |
  | 7B | 27 | 2 (+ primes right but for an extra line) | 12 min |

  Most failures at every size were copying, now guarded against:
  - the context listed functions as `(fn square ((n int)) int)`, and models
    pasted it as an empty function: context is now plain text
    (``square: a function, used as `(square n)` where n is int; ...``);
  - the detail pass copied another paragraph's "Define a function" steps,
    so the function was defined twice: such copies are dropped;
  - a translation printed "Average: 82.33" (a few-shot example) for a
    program about squares: text sharing no word with the paragraph or its
    steps is now a problem the repair loop must fix.
  Rerun with the guards (`160a25e`), programs only: **7B 3 of 5** (hello,
  fizzbuzz, squares; primes right but worded differently; temperature
  built but printed wrong values), **1.5B 1 of 5** (hello). 1.5B wrote
  "The rest of the program already does this." as the loop's steps and
  translated it into a copy of a few-shot example (read a number, print
  its square), which has no text for the guard to catch.
- The model still copies few-shot replies word for word: "Open a window
  and draw a red circle" was called irrelevant with the reason "It is
  about the author", from the irrelevant example.
- **Example lockfiles are hand-written** by the developer acting as the
  model (marked `"model": "hand-written reference translation"`).
- **Speed**: the 1.5B model on 4 CPU cores took 24–45 s per example.
  The GPU path (on by default) has not been measured.
- Builds and passes tests on macOS arm64 with Homebrew LLVM 22 (29 pass,
  2 skip: no model, and the C-library case looks for headers in
  `/usr/include`, which macOS keeps in the SDK). Otherwise Linux. Windows is not supported:
  renaming the running executable, `/proc`, and the tests use `popen` with
  `printf`.

## Environment notes (cloud sessions)

- Install `llvm-18-dev` and `libclang-18-dev`
  (`apt-get install -y llvm-18-dev libclang-18-dev`). The base image has
  the runtime libraries but not the headers. Without libclang, `use-c` is
  disabled and its tests skip.
- zlib and SQLite headers and `pkg-config` are already in the image; the
  C-library tests use them.
- Build: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j4`.
  Debug builds of llama.cpp are about 10× slower.
- `huggingface.co` and its CDN (`*.hf.co`) are allowed now, so
  `ovis model pull` works. GitHub and PyPI are reachable. The 1.5B, 3B and
  7B models were already in `~/.cache/ovis/models` in the 2026-10-18
  session.
- The tiny test model:
  - `pip install numpy gguf`
  - `ggml-vocab-qwen2.gguf` is in `build/_deps/llama-src/models/`
  - `python tools/make_tiny_model.py <vocab> tiny.gguf`

## Pinned dependencies

| Dependency | Version | Notes |
| --- | --- | --- |
| llama.cpp | `b11200` | 2026-09-26; uses `llama_model_load_from_file_ptr`, `llama_memory_*` |
| Catch2 | v3.8.1 | |
| nlohmann/json | v3.11.3 | |
| cpp-httplib | v0.18.3 | picks up OpenSSL if present |
| LLVM | system 18 | also builds with 22; API differences guarded by `LLVM_VERSION_MAJOR` |

## Decisions made with the project owner

| When | Decision |
| --- | --- |
| Session 1 | English → small LLM → strict IR (Ovis Core) → LLVM. The model never writes IR. |
| Session 1 | The lockfile caches translations for reproducible, offline builds. |
| Plan 1 | Built-in model weights: lookup or download by default, embedding optional. Default model Qwen2.5-Coder-1.5B Q4_K_M. |
| Plan 1 | libc: a curated table plus `extern`/`link` now; libclang header import later. |
| Plan 1 | Keep the HTTP endpoint backend as an option. |
| Plan 1 | `/` is real division; `(div a b)` is integer division. |
| Plan 2 | The detail pass writes into the **source file itself**, under the author's words (not a separate file). Authors may edit the steps. |
| Plan 2 | Failures after all repair attempts get a model explanation; raw errors only with `--verbose`. |
| Follow-up | Steps never outlive their sentence: remove leftovers, revise on a sentence change, review edited steps. |
| Plan 3 | Tracking docs under 500 lines; dreaming archives inactive memory into `docs/dream/`. |
| Plan 4 | Modules: a folder with `main.ovis` is the program; helper files only define; subfolders are nested modules. |
| Plan 4 | Any C library through `use-c` + libclang; dynamic linking by default. |
| Plan 5 | **Pivot:** a deterministic English-like phrase grammar; the model only picks between type-checked readings, with a time budget. Records, then traits. Design in [proposals/](proposals/phrase-grammar.md), plan M0–M4 in remaining-tasks.md. |

## Lessons learned

- Round-trip what Ovis writes back into the source. A wrapped line starting
  with "30.1." was once read back as step 30 and corrupted a file on the
  second build. Tests now build every example repeatedly and require
  byte-identical output.
- Mock model servers should match only the paragraph part of a request. A
  mock that matched words in the context section once gave two paragraphs
  the same steps, which looked like an Ovis bug.
- Build llama.cpp in Release. The Debug build was about 10× slower and made
  inference look broken.
- `pkill -f <pattern>` in the sandbox can match and kill the shell running
  it. Kill by PID instead. (`pgrep -f` has the same trap.)
- A stray process spinning one core made the model 10–40× slower: llama.cpp's
  threads wait on each other, so one busy core stalls them all. Before
  timing the model, check nothing else is running (`ps -eo pid,pcpu,comm`).

- The session moved to another CPU after a worker restart, and the
  llama.cpp built with `GGML_NATIVE` died with "Illegal instruction".
  `-DGGML_NATIVE=OFF` alone turns AVX2 off too and made questions 20×
  slower. Build with `-DGGML_NATIVE=OFF -DGGML_AVX=ON -DGGML_AVX2=ON
  -DGGML_FMA=ON -DGGML_F16C=ON` in cloud sessions.

- Set `CMAKE_CXX_STANDARD` before `FetchContent_MakeAvailable`. Set after
  it, Catch2 built as C++14 on Apple clang without `string_view` support
  and the tests failed to link.

- Test the path where nothing is translated yet. The first translation of
  a paragraph that introduces a C library couldn't see the library's
  functions until the context also imported headers named in the steps.
  A scripted-model test caught it.

## History

| Commit | Summary |
| --- | --- |
| `c2a8763` | English front end: paragraphs, LLM translation, Ovis Core, checker, LLVM codegen, lockfile |
| `f913137` | Elaboration pass (inference, name repair, defaults); libc support, `extern`, `link`, text functions |
| `13ce853` | Built-in llama.cpp model, restricted repair grammar, embedding, `ovis model` commands |
| `4c8d08a` | Detail pass written into the source; vague/irrelevant rejection; explanations; lockfile v2 |
| `8afe40d` | Steps can't outlive their sentence (remove / revise / review); correct line numbers after rewrite |
| `fc5d89c` | Tracking documents; dreaming archives inactive memory into `docs/dream/`; 500-line limit enforced by a test |
| `d0fa4d1` | Any C library: libclang import, narrow/unsigned C types, `(ref x)`, pkg-config |
| `ff0234b` | Multi-file programs with nested modules; C context for the model; header hashes in lockfiles |
| `bd93d5b` | `ovis_eval` for the detail pass's verdicts; first real-model results |
| `0438695` | Model on the GPU by default (`--cpu` to turn off); threads default to performance cores |
| `0f9c408` | Commands are never notes; capped text lines; detail verdicts 15 → 22 of 23 |
| `f3ffa30` | Python-like pseudo-code in paragraphs; detail block starts only at an item line; lenient `let`/bodiless `fn` |
| `97b690e` | Only concise suggestions are shown (short, no Ovis Core); the prompts ask for short ones |
| `80188d7` | `ovis run`: on Linux the program dies with ovis instead of running on |
| `160a25e` | Context in plain text; copied definitions dropped from steps; copied example text rejected |
| `a3d9d95` | Phrase-grammar proposal and plan (M0–M4); old pipeline tasks archived |
| `7379932` | M0.1 `choose` (built-in, endpoint, fallback); M1.1 lexer; M1.2 `.phr` templates, embedded at build time |
| (next) | Minimal phrase front end end to end (`--phrases`): chart parser, questions, `Read as:`, prelude, timings |
