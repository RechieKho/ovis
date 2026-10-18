# Project state

Last updated: 2026-10-18 (M2.1: records in Ovis Core)
(branch `claude/english-llvm-frontend-pso2fc`).
Last dreamed: 2026-10-18 at `d0444d5`. Archived (inactive) entries are in
[dream/state](dream/state/INDEX.md).

Ovis is a compiler whose source code is plain English: lines of phrases,
read by a deterministic parser, with a built-in model choosing between
readings only where a line is unclear. The design is in
[architecture-spec.md](architecture-spec.md) and the open work is in
[remaining-tasks.md](remaining-tasks.md).

## Status at a glance

| Area | Status |
| --- | --- |
| Phrase front end: lines, blocks, phrases, `to …:` phrases, calls | Built, tested (front end, functions, programs) |
| Phrase library (90 templates, prelude) | Built; every template run by a test program |
| Questions: rules, ranking shortcut, `choose`, `Read as:`, lockfile | Built; tested with scripted models; measured with 0.5B–7B (below) |
| Deterministic elaboration, checker, LLVM codegen, C runtime | Built, tested by compiling and running programs |
| Records in Ovis Core (`record`, `new`, `field`, `set-field`) | Built, tested (`record_test`); no surface phrases yet (M2.2) |
| C standard library, any C library via `use "x.h"` (libclang, `ref x`, pkg-config) | Built, tested with a test library, real zlib and SQLite |
| Multi-file programs, nested modules, helper-file rule | Built, tested (`examples/shop`) |
| Built-in llama.cpp model, embedding, lookup, `ovis model pull` (3B) | Built; determinism tested with a tiny random model |
| GPU (Metal etc.) | On by default, `--cpu` to turn off. Measured by the owner on a Mac (M0.5, below) |

## Verified

- `./build/test_ovis`: 51 test cases and 775 assertions pass. Two cases are skipped unless
  `OVIS_TEST_MODEL` is set. The C-library cases skip themselves when
  libclang, zlib or SQLite headers are missing. The `[docs]` case keeps the
  tracking documents under 500 lines and checks their links.
- `OVIS_TEST_MODEL=<tiny.gguf> ./build/test_ovis "[model]"`: the built-in
  model gives the same reply and probabilities from a fresh instance,
  after an unrelated request (prefix reuse), and with embedded weights.
- Every example builds offline from its `Read as:` lines and lockfile, twice,
  with no file changed; `vague.ovis` reports its question (test and CI).
- **GPU (M0.5)**, by the owner on a Mac with Metal, 1.5B, `ovis_eval
  questions --compare-cpu`: 20 questions compared, the largest difference
  in a probability 0.0199, no case decided differently; ~0.23 s per
  question (1.5B on 4 CPU cores here: ~0.9 s).
- One-off manual checks of the old pipeline: archived in
  [dream/state](dream/state/INDEX.md).

## Continuous integration

`.github/workflows/ci.yml` runs on every push and pull request, on Ubuntu
24.04 with LLVM 18. It:
- builds the project and runs `test_ovis`, uploading `test-results.xml`;
- builds every example offline and fails if any example file changes, and
  checks `vague.ovis` fails;
- checks every case of `eval/questions.txt` is still a question
  (`ovis_eval questions --no-model`);
- generates the tiny random model and runs the `[model]` tests with it.

llama.cpp is built with `GGML_NATIVE=OFF` in CI.

`.github/workflows/embedded-model.yml` is started by hand (3B by default,
1.5B or 0.5B). It builds `ovis` with the model embedded, publishes it as an
artifact for 14 days, and reports `ovis_eval questions` and `ovis_eval
programs` in the job summary. These steps have not run on GitHub yet.

## Phrase front end: questions measured

`ovis_eval questions eval/questions.txt`: 26 one-line programs with one
unclear span each and the reading a person means (2 of them: none fits).
Qwen2.5-Coder Q4_K_M, `--cpu`, 4 cores of an Intel Xeon @ 2.8 GHz, 8
readings plus "none of these" per question, so one question per case:

| Model | Right | Confident (p ≥ 0.6, margin ≥ 0.3) | Of those right | Best guess right | Per question |
| --- | --- | --- | --- | --- | --- |
| 0.5B | 2 | 0 | – | 1 | ~0.5 s |
| 1.5B | 7 | 5 | 5 | 18 | ~0.9 s |
| 3B | 23 | 21 | 21 | 23 | ~1.8 s |
| 7B | 25 | 23 | 23 | 25 | ~4.2 s |

- **Ranking (M0.4):** the right reading is first in 13 of 24 and among
  the 8 shown in 24 of 24, so embeddings aren't needed for now. Lexical
  ranking can't tell `mean`, `biggest` or `flip` from the other one-list
  phrases; aliases cover what M1.14 shows is common.
- **8 readings per question, not 7:** a list has 8 one-list readings, so
  with 7 every such question needed a run-off (3 asks). With 8: 26 asks
  instead of 54, and 3B got one more right (23, was 22, with 2 of 23
  confident picks wrong).
- **Thresholds:** 1.5B is rarely sure, but right when it is: at p ≥ 0.4
  and margin ≥ 0.1 it picks 12, all right; 3B picks 24, 22 right. M1.14
  tunes them.
- **M0.3:** 0.5B guesses at random. 1.5B is right when sure,
  but sends 21 of 26 to the author. 3B settles 21 of 26, all right, and is
  the smallest that meets the goal of few questions. Decided: 3B becomes
  the default with M1.13.
- Loading is not counted (1.5B ~3 s, 3B ~9 s warm, up to 140 s cold).

Since then (M1.9 library, M1.14) the ranking shortcut only takes a reading
that covers every content word of the span, after it read "is below zero"
as `is zero`: on this set it settles 1 case, not 6.

## Phrase front end: programs measured (M1.14)

`ovis_eval programs eval/programs`: 20 programs written the way a person
would, with the readings meant in `# expect` lines. Same machine, `--cpu`,
model load not counted (3B: 8–10 s warm).

| Run | Built | Questions per program | Shortcut | Model (right) | To the author (meant) | Time per program |
| --- | --- | --- | --- | --- | --- | --- |
| 1.5B | 8 | median 1, most 3 | 3 (3) | 1 (1) | 17 (3) | median 1.9 s |
| 3B | 15 | median 1, most 3 | 3 (3) | 15 (13) | 4 (2) | median 3.8 s, most 14 s |
| 7B (load 52 s) | 15 | median 1, most 3 | 3 (3) | 14 (13) | 5 (2) | median 8.3 s, most 33 s |
| 3B, + fit filter, loop scope | 16 | median 1, most 3 | 3 (3) | 14 (14) | 4 (3) | median 3.8 s, most 11 s |
| 3B, + aliases from the gaps | 16 | **median 0**, most 3 | 3 (3) | 4 (4) | 4 (3) | median 2 ms, most 9 s |

- Targets: median questions ≤ 2 met; confident picks right ≥ 90% met
  (100% after the fixes); a cold build under 5 s missed with 3B on 4 CPU
  cores then (loading 8–10 s); met after M1.15 (below).
- What the first runs found, now fixed: readings that don't fit where the
  span is (a list where a number is needed) were offered and picked, so
  candidates must now let the line read; a `for each` variable stayed in
  scope after its loop, and the elaborator repaired it to the list's name.
- Left to the author, as meant: "positive numbers", "roll a die", "no
  digits". Not as meant: `character i of sentence in lower case`, which
  reads two ways that give the same result; 3B can't choose.
- Gaps turned into aliases (`has`, `begins with`, `backwards`, `in
  uppercase`, `word count of`, `is below zero`, `max of`, `is an even
  number`, `how many items in`, `average value of`). `biggest`, `lowest`,
  `mean` and `add up` stay questions, because `eval/questions.txt` measures
  them.

## Cold build (M1.15)

`ovis build examples/statistics.ovis` with its `Read as:` line removed (one
question), 3B, `--cpu`, 4 cores, warm disk cache:

| Step | Before | After |
| --- | --- | --- |
| Map the weights | 0.3 s | 0.3 s |
| Repack them for AVX2 (`q4_K_8x8`), every load | 6.8 s | – |
| Create the context | 1.4 s | 0.2 s |
| Questions (system message + question; run-off) | 7.4 s (3 asks) | 2.4 s (1 ask, system message cached); 4.5 s the first time |
| Whole build | 15.9 s | **2.6 s** (4.8 s the first time) |

- Questions are ~0.3 s slower without repacking; decisions on the 26
  questions were the same either way.
- 9 readings per question ("none of these" as `0`) ended most run-offs.
  Run-offs had hurt: in the final round "none" faces one reading, and 3B
  confidently picked `average of numbers` for "median" and "standard
  deviation". With 9: 23 of 26 right, confident picks 20 of 21 (95%).
- A cold disk cache is another matter here: 140 s to page in 2 GB.
- "roll a die" now reads as `ask for a whole number` (its left-out-argument
  readings are gone); it was meant to go to the author.

## Not verified / known gaps

- The prompt cache's states (~3 MB each) are never cleaned up.
- `ovis_eval` results come from 26 questions and 20 programs written by the
  developer: real authors will write differently.
- The front end writes no `Compiler:` lines: the elaborator's notes are
  dropped.
- Builds and passes tests on macOS arm64 with Homebrew LLVM 22 (owner's
  run, before M1); the C-library case looks for headers in `/usr/include`,
  which macOS keeps in the SDK. Windows is not supported.

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
| M0 | Default model: Qwen2.5-Coder-3B for the phrase front end, switched in M1.13 (the old pipeline was tuned on 1.5B). A model counts by the questions it settles, not only by how often its confident picks are right. |

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
| … | Earlier history: [dream/state](dream/state/INDEX.md) |
| `a3d9d95` | Phrase-grammar proposal and plan (M0–M4); old pipeline tasks archived |
| `7379932` | M0.1 `choose` (built-in, endpoint, fallback); M1.1 lexer; M1.2 `.phr` templates, embedded at build time |
| `b473348` | Minimal phrase front end end to end (`--phrases`): chart parser, questions, `Read as:`, prelude, timings |
| `3d90bd8` | Proposal for `at the end` (Zig's `defer`) and its tasks D1–D3 |
| `a2d5a67` | `at the end` proposal accepted as recommended |
| `d3ed2dc` | M0.2, M0.4: `ovis_eval questions`, 26 labelled questions, 0.5B–7B measured; 8 readings per question |
| `baee054` | M0.3 decided: 3B becomes the default model with M1.13 |
| `a65ae35` | Phrase front end split into parser, questions and reader |
| `941d496` | M1.3–M1.5: `to …:` phrases, `give back`, `name(args)` calls of user and C functions, locals, line mapping; phrase-front-end.md |
| `5a6e999` | M1.6, M1.7: question kinds (unknown name, `it`, left-out argument) with rules first, ranking shortcut, how each question was settled |
| `1ce0e5c` | M1.8: phrase lockfile (version 3) for the model's readings; rebuilds change neither file |
| `6dd7c1c` | M1.10, M1.11: programs of several files, `use "x.h"` and `ref x` for C, `--phrases` on folders |
| `296834b` | M1.9: phrase library of 90 templates with a test program each; reading a number skips its line ending |
| `6f19e00` | M1.12: the examples in phrases (`examples/phrases/`), built offline in CI; readings that group differently read differently |
| `d0444d5` | M1.14: 20 labelled eval programs, `ovis_eval programs`, 1.5B/3B/7B measured; candidates must fit the line; loop variables end with their loop; strict shortcut; commas in phrase headers; aliases from the gaps |
| `e58c93a` | M1.13: phrases are the only front end; the paragraph pipeline, its prompts, lockfile v2 and tests removed; default model 3B; spec and README rewritten; M0.5 recorded |
| `69cadbd` | M1.15: cold build 15.9 s → 2.6 s: no repacking, system-message state cached on disk, 9 readings per question |
| `97708d9` | M1.16: problems in the program's words: English types, phrases by their headers, the line quoted |
| (next) | M2.1: records in Ovis Core: declared, created, read, set, printed as written; field types and a parameter's record inferred |
