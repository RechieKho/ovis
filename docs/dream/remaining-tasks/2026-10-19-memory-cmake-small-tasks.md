# Finished tasks: small tasks, CMake, memory, lists of lists, phrase files

Archived from remaining-tasks.md → several sections on 2026-10-19 (dream at e631216).

- [x] `x = nothing` and `x != nothing` read as `is nothing` / `is not
  nothing` (a record compared with the `nothing` literal).
- [x] Making a record with a word the field doesn't have (`a person called
  "W" and aged 12`): the unclear span widens back to the record's name.
- [x] Missing phrases: `remove X from XS`, `remove item N of XS` (Core
  `remove-at`), `T becomes V`, `make T V`; `divide x by 2` of a whole
  number says the result is a decimal.
- [x] Rewrites: `ovis_eval rewrites eval/rewrite-questions.txt` (3B: 8 of 15,
  state.md). Next: run it for 7B and after each prompt change. A
  one-sentence change once turned a right 3B rewrite wrong. 3B gives some lines back
  unchanged (`counter_a's count not 0`), where 7B rewrote them. `X is V` as equality is what the
  model writes most for a test (`winnie's age is 13`); the 10 s
  `--model-budget` holds about three rewrites on a CPU.
- [x] The daemon is waited for ten minutes (the 7B took 320 s once) and the
  fallback says it was still loading.
- [x] Candidates are capped at three questions' worth (27) in library order
  among equals; it was 21 and the library's growth pushed `n is divisible
  by 3` out for `if 3 goes into n`.
- [x] Ovis Core `for-down` counts down; `the numbers from A down to B`.
  A block phrase `count down from N to M as i:` is not written.
- [x] `random` is splitmix64 seeded from the clock, process and address,
  with rejection so every value is equally likely.
- [x] Whole-number `+ - *` and negation stop the program on overflow
  (`llvm.*.with.overflow`). Division by zero was checked already.
  `INT64_MIN div -1` is not checked.
- [x] `-DOVIS_PORTABLE=ON` (CI and the embedded-model workflow use it):
  `GGML_NATIVE` off, AVX/AVX2/FMA/F16C on for x86-64, NEON on ARM. Source
  builds keep NATIVE on.
- [x] Determinism holds only on the same machine and build: documented
  (architecture-spec §10, README).
- [x] Text: split, find, replace, case and number parsing existed;
  `is a whole number` / `is a number` check text before converting it.
- [x] Lists of lists: Core types (`value_type::inner_lists`), codegen,
  printing, `keep`/`copy`, and phrases (`list of lists of whole numbers`).
- [x] Ownership of memory C functions return: the author's, freed with
  `free` ([proposals/memory.md](proposals/memory.md) §7; c-libraries.md).
- [x] **Building with CMake** ([proposals/cmake.md](proposals/cmake.md)):
  K1–K6 are built (`--object`, `--link-by-caller`, `@FILE`, `--locked`,
  `--depfile`, `ovis runtime`, `find_package(Ovis)`, `ovis_add_executable`,
  `examples/cmake` in CI, docs in c-libraries.md). Not run on macOS or with
  a shared C library yet.
- [x] Phrase files loaded at run time (`phrases/` beside a folder program's
  `main.ovis`, or `--phrases DIR`): `.phr`, `.core`, `.generic`; hashes in
  the lockfile with a warning on an edit; errors at `file:line`; a phrase
  worded like a built-in is refused; `ovis_eval questions --with FOLDER`
  reads them. Not done: `ovis phrases` doesn't list them.
- [x] `ovis check-phrases PROGRAM`: the phrase self-test in `ovis`, with
  `eval/questions.txt` embedded (`phrase_check.cpp`; `ovis_eval questions
  --with` uses it too). Not done: the wording and hard sets aren't embedded.
  - [x] Q6 `ovis lsp`, Q7 VS Code extension (dims sigils; folding a line
    away isn't possible in VS Code). Not yet tried in VS Code itself: the
    owner should install `editors/vscode` and report.
  - [x] Q4 closest phrases when nothing fits; `ovis phrases WORDS`.
  - [x] Q5 `ovis fmt` (re-render sigils, migrate old `Read as:` lines).
- [x] A meaning the lockfile doesn't know warns (not fails) when it loses a
  name or number the line has (the rewrites' check).
- [x] `test/test.cpp` is split (`core_test`, `model_test`, `docs_test`,
  `c_import_test`, `modules_test`; helpers in `support`).
- [x] macOS in CI: build, `test_ovis` and offline examples (`ci.yml`).
- [x] macOS: system headers are looked for in the SDK
  (`system_include_directories`, `xcrun --show-sdk-path`); the
  real-C-library test uses the same list. Not run on a Mac yet.
