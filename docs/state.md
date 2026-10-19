# Project state

Last updated: 2026-10-18 (P0 finished: `at the end`, M2 and M3 leftovers)
(branch `claude/english-llvm-frontend-pso2fc`).
Last dreamed: 2026-10-18 after P0 (finished tasks, the spec's model-backends summary archived, C libraries moved to c-libraries.md). Archived (inactive) entries are in
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
| Phrase library (93 templates, prelude, generic functions) | Built; every template run by a test program |
| `at the end` / `defer` (D1–D3): `(defer …)` in Ovis Core, copied to every exit of its block; the surface forms with the `Compiler:` note | Built, tested (`defer_test`, `examples/notes.ovis`, `eval/programs/tidy.ovis`) |
| Built-in traits: slot types `showable`, `comparable`, `equatable`, `T:trait`; a record implements one with `to (a: book) comes before (b: book):`, `is the same as`, `text of`, or `a book is comparable:` (equatable, showable); `nothing`; lists of records with `text of` show as `[a, b]` | Built, tested (`record_phrase_test`, `phrase_library.txt`); static dispatch |
| User traits: `a NAME can:` (required and default phrases, `it` the value), `a NAME is a TRAIT:`; a trait is a type (`(s: shape)`, `list of shape`, mixed lists), phrases on it dispatch when the program runs; untyped values used only through one trait's phrases get that trait | Built, tested (`trait_test`, `trait_phrase_test`, `examples/shapes.ovis`) |
| Questions: rules, ranking shortcut, `choose`, `Read as:`, lockfile | Built; tested with scripted models; measured with 0.5B–7B (M0, archived; M1.14 below) |
| Deterministic elaboration, checker, LLVM codegen, C runtime | Built, tested by compiling and running programs |
| Records in Ovis Core (`record`, `new`, `field`, `set-field`) | Built, tested (`record_test`) |
| Records in phrases: `a NAME has:`, `a NAME with …`, fields read and set 3 ways, `field of T` slots, questions about fields and `the NAME` | Built, tested (`record_phrase_test`, `phrase_library.txt`, `examples/library.ovis`); measured with 3B (eval programs, below) |
| C standard library, any C library via `use "x.h"` (libclang, `ref x`, pkg-config) | Built, tested with a test library, real zlib and SQLite |
| Multi-file programs, nested modules, helper-file rule | Built, tested (`examples/shop`) |
| Built-in llama.cpp model, embedding, lookup, `ovis model pull` (3B) | Built; determinism tested with a tiny random model |
| Model daemon (`ovis model serve`, started by builds, in-process fallback) | Built; tested with a scripted model and, `[model]`, started by `ovis` |
| GPU (Metal etc.) | On by default, `--cpu` to turn off. Measured by the owner on a Mac (M0.5, below) |

## Verified

- `./build/test_ovis`: 90 test cases and 1144 assertions pass. Three cases are skipped unless
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

A second job in `ci.yml` runs on macOS 15 (arm64): Apple clang, linked
against Homebrew's LLVM (22 at the time of writing). It builds, runs
`test_ovis` and checks the examples offline. The real-C-library test skips
there (headers are in the SDK, not `/usr/include`), and the job skips the
`[model]` tests and the question ranking, which the Ubuntu job covers.

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

Why, and what was tried (2026-10-18, archived in
[dream/state](dream/state/INDEX.md)): the right reading always ties for the
top lexical score, which is blind to synonyms, so ties fall to library
order. Breaking ties by embeddings ranked it first far more often but
didn't raise the model's right picks; option order alone moves 3B by 3 of
26 (`--orders`: 22–23, mean 22.7); embeddings alone get 20–21 of 26 but
every miss is a wrong pick, never "none". Code kept, off
(`--embeddings-only`).

Hard wording (`eval/hard-questions.txt`, 20 held-out cases: one phrase,
values written plainly, words far from the phrase's), 4 orders:

| Settled by | Right (of 20) | Confident picks right | Per question |
| --- | --- | --- | --- |
| Embeddings only, 0.5B / 3B | 4 / 4 | (always picks) | — |
| 3B asked | 12–15 (mean 13.5) | 13 of 19 (68%) | 2.3 s |
| 7B asked | 16–18 (mean 17.5) | 16 of 18 (89%) | 5.3 s |

Embeddings collapse: shared words give a wrong reading a top score of its
own (`how many folks` → `length`), so ties rarely reach them. 3B stays
ahead but is confidently wrong on 6. Model size is what reaches hard
wording; the cost is the time per question (4 CPUs here).

## Model daemon measured

`ovis run`, 4 CPUs, weights in the OS file cache, median of 5 (3 for 7B),
each build asking again:

| Program | 3B: no daemon / starts it / loaded | 7B: no daemon / starts it / loaded |
| --- | --- | --- |
| no question | 0.24 / 0.23 / 0.23 s | 0.20 / 0.21 / 0.22 s |
| 1 question | 3.21 / 3.73 / 2.52 s | 6.06 / 8.00 / 5.25 s |
| 3 questions | 7.67 / 8.27 / 6.98 s | 16.57 / 18.71 / 15.93 s |

The daemon saves 0.7–0.8 s a build (4–21%); starting it costs 0.5 s (3B)
or 2 s (7B) once. Loading from the file cache is cheap (mmap); answering
is the cost: about 2.3 s (3B) and 5.3 s (7B) per question on 4 CPUs,
against ~0.23 s for 1.5B on a Mac GPU (M0.5).

Where a question's time goes (3B, 4 CPUs): reading the prompt, about 25 ms
a token, 70–136 new tokens a question (the system message comes from the
cache). Repacking the weights for the CPU (`use_extra_bufts`) read 10–15%
faster but adds 5–7 s to each load: left off. Run-off questions now reuse
their shared header (the line, names and question, ~38 tokens), so their
later questions read about 40% fewer tokens. Same machine, hard set: 2.23
→ 2.00 s a question on average, answers identical; easy set 22.7 of 26
over 6 orders as before.

Decided (owner): GPU first, and cheaper questions. With a GPU the default
model is 7B; on the CPU 3B (`default_model_for`).

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
| 3B, M2: 23 programs (3 with records) | 20 | median 0, most 3 | 5 (5) | 8 (7) | 3 (2) | median 3 ms, most 7 s |

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
- M2 (records): `reading`, `team` and `contacts` ask about unknown
  fields (`cost of novel` → price, `length of novel` → pages, `total
  points of team` → score; 3B right in all 3) and `the book`/`the player`
  (one variable each: the rule). The one wrong model pick is "roll a die"
  (now `ask for a whole number`, meant none); the build before M2 picks it
  too, so it changed earlier (likely `c043cee`), not with records.
- Gaps turned into aliases (`has`, `begins with`, `backwards`, `in
  uppercase`, `word count of`, `is below zero`, `max of`, `is an even
  number`, `how many items in`, `average value of`). `biggest`, `lowest`,
  `mean` and `add up` stay questions, because `eval/questions.txt` measures
  them.

Cold build (M1.15: 15.9 s → 2.6 s, no repacking, cached system message;
a cold disk cache takes 140 s to page in 2 GB): archived in
[dream/state](dream/state/INDEX.md).

## Not verified / known gaps

- The prompt cache's states (~3 MB each) are never cleaned up.
- `ovis_eval` results come from 26 questions and 20 programs written by the
  developer: real authors will write differently.
- The front end writes `Compiler:` lines only for `at the end` in a loop,
  branch or block: the elaborator's notes are otherwise dropped.
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
| Plan 3 | Tracking docs under 500 lines; dreaming archives inactive memory into `docs/dream/`. |
| Plan 4 | Modules: a folder with `main.ovis` is the program; helper files only define; subfolders are nested modules. |
| Plan 4 | Any C library through `use-c` + libclang; dynamic linking by default. |
| Plan 5 | **Pivot:** a deterministic English-like phrase grammar; the model only picks between type-checked readings, with a time budget. Records, then traits. Design in [proposals/](proposals/phrase-grammar.md), plan M0–M4 in remaining-tasks.md. |
| Superseded | The paragraph pipeline's decisions (Session 1, Plans 1–2): [dream/state](dream/state/INDEX.md). Still in force from them: the lockfile, the endpoint backend, `/` as real division. |
| M0 | Default model: Qwen2.5-Coder-3B for the phrase front end, switched in M1.13 (the old pipeline was tuned on 1.5B). A model counts by the questions it settles, not only by how often its confident picks are right. |
| 2026-10-19 | Memory management is explicit and strict: no garbage collector or reference counting; every new value is made where a statement stores it; `free` (shallow), `with new memory:` arenas, `keep`; escapes are compile errors; checked by default (`--fast`), leak report opt-in. [proposals/memory.md](proposals/memory.md), plan E1–E7. |
| 2026-10-18 | The model only chooses (no generative job). GPU first: 7B where a GPU runs it, 3B otherwise; make questions cheaper. Judge model changes over several orders (`--orders`). |

## Lessons learned

- Round-trip what Ovis writes back into the source. A wrapped line starting
  with "30.1." was once read back as step 30 and corrupted a file on the
  second build. Tests now build every example repeatedly and require
  byte-identical output.
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

## History

| Commit | Summary |
| --- | --- |
| … | Earlier history: [dream/state](dream/state/INDEX.md) (to `1597534`) |
| `c043cee` | GPU first: 7B is the default where a GPU runs it, 3B otherwise (`ovis model pull [3b|7b]`, lookup order); run-off questions reuse their header (10% less time a question on 4 CPUs) |
| `1ef7e55` | M2: records in phrases: `a NAME has:` with types and defaults, `a NAME with …`, `F of X`/`X's F`/`X.F` read and set, record-typed parameters; `field of T` slots with generic functions (`sorted by`, `sort … by`, `total … of`, `the T in xs with the highest/lowest F`); questions about unknown fields and `the NAME`; text ordered by `<`; `examples/library.ovis`, 3 eval programs |
| `5e67363` | M3: built-in traits: trait slot types (`T:comparable`, `for records:` Core, `=> records`), library rewritten (`sort`, `smallest of`, `largest of`, `contains`, `show`, `as text`), records implement them by `to …:` phrases; `<`, `=`, `+` call them; `traits.phr`, `traits.generic` |
| `3756d61` | M4.1, M4.3, M4.4: `(trait …)` and `(impl …)` in Ovis Core; a trait is a type; calls on a record call its impl, on a trait value dispatch through a table indexed by the record's hidden tag; mixed lists; untyped values get the trait whose phrases are used on them |
| `63809d4` | M4.2: `a NAME can:` and `a NAME is a TRAIT:` in phrases (phrases known before impls and across files), `examples/shapes.ovis` |
| (M5) | One-line phrase bodies: `to double (n): give back n * 2`, `area of (it): give back …`, split by the lexer (`split_one_line_blocks`) into a header and its one body line; a list of one record is already a list of its trait (test added) |
| (M5) | Mixed lists of records with several common traits: the list takes the first trait by name; the checker lets its items be asked for the others' phrases and the list be passed as a list of them when a record has both; the dispatch table's missing entry is a runtime error (`ovis_method_check`) |
| (P0) | D1–D3 `at the end`: `(defer …)` in Ovis Core (parser, elaborator's implicit return after trailing defers, checker rules, codegen frames copied to every exit), `at the end[,] STATEMENT` / `at the end:` / `defer` in phrases with the `Compiler:` note, `examples/notes.ovis`, `eval/programs/tidy.ovis` |
| (P0) | M2 leftovers: `nothing` and `is nothing` (a `record` slot type), defaults of later records, a field given once; M3 leftovers: hint for a missing `comes before` / `is the same as`, `position of` / `count of` over `equatable`, lists of records shown through `text of`, `a book is comparable:` |
| (P1) | `proposals/memory.md` accepted: explicit, strict memory (`free`, `with new memory:` arenas, `keep`, lost/stale value checks, checked mode; plan E1–E7), replacing a first draft that recommended Boehm GC. Measured the leak: 10 million passes joining text peak at 783 MB |
| (CI) | macOS: build fixes for libc++ (the daemon's `exchange` helper renamed `ask_daemon` so `std::exchange` is not picked through a `path` argument; file times cast from `__int128`; `<unistd.h>` in `daemon_test.cpp`; reals in S-expressions read with `strtod`, since Apple libc++ has no floating-point `from_chars`), and a macOS 15 job in `ci.yml` |
| (P0) | Record construction in English: `with the age of 12`, `, and with`, `named`/`called` for `name`; an unclear line starting with a record's name is offered `a NAME with F V, …` (`a person namely "Winnie", aged 12` settles by ranking) |
