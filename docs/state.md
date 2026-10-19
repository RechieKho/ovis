# Project state

Last updated: 2026-10-19 (explicit memory E1–E7, building with CMake K1–K6, small tasks)
(branch `claude/english-llvm-frontend-pso2fc`).
Last dreamed: 2026-10-19 at 4bea802 (records and traits moved from architecture-spec.md into docs/records-and-traits.md).
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
| Phrase files read at run time: `phrases/` beside a folder program's `main.ovis`, or `--phrases DIR` (`.phr`, `.core`, `.generic`); `ovis check-phrases PROGRAM` | Built, tested (`program_test`, `fix_test`) |
| Phrase library (112 templates, prelude, generic functions) | Built; every template run by a test program |
| `at the end` / `defer` (D1–D3): `(defer …)` in Ovis Core, copied to every exit of its block; the surface forms with the `@[note: …]` sigil | Built, tested (`defer_test`, `examples/notes.ovis`, `eval/programs/tidy.ovis`) |
| Built-in traits: slot types `showable`, `comparable`, `equatable`, `T:trait`; a record implements one with `to (a: book) comes before (b: book):`, `is the same as`, `text of`, or `a book is comparable:` (equatable, showable); `nothing`; lists of records with `text of` show as `[a, b]` | Built, tested (`record_phrase_test`, `phrase_library.txt`); static dispatch |
| User traits: `a NAME can:` (required and default phrases, `it` the value), `a NAME is a TRAIT:`; a trait is a type (`(s: shape)`, `list of shape`, mixed lists), phrases on it dispatch when the program runs; untyped values used only through one trait's phrases get that trait | Built, tested (`trait_test`, `trait_phrase_test`, `examples/shapes.ovis`) |
| Questions: rules, ranking shortcut, `choose`, rewrites, `@[meaning: …]` sigils, lockfile | Built; tested with scripted models; measured with 0.5B–7B (M0, archived; M1.14 below) |
| Deterministic elaboration, checker, LLVM codegen, C runtime | Built, tested by compiling and running programs |
| Records in Ovis Core (`record`, `new`, `field`, `set-field`) | Built, tested (`record_test`) |
| Records in phrases: `a NAME has:`, `a NAME with …`, fields read and set 3 ways, `field of T` slots, questions about fields and `the NAME` | Built, tested (`record_phrase_test`, `phrase_library.txt`, `examples/library.ovis`); measured with 3B (eval programs, below) |
| Explicit memory: `with new memory:`, `new memory`, `with M:`, `clear`, `free`, `keep`; checked builds (`--fast` off, `--report-leaks`); strict rules (on; `--lax-memory` off) | Built, tested (`memory_test`, `checked_test`, `memory_check_test`, `phrase_library.txt`); the examples follow them; `examples/visits.ovis` has a named memory and `clear` |
| Building with CMake: `--object`, `--link-by-caller`, `@FILE`, `--locked`, `--depfile`, `ovis runtime`, `find_package(Ovis)`, `ovis_add_executable` | Built, tested (`cmake_test`, `examples/cmake`, CI) |
| C standard library, any C library via `use "x.h"` (libclang, `ref x`, pkg-config) | Built, tested with a test library, real zlib and SQLite |
| Multi-file programs, nested modules, helper-file rule | Built, tested (`examples/shop`) |
| Built-in llama.cpp model, embedding, lookup, `ovis model pull` (3B) | Built; determinism tested with a tiny random model |
| Model daemon (`ovis model serve`, started by builds, in-process fallback) | Built; tested with a scripted model and, `[model]`, started by `ovis` |
| GPU (Metal etc.) | On by default, `--cpu` to turn off. Measured by the owner on a Mac (M0.5, below) |

## Verified

- `./build/test_ovis`: 129 test cases and 1556 assertions pass. Three cases are skipped unless
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

llama.cpp is built with `-DOVIS_PORTABLE=ON` in CI (`GGML_NATIVE` off, AVX2/FMA/F16C on).

A second job in `ci.yml` runs on macOS 15 (arm64): Apple clang, linked
against Homebrew's LLVM (22 at the time of writing). It builds, runs
`test_ovis` and checks the examples offline. The real-C-library test skips
there (headers are in the SDK, not `/usr/include`), and the job skips the
`[model]` tests and the question ranking, which the Ubuntu job covers.

`.github/workflows/embedded-model.yml` is started by hand (3B by default,
1.5B or 0.5B). It builds `ovis` with the model embedded, publishes it as an
artifact for 14 days, and reports `ovis_eval questions` and `ovis_eval
programs` in the job summary. These steps have not run on GitHub yet.

## Memory measured

`tools/peak_memory.py build/ovis eval/memory/*.ovis` (4 CPUs, peak
resident MB / seconds). Before E2 (E1's baseline), then now unchecked
(`--fast`) and checked (the default):

| Program | Before | `--fast` | Checked |
| --- | --- | --- | --- |
| `join_loop` (2M passes joining text, never freed) | 154.2 / 0.38 | 62.7 / 0.20 | 321.6 / 1.09 |
| `churn` (20000 lists of 50 records) | 54.1 / 0.08 | 54.6 / 0.07 | 172.1 / 0.43 |
| `tree` (20000 parent-linked trees of 31 nodes) | 81.6 / 0.12 | 54.4 / 0.09 | 334.3 / 0.72 |
| `join_loop_in_memory` (the loop with `with new memory:`) | — | 11.9 / 0.51 | 33.0 / 0.55 |
| `join_loop_freed` (the loop with `free label`) | — | 12.1 / 0.21 | 12.0 / 0.32 |

One join is one allocation now (`ovis_join`), which is why `join_loop`
shrank. Checked mode is 3–6× in time and memory for programs that make
many values (the table keeps an entry per live value); with `with new
memory:` or `free` in the loop the table stays small and memory is flat.
Making and freeing an arena each pass costs extra time (0.51 s against
0.21 s for 2M passes). `--strict-memory` (now the default) on the examples:
10 lines fail, all of two kinds: `for each i in the numbers from 1 to N`
and `show X with 2 decimals` / `show TEXT + …` (a list or text made inside
another statement); see remaining-tasks.md.

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

## Rewrites held-out set

`ovis_eval rewrites eval/rewrite-questions.txt` (15 lines no reading fits,
judged by what the program prints; `expect: none` is a refusal), 3B on 4 CPUs,
2026-10-19: **8 of 15 right**, 7 lines rewritten, the one refusal kept, and
every miss a refusal (no wrong rewrite was kept). Right: `double`, `has a
birthday`, `is a teenager`, `how old winnie is`, `gets one year older`, `is
older than 10`, `the age of winnie plus 1`. Refused: `in months`, `turns 13`,
`the names of`, `increase winnie's age`, `ages of people`, `the oldest
person`, `add one to`. A case takes 2–47 s (median ~13 s). Judge a prompt
change against this, not a few lines.

## Edit candidates measured

`eval/wording-questions.txt`: 28 near misses (a word left out, an extra
joining word, a word spelt like the phrase's, built-in forms, fields).
3B, one order, before → after edit candidates: right 14 → 22 of 28 (every
confident pick right); `questions.txt` 24 → 24 of 26; `hard-questions.txt`
12 → 12 of 20. Ranking (`--no-model`): near misses first 13 → 21, shown
15 → 23. Tried and dropped: replacing a word by any phrase word (`median`
became `average` confidently), a single question of 9 (cut right readings
the run-off kept), dropping words with meaning (`w's age in months` read
as `w's age` at 86%). Two edits (a verb put first, the joining words
before the value made `to`/`by`): wording ranking first 21 → 23, shown
23 → 25 of 28 (`w's age is now 13`, `goes to 14`); the other sets
unchanged; CI floor raised. Left to rewrites: word order, `sort out`,
`the order of`.

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
| 2026-10-19 | **Copying is explicit, as in Rust:** text, lists and records are shared by `=`, arguments and items, never copied behind the author's back; `a copy of x` is the one copy, and deep (shares nothing; cycles survive as with `keep`). No moves or borrowing. [proposals/references.md](proposals/references.md) §10. |
| 2026-10-19 | **References as in C:** `ref x` of a variable, field or item is a typed pointer, an ordinary value (giving back a local's address is a build error). No marker for changed parameters: a phrase changes a number through a pointer; dropping a phrase's result gets a warning. references.md §6, §9. |
| 2026-10-19 | **Pointers as Zig's kinds:** `pointer to a T`, `pointer to many T`, `array of T` (a slice), text, `T or nothing`, `C pointer to T` (headers'), `pointer` (`void *`). Null only in `or nothing` types and C pointers: records stop being null by default (a migration). references.md §3–§4. |
| 2026-10-19 | references.md accepted: C structs imported as C records; structs by value through a C wrapper per function; raw pointer phrases everywhere; Ovis phrases as C callbacks (no captures); headers: the library's and the types its functions reach. Plan R1–R8. |
| 2026-10-19 | A phrase as a value is named with `_` for its slots (`compare _ and _`) or, without slots, `the phrase say goodbye` (or `the action`, `the function`, `the method`, `the procedure`, `the routine`); bare words are always a call. Phrase values are typed `phrase (…) giving …` (Ovis's convention) and `C phrase (…) giving …` (headers'), called with `call f with …`; the type's word takes the same synonyms (`action (…) giving …`). references.md §7b. |
| 2026-10-19 | **CMake:** Ovis reads C headers with the CMake target's flags (a response file), writes an object, and CMake links it with the target (static or shared is CMake's); builds under CMake are `--locked` (no model, no writes). [proposals/cmake.md](proposals/cmake.md), K1–K6. Linking by itself stays for `ovis run`/`build`. |
| 2026-10-19 | **Memory, as built:** the owner chose option (a), the proposal as written: the author stores every new value first. `ovis` enforces it unless `--lax-memory`; the library API's `strict_memory` is off for the tests. Core `free` is one builtin form that picks the runtime call by type, so a program that `use`s C's `free` still works. Programs are checked by default (`--fast` turns it off; CMake's `OVIS_FAST`). Strict analysis messages cite the source line; checked-mode messages name lines, not variables. |
| 2026-10-19 | **CMake, as built:** a locked build also needs the lockfile that records the author's `@[meaning: …]` lines and the C headers' hashes (one `ovis run` writes it). The runtime is C source (`ovis runtime`) that CMake builds with the project's flags as `ovis::runtime`. |
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
| (P0) | Small tasks: `x = nothing`; `remove X from XS`, `remove item N of XS` (Core `remove-at`); `T becomes V`, `make T V`; whole-number overflow is an error; `random` unbiased; Core `for-down`; `is a number`; the question cap is 27; a record span widens back to its name; `divide x by 2` hint; the daemon waits ten minutes; a lost-name warning for a meaning the lockfile doesn't know; `OVIS_PORTABLE`; macOS SDK headers; `test/test.cpp` split |
| (P0) | **CMake K1–K6**: `ovis build --object FILE --link-by-caller --locked --depfile FILE @RSP`, `ovis runtime`, `find_package(Ovis)` (`OvisConfig.cmake`, `OvisMacros.cmake`, installed with `cmake --install BUILD --component ovis`), `ovis_add_executable(NAME PROGRAM [LINK …])`, `examples/cmake`, CI on Linux and macOS |
| (P0) | **Memory E1–E7**: runtime memories (program malloc or arenas, a stack), Core `new-memory with-memory with-new-memory clear-memory free keep`, string constants in the `ovis_const` section, `ovis_join`, the phrases of `phrases/memory.phr`, checked builds (value table, lines, poison and delayed free, `--fast`, `--report-leaks`), `check_memory` behind `--strict-memory`; `eval/memory`, `tools/peak_memory.py` |
| (P0) | Lists of lists (`value_type::inner_lists`; Core, codegen, printing, `keep`/`copy`; phrases `list of lists of whole numbers`); `a copy of x` (deep, Core `copy`); the dropped-result warning (`core::warn`); phrase files at run time (`phrases/` beside `main.ovis`, `--phrases DIR`, hashes in the lockfile); `ovis check-phrases PROGRAM`; user-phrase parameters used as numbers take number slots; `ovis_eval rewrites` with `eval/rewrite-questions.txt` |
| (P0) | Terminal progress: a live status line (spinner; reading FILE, line N of M; asking the model what "…" means; rewriting; checking; generating, linking; elapsed; "loading the model"), dimmed event lines, a summary in words (`✓ 5 lines read · 3 unclear: 1 by a rule, …`); problems a prompt covers aren't printed twice. Pipes, CI, `--quiet`, `--verbose` unchanged |
| (CI) | macOS build: `<algorithm>`/`<iterator>` included where ranges algorithms are used (`format.cpp` failed on libc++) |
| (CI) | LLVM 23 (macOS): `is_terminated` reads the block's last instruction, not `getTerminator()`, which no longer gives null (40 tests failed there) |
| (P0) | `has_gpu` sets llama.cpp's log first, so the Metal backend's start-up lines show only with `--verbose` (they printed on every `ovis run` on a Mac) |
| (P0) | Traits: a number or text given a trait is refused with a record that holds it suggested; a trait's phrase without `(it)` (`(a) comes before (b)`: untyped parameters are the value, Core `(b same)`, checked same record when dispatched, `ovis_same_record_check`); `comes before`/`is the same as`/`text of` in a trait make its records and values comparable/equatable/showable (`comes_before__shape`); two records meeting in one slot become their common trait (`[c1, a square …]` was a list of circles); `OVIS_DUMP_CORE` prints the front end's Core |
| (P0) | Traits that require traits: `is comparable and a thing` in `a shape can:` (Core `(requires thing)`; a built-in one is its phrase, which a record may write outside the impl); every record of the trait must be the others too |
| (P0) | Lists of records sharing several traits: a phrase of the second trait on an item, or the list given as a list of it, is a build error where the list's records are known (a list written out, or a variable given one; forgotten once a phrase or call may change it), else checked when it runs; a type variable takes the trait for a record of it (`append c to shapes`) |
| (P0) | **M4.5**: a trait phrase only one record answers is called directly (its tag checked first, since a value of another trait may reach it), so LLVM can inline it |
| (P0) | Edit candidates with two edits: a built-in update's verb first and the joining words before the value made `to`/`by` (`w's age is now 13` is `set w's age to 13`); wording CI floor 23/25 |
| (P0) | `a buffer of N characters` (`room for N bytes`): Core `buffer` in phrases, for C functions that fill text (`fgets`); `examples/notes.ovis` reads lines with it; names first set in a block inside `at the end:` stay in it |
| (P1) | User phrases: an untyped parameter used only where the library's text phrases read it (`p in capitals`, `words of p`, `p starts with`) is a text slot, only where its list phrases do (`for each x in p`, `item 2 of p`, `sort p`) a list slot, `p joined with` a list of text (`use_of`); `--with examples/shop` 0 worse |
| (P1) | `examples/visits.ovis`: a named memory cleared each pass. A global list no function uses no longer starts as an empty list (it leaked, reported as made on line 0) |
| (P1) | Memory: checked builds check text, lists and records given to C (`ovis_live`: `length of` a freed text was C's `strlen` on freed memory) and name the variable a value was last stored in (`ovis_name`: "the text in 'line' was freed on line 3"); the strict pass follows a named memory's values (stale after `clear`/`free` of it) and a list's items to the memory they were made in |
| (P1) | Memory, migrated: `eval/programs` follow the strict rules (all but `vowels.ovis`, whose unclear span nests text whichever way it reads); the prelude's text phrases build in one buffer (`set-char`, `prelude_put_text`, `prelude_matches_at`): linear and leak-free (joining, capitals, reversing, replacing, splitting made and lost a text per character); `text of` a list of records joins in a memory of its own; a test runs the strict pass over the prelude and generic copies. `for each i in the numbers from A to B` is a counting loop (no list); a number `as text` inside a join is part of it |
