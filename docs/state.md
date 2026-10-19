# Project state

Last updated: 2026-10-19 (rewrites, sigils, edit candidates, quick fixes Q1–Q7)
(branch `claude/english-llvm-frontend-pso2fc`).
Last dreamed: 2026-10-19 at 53ad22a (history to the macOS CI job and the M1.14 program measurements archived). Archived (inactive) entries are in
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
| `at the end` / `defer` (D1–D3): `(defer …)` in Ovis Core, copied to every exit of its block; the surface forms with the `@[note: …]` sigil | Built, tested (`defer_test`, `examples/notes.ovis`, `eval/programs/tidy.ovis`) |
| Built-in traits: slot types `showable`, `comparable`, `equatable`, `T:trait`; a record implements one with `to (a: book) comes before (b: book):`, `is the same as`, `text of`, or `a book is comparable:` (equatable, showable); `nothing`; lists of records with `text of` show as `[a, b]` | Built, tested (`record_phrase_test`, `phrase_library.txt`); static dispatch |
| User traits: `a NAME can:` (required and default phrases, `it` the value), `a NAME is a TRAIT:`; a trait is a type (`(s: shape)`, `list of shape`, mixed lists), phrases on it dispatch when the program runs; untyped values used only through one trait's phrases get that trait | Built, tested (`trait_test`, `trait_phrase_test`, `examples/shapes.ovis`) |
| Questions: rules, ranking shortcut, `choose`, rewrites, `@[meaning: …]` sigils, lockfile | Built; tested with scripted models; measured with 0.5B–7B (M0, archived; M1.14 below) |
| Deterministic elaboration, checker, LLVM codegen, C runtime | Built, tested by compiling and running programs |
| Records in Ovis Core (`record`, `new`, `field`, `set-field`) | Built, tested (`record_test`) |
| Records in phrases: `a NAME has:`, `a NAME with …`, fields read and set 3 ways, `field of T` slots, questions about fields and `the NAME` | Built, tested (`record_phrase_test`, `phrase_library.txt`, `examples/library.ovis`); measured with 3B (eval programs, below) |
| C standard library, any C library via `use "x.h"` (libclang, `ref x`, pkg-config) | Built, tested with a test library, real zlib and SQLite |
| Multi-file programs, nested modules, helper-file rule | Built, tested (`examples/shop`) |
| Built-in llama.cpp model, embedding, lookup, `ovis model pull` (3B) | Built; determinism tested with a tiny random model |
| Model daemon (`ovis model serve`, started by builds, in-process fallback) | Built; tested with a scripted model and, `[model]`, started by `ovis` |
| GPU (Metal etc.) | On by default, `--cpu` to turn off. Measured by the owner on a Mac (M0.5, below) |

## Verified

- `./build/test_ovis`: 97 test cases and 1285 assertions pass. Three cases are skipped unless
  `OVIS_TEST_MODEL` is set. The C-library cases skip themselves when
  libclang, zlib or SQLite headers are missing. The `[docs]` case keeps the
  tracking documents under 500 lines and checks their links.
- `OVIS_TEST_MODEL=<tiny.gguf> ./build/test_ovis "[model]"`: the built-in
  model gives the same reply and probabilities from a fresh instance,
  after an unrelated request (prefix reuse), and with embedded weights.
- Every example builds offline from its meanings and lockfile, twice,
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
  floor (`--min-first 11 --min-shown 23`), with the numbers in the job
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

Ranking now (CI's floor): the right reading first in 11 of 24 and shown in
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

## Edit candidates measured

`eval/wording-questions.txt`: 28 near misses (a word left out, an extra
joining word, a word spelt like the phrase's, built-in forms, fields).
3B, one order, before → after edit candidates: right 14 → 22 of 28 (every
confident pick right); `questions.txt` 24 → 24 of 26; `hard-questions.txt`
12 → 12 of 20. Ranking (`--no-model`): near misses first 13 → 21, shown
15 → 23. Tried and dropped: replacing a word by any phrase word (`median`
became `average` confidently), a single question of 9 (cut right readings
the run-off kept), dropping words with meaning (`w's age in months` read
as `w's age` at 86%). Left to rewrites: two edits (`w's age becomes 13`,
`make w's age 14`), word order, `sort out`, `the order of`.

## Rewrites measured

`ovis run` through the daemon, 4 CPUs, 3B (7B in brackets). A rewrite
takes 2.2–3.1 s (5.3–7 s); the first read of its system message (~1500
tokens, the phrase list) takes ~20 s (47 s) once, then its state is
loaded from the prompt cache (32 MB). Lines no reading fitted (the
records probes): 3B rewrote right `winnie's age becomes 13`, `make
winnie's age 14`, `double winnie's age`, `the_car's owner turns 13` (set
its owner's age), `give the car 2 more wheels`; refused, so reported,
`remove winnie from people`, `in months`, `is a teenager`, `names of
people`, `the oldest person`. 7B also got `is a teenager` (age between 13
and 19), `the oldest person` and `the number of wheels the_car has`; its
one wrong rewrite (`turns 13` as `increase … by 1`) was refused. No wrong
rewrite was kept by either. Asked again with the refusal's reason, 3B
wrote the same in 4 of 5, so there is one try.

## Phrase front end: programs measured

`ovis_eval programs eval/programs` (20+ programs, readings meant in
`# expect` lines): the M1.14–M2 runs are archived in
[dream/state](dream/state/INDEX.md). Not measured again since rewrites,
edits and quick fixes.

## Not verified / known gaps

- The prompt cache's states (~3 MB each) are never cleaned up.
- `ovis_eval` results come from 26 questions and 20 programs written by the
  developer: real authors will write differently.
- The front end writes `@[note: …]` only for `at the end` in a loop,
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
| 2026-10-19 | What the compiler adds to a source is a **sigil above the line**, at its indentation, never indented under it (that looked like a block): `@[meaning: LINE]`, the whole line as read, and `@[note: …]`. The lockfile records the line each meaning was for, so an edited line is read again; a meaning it doesn't know is the author's. Hiding them in the lockfile behind future tooling was weighed and not chosen. |
| 2026-10-19 | The model may also **rewrite** an unclear line in words the compiler reads (chosen by the owner now the daemon keeps it loaded), under a grammar of the program's words, kept only if the line reads and nothing named is lost. Choosing comes first. |
| 2026-10-19 | **Traits:** records only for now; numbers and text are refused with an error suggesting a record that wraps the value; boxing them with a tag waits for memory E2–E3. A trait phrase need not have `(it)`: the first parameter of the trait's type picks the impl, and two such values must be the same record (checked when it runs), so operators and `(a) comes before (b)` are allowed. Traits that require traits: yes, later. Generic traits: no (Core has no generics). Lists of records sharing several traits: keep the run-time check, and add a build-time one where the list's records are known. |
| 2026-10-19 | `x = nothing` / `x != nothing` read as `is nothing` / `is not nothing`. A meaning the lockfile doesn't know warns when it loses a name or number the line has (the rewrites' check), it doesn't fail. Determinism across machines is documented, not fingerprinted per entry: the committed lockfile makes builds the same. llama.cpp: `GGML_NATIVE` for source builds; release builds and CI pin AVX2/FMA/F16C (NEON on ARM). |
| 2026-10-18 | The model only chooses (no generative job; superseded 2026-10-19 by rewrites). GPU first: 7B where a GPU runs it, 3B otherwise; make questions cheaper. Judge model changes over several orders (`--orders`). |

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
- libc++ (macOS) doesn't bring in `<algorithm>` through other standard
  headers as libstdc++ does: `std::ranges::move` in `format.cpp` built on
  Linux and failed on macOS. Include `<algorithm>` (and `<iterator>` for
  `back_inserter`) where they are used.
- The macOS job takes Homebrew's newest LLVM, which moves on its own (18
  on Linux, 23 on macOS on 2026-10-19). LLVM 23's `getTerminator()`
  assumes the block has one and gives back its last instruction, so
  every block looked finished and none got a branch or return. Test the
  last instruction's `isTerminator()` instead.

## History

| Commit | Summary |
| --- | --- |
| … | Earlier history: [dream/state](dream/state/INDEX.md) (to the macOS CI job) |
| (P0) | Record construction in English: `with the age of 12`, `, and with`, `named`/`called` for `name`; an unclear line starting with a record's name is offered `a NAME with F V, …` (`a person namely "Winnie", aged 12` settles by ranking) |
| (P0) | Questions offer no reading that drops what a line is about (a field it names, a name read as a word the phrase lacks, `show` of values sharing no word with it); `set`/`change`/`increase`/`decrease`/`multiply`/`divide`/`add`/`subtract` on variables and fields, `set x to`, `let x be`; `is greater/less than`, `over`/`under`, `at least`/`at most`, `is equal to`, `is not`. Ranking first: 10 → 11 of 24 |
| (P0) | **Rewrites**: a line no offered reading fits is written again by the model (`chat` under a grammar of the program's words), kept if it reads and loses no name, field, record, number or text; lockfile and `Read as:` like choices. The prompt cache saves system messages longer than a batch. `ovis_eval questions` measures choosing without rewrites |
| (P0) | **Sigils**: `@[meaning: LINE]` and `@[note: …]` above a line replace `Read as:` / `Compiler:` lines under it (still read, and rewritten). A meaning is the whole physical line as read; it becomes one decision again by its common start and end with the line; the lockfile's `meanings` drop one whose line was edited. `front_end_result::decisions` for `ovis_eval`. Examples migrated |
| (P0) | A list field in `a NAME with …` takes items joined by `and` or one item, after `which are`/`who is`; the rewrite grammar allows `[` `]` and its prompt shows the list form (3B wrote `(betty, winnie)` without it) |
| (P0) | Field names of several words (`number of wheels (whole number)`): `number_of_wheels` in Ovis Core, matched longest-first in `with`, `X's F`, `F of X`, field slots (`sorted by number of wheels`), questions and rewrites; printed as written |
| (P0) | `a NAME with F set to V` / `F equal to V` / `F equals V` (3B rewrote `button a turned on` as `button a set to true`, which was refused) |
| (P0) | `a list of T` and `a new list of T` as aliases of `empty list of T` (3B gave `a list of pant` back unchanged) |
| (P0) | **Edit candidates**: an unclear span's small edits (a word put in, joining words taken out, a word spelt alike replaced), read by the parser, offered first; whole-line edits when nothing else is offered. Updates with the verb after the target. `eval/wording-questions.txt` (near misses, CI floor). Questions moved to `docs/questions.md` |
| (P0) | Variables named in 2–4 words without `of` (`fan speed = 100`, `set the top speed to …`, updates); a line naming nothing is never rewritten (3B had rewritten `show fan speed` as `show a blank line`, and it was kept) |
| (P0) | **Quick fixes Q1–Q3**: problems are `diagnostic`s (span, columns, kind, fixes with their whole-line meaning) numbered in the file as written (they were cited as read, off by the sigils written above); `ovis check [--json]`; `ovis fix` (ask, `--line N --apply K`, `--meaning`); `run`/`build` offer the fixes in a terminal |
| (P0) | **Quick fixes Q6–Q7**: `ovis lsp` (problems with their fixes on open and save, a code action per fix writing the sigil, hover with the meaning and who wrote it), VS Code extension in `editors/vscode` (starts it, highlighting, dims sigils; packaged with vsce here, not run in VS Code) |
| (P0) | **Quick fixes Q4–Q5**: reports of lines nothing was taken for end with "Phrases with these words" (`closest_templates`); `ovis phrases [WORDS]`; `ovis fmt [--check]` (sigils in order, old `Read as:`/`Compiler:` migrated, meanings equal to their line dropped; CI checks the examples) |
| (P0) | Terminal progress: a live status line (spinner; reading FILE, line N of M; asking the model what "…" means; rewriting; checking; generating, linking; elapsed; "loading the model"), dimmed event lines, a summary in words (`✓ 5 lines read · 3 unclear: 1 by a rule, …`); problems a prompt covers aren't printed twice. Pipes, CI, `--quiet`, `--verbose` unchanged |
| (CI) | macOS build: `<algorithm>`/`<iterator>` included where ranges algorithms are used (`format.cpp` failed on libc++) |
| (CI) | LLVM 23 (macOS): `is_terminated` reads the block's last instruction, not `getTerminator()`, which no longer gives null (40 tests failed there) |
| (P0) | `has_gpu` sets llama.cpp's log first, so the Metal backend's start-up lines show only with `--verbose` (they printed on every `ovis run` on a Mac) |
