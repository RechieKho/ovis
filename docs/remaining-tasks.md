# Remaining tasks

Ordered by priority. Current status is in [state.md](state.md), and the
design these tasks change is in [architecture-spec.md](architecture-spec.md).
Move a task to state.md's history when it is done.

## P0: make it work with a real model

Nothing below matters until a real model is known to work.

- [ ] **Stop instructions being taken as notes** (see state.md's trial).
  At least warn when a program does nothing, or when a note contains a
  command verb.
- [ ] **Reject translations that copy a few-shot example**: e.g. require
  every text literal in the code to appear in the steps.
- [ ] **Measure GPU speed** on a Mac (Metal) and check determinism there.
- [ ] **Evaluate the detail pass** on the examples and on about 20 new
  paragraphs: clear ones, vague ones, irrelevant ones and notes. Check:
  - Are the steps concrete?
  - Are the verdicts right?
  - Does it over-refuse?
- [ ] **Evaluate translation** from steps to Ovis Core. Measure:
  - how often the first attempt passes the checker;
  - how many repair rounds it takes;
  - which errors come up most often.
- [ ] **Evaluate C library use:** does the detail pass write "Use the C
  library … (header …)", and does the relevance ranking show the model the
  right functions?
- [ ] **Evaluate the review**, against edited steps that are fine and edited
  steps that drop a key step.
- [ ] **Tune the prompts and few-shot examples** from what the evaluations
  show. Re-check the context budget, since prompts are about 1.5–2.5k
  tokens against a context of 8192.
- [ ] **Measure speed per paragraph** with the 1.5B and 0.5B models. If it
  is slow:
  - cache the KV of the fixed system prompt and examples across runs;
  - consider a smaller default model.
- [ ] **Replace the hand-written example lockfiles** with real model output,
  or keep both and label them clearly.

## P1: correctness and robustness

- [ ] Indented lines in the author's own prose are taken as the start of a
  detail block. Either define this as a rule in the docs or require the
  block to look like steps.
- [ ] Origin matching needs half of the steps to be identical. A heavily
  rewritten block is treated as written by the author and gets reviewed
  instead of revised. Consider fuzzy matching per step.
- [ ] The first translation uses the generic grammar, so it can call made-up
  functions. Consider restricting it to known names plus names that appear
  in any paragraph's steps.
- [ ] Checker messages cite Ovis Core line numbers ("line 3: …"). They are
  fine for the model, but confusing if shown to the author through
  `--verbose`.
- [ ] Memory: lists and text are never freed. That is fine for short
  programs, but long loops leak.
- [ ] `for` only counts up. Add a step or downward counting, or make the
  detail pass say "count down with while".
- [ ] `random` uses `rand() % n`, which is biased and has a weak seed.
- [ ] Integer overflow is unchecked. Integer division by zero is checked.
- [ ] llama.cpp is built with `GGML_NATIVE` on, so binaries may not run on
  other CPUs. Turn it off for release builds.
- [ ] Determinism holds only on the same machine and build. Document this
  in the lockfile, or store the fingerprint per entry.

## P2: language features

- [ ] Records/structs and maps (dictionaries).
- [ ] Text as a first-class value beyond what exists: split, find,
  replace, upper and lower case, and number parsing without libc.
- [ ] Lists of lists.
- [ ] Callbacks into C (e.g. `qsort`, `sqlite3_exec` with an Ovis
  function). Today a callback parameter can only be given `null`.
- [ ] C structs: pass and return by value, read fields. Today these
  functions are skipped on import.
- [ ] C function-like macros, and declarations in headers that the imported
  header includes (only the header itself is read today).
- [ ] Ownership of memory C functions return (free, or document leaks).
- [ ] Static linking when a `.a` exists, as an option.

## P2: model and tooling

- [ ] Fine-tune a tiny model on (paragraph → steps) and (steps → Ovis Core)
  pairs harvested from lockfiles. Embed it by default if it is small enough.
- [ ] A clarifying-question mode: instead of failing with `too_vague`, ask
  the author a question and record the answer in the source.
- [ ] `ovis fmt`: re-render detail blocks without building.
- [ ] Editor support: highlight detail blocks, jump from errors.
- [ ] Split `test/test.cpp` (about 770 lines) by area.
- [ ] Windows support and testing, then add it to CI.
- [ ] macOS: builds and tests pass with LLVM 22. The real-C-library test
  should find zlib/SQLite headers in the SDK (`xcrun --show-sdk-path`),
  not only `/usr/include`. Then add macOS to CI.
