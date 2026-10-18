# Architecture spec §9, model backends (summary)

Moved word for word on 2026-10-18 to bring the spec under 500 lines. The full description is [../../model-backends.md](../../model-backends.md).

## 9. Model backends

Details, the daemon's protocol and the embedding format are in
[model-backends.md](model-backends.md). In short:
- `language_model::choose(messages, n)` gives the probability of each
  option `1`..`n` (at most 10); `embed(texts)` gives unit vectors (empty if
  unsupported).
- `builtin_model`: llama.cpp `b11200`, statically linked; every layer on
  the first GPU, else the CPU (`--cpu`); greedy; prompt KV reused only in
  whole batches at fixed starts, so a reply never depends on earlier
  requests; the state after a system message cached on disk.
- `daemon_model`: what `ovis build`/`run` use; `ovis model serve` holds a
  `builtin_model` on a Unix socket; answers are bit-identical to
  in-process, so lockfiles don't depend on which process answered.
- `endpoint_model` (an OpenAI-compatible server) and `missing_model`.
- Weights: `--model-path`, `$OVIS_MODEL_PATH`, embedded, then a default
  model in the cache (7B first with a GPU, else 3B).

