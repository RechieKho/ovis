# Project state

Last updated: 2026-10-18 (M2.1: records in Ovis Core)
(branch `claude/english-llvm-frontend-pso2fc`).
Last dreamed: 2026-10-18 at `9d57501`. Archived (inactive) entries are in
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
| Questions: rules, ranking shortcut, `choose`, `Read as:`, lockfile | Built; tested with scripted models; measured with 0.5B–7B (M0, archived; M1.14 below) |
| Deterministic elaboration, checker, LLVM codegen, C runtime | Built, tested by compiling and running programs |
| Records in Ovis Core (`record`, `new`, `field`, `set-field`) | Built, tested (`record_test`); no surface phrases yet (M2.2) |
| C standard library, any C library via `use "x.h"` (libclang, `ref x`, pkg-config) | Built, tested with a test library, real zlib and SQLite |
| Multi-file programs, nested modules, helper-file rule | Built, tested (`examples/shop`) |
| Built-in llama.cpp model, embedding, lookup, `ovis model pull` (3B) | Built; determinism tested with a tiny random model |
| GPU (Metal etc.) | On by default, `--cpu` to turn off. Measured by the owner on a Mac (M0.5, below) |

## Verified

- `./build/test_ovis`: 51 test cases and 777 assertions pass. Two cases are skipped unless
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
  (`ovis_eval questions --no-model`), and that the ranking keeps its
  floor (`--min-first 10 --min-shown 23`), with the numbers in the job
  summary;
- generates the tiny random model and runs the `[model]` tests with it.

llama.cpp is built with `GGML_NATIVE=OFF` in CI.

`.github/workflows/embedded-model.yml` is started by hand (3B by default,
1.5B or 0.5B). It builds `ovis` with the model embedded, publishes it as an
artifact for 14 days, and reports `ovis_eval questions` and `ovis_eval
programs` in the job summary. These steps have not run on GitHub yet.

## Phrase front end: questions measured

M0's measurements of ranking and of 0.5B–7B on `eval/questions.txt` (3B
settled 21 of 26, all right; decided: 3B is the default) are archived in
[dream/state](dream/state/INDEX.md).

Ranking now (CI's floor): the right reading first in 10 of 24 and shown in
the first question in 23 of 24. M0.4 measured 13 and 24 with 8 readings;
since then (the library, aliases, 9 shown) "3.7 rounded down" fell to 10th
of 14, so the 9 shown miss it.

Why (2026-10-18): in all 24 cases the right reading ties for the top
lexical score; the score never puts a wrong reading above it. It is blind
to synonyms (`floored` shares no word, prefix or trigram with any
template), so ties fall back to library order, and templates added before
`rounded down` pushed it to 10th. Only 8 cases have a unique top. The
miss costs a run-off ask, not a wrong reading: `choose` asks in groups of
9, so the model still sees every tied reading. A scratch probe (not in the
repository) broke ties by cosine similarity of the span and each reading,
using last-token-pooled hidden states of the Qwen models already used:
first in 20 of 23 non-shortcut cases (9 today), shown in 23 of 23, with
both 0.5B and 3B. Mean pooling: 15 (0.5B) and 18 (3B). Remaining misses
(0.5B): `last item`, `rounded down`, `as text`, each 2nd or 3rd. Cost on 4
CPUs, one text at a time: about 0.5 s per question with 0.5B, 1.6 s with
3B; batching the readings in one decode should cut this. 23 cases and 4
variants tried, so expect some overfit.

Built in `8ffe5c3` and reverted: ranking first rose to 21 of 24 (3B), 18
(1.5B), 20 (0.5B), but right picks did not: 3B 23 → 23 of 26 (fixes
"mean", but "median" becomes a confident "average", now listed first),
1.5B 8 → 4, 0.5B 3 → 3. The run-off already shows the model every tied
reading, and its answer depends on option order. Choosing the 9 shown by
embedding, in library order with no run-off, asked 25 questions instead
of 37 but 3B fell to 21. Ranking first is a poor stand-in for right picks.

Option order alone (same readings, 6 orders: as ranked, reversed, 4
shuffles): 3B right in 20–23 of 26, 1.5B in 8–12. The best guess barely
moves (3B 22–23, 1.5B 17–19); the order decides whether it clears the
60%/30% confidence bar. 3B is right in every order on 17 cases. "none"
cases are fragile: "median" and "standard deviation" become a confident
"average" in 3 and 2 of 6 orders. So one order can't measure a change of
3 or less: the tie-break (23) and the 9-shown variant (21) are both within
it. The 1.5B tie-break (4) is below every order seen.

`ovis_eval questions --orders N` now reports this. 3B, 6 orders: right in
22 to 23 of 26 (mean 22.7); 21 cases right in every order, 2 in none
("put 5 into", "write": always unsure), 3 depend on the order ("mean" 2
of 6, "highest" 3 of 6, "read a line of text" 5 of 6).

Embeddings only (`--embeddings-only`: ties ordered by embedding, the
first taken, nothing asked): 3B 21 of 26 right, 0.5B 20, against 3B asked
22–23. Per case 0.3 s (0.5B, 0.5 GB) and 1.2 s (3B), against about 2 s
per question asked with 3B. The difference is in the misses: all of the
embeddings' misses are wrong picks (3B: "first item" read as "last item",
"as a string" as "as a whole number", "order" as "show", and "median" and
"standard deviation" as "average"), while 3B asked is mostly unsure,
which goes to the author. The similarity margin doesn't tell them apart:
wrong picks have margins 0.001 to 0.075, like right ones. So the model's
edge as a matchmaker is 2–3 cases of 26 and knowing when it doesn't know.

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
| `9d57501` | M2.1: records in Ovis Core: declared, created, read, set, printed as written; field types and a parameter's record inferred |
| `5178383` | Ranking floor: `ovis_eval questions --min-first/--min-shown`, set in CI to 10 and 23, numbers in the job summary |
| `3ed520a` | `ovis_eval questions --orders N`: every question in N orders of its readings; the model workflow uses 4 |
| (next) | Embedding experiments behind `front_end_options` (off): `builtin_model::embed`, ties ordered by embedding, `ovis_eval questions --embeddings-only` |
| (next) | Phrase self-test: `ovis_eval questions --with PROGRAM` lists the cases a program's phrases make worse; `examples/shop` worsens one (task added) |
