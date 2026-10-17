# Project state

Last updated: 2026-10-17, at commit `8afe40d`
(branch `claude/english-llvm-frontend-pso2fc`).

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
| Built-in llama.cpp model, embedding, lookup | Built; plumbing and determinism tested with a tiny random model |
| **Quality with a real model** | **Not tested at all.** See "Blocked" below. |
| `ovis model pull` | Built, **never run** (its download host is blocked here) |

## Verified

- `./build/test_ovis`: 23 test cases and 237 assertions pass. One case is
  skipped unless `OVIS_TEST_MODEL` is set.
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
- The CLI end to end against a mock OpenAI-compatible server: detail,
  translate, write back, rebuild offline, edit a step.
- Deleting a sentence but leaving its steps through the real CLI: the steps
  are removed, with a warning.

## Not verified / known gaps

- **No real model has been run.** The prompts and few-shot examples have
  never been tested against what a real 0.5–1.5B model produces. Translation
  quality, the detail pass's judgement (vague vs. fine) and review accuracy
  are unknown.
- **Example lockfiles are hand-written** by the developer acting as the
  model (marked `"model": "hand-written reference translation"`).
- **Speed** was measured only with the tiny random model: about 25 ms/token
  on 4 cores, Release build. A real model will be slower per token.
- Linux only so far. macOS should mostly work. Windows is not supported:
  renaming the running executable, `/proc`, and the tests use `popen` with
  `printf`.

## Environment notes (cloud sessions)

- LLVM 18 needs `llvm-18-dev` (`apt-get install -y llvm-18-dev`). The base
  image has the runtime but not the headers.
- Build: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j4`.
  Debug builds of llama.cpp are about 10× slower.
- The network policy blocks `huggingface.co`, so no model can be downloaded.
  To unblock it, add `huggingface.co` under Allowed domains in the
  environment's network settings. GitHub and PyPI are reachable.
- The tiny test model:
  - `pip install numpy gguf`
  - fetch `ggml-vocab-qwen2.gguf` from llama.cpp's GitHub
  - `python tools/make_tiny_model.py <vocab> tiny.gguf`

## Pinned dependencies

| Dependency | Version | Notes |
| --- | --- | --- |
| llama.cpp | `b11200` | 2026-09-26; uses `llama_model_load_from_file_ptr`, `llama_memory_*` |
| Catch2 | v3.8.1 | |
| nlohmann/json | v3.11.3 | |
| cpp-httplib | v0.18.3 | picks up OpenSSL if present |
| LLVM | system 18 | |

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

## History

| Commit | Summary |
| --- | --- |
| `c2a8763` | English front end: paragraphs, LLM translation, Ovis Core, checker, LLVM codegen, lockfile |
| `f913137` | Elaboration pass (inference, name repair, defaults); libc support, `extern`, `link`, text functions |
| `13ce853` | Built-in llama.cpp model, restricted repair grammar, embedding, `ovis model` commands |
| `4c8d08a` | Detail pass written into the source; vague/irrelevant rejection; explanations; lockfile v2 |
| `8afe40d` | Steps can't outlive their sentence (remove / revise / review); correct line numbers after rewrite |
