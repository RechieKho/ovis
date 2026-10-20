# Model backends

Part of [architecture-spec.md](architecture-spec.md) (§9), kept in its own
file for length: how the model is run, cached, served and found. It
describes what is built; the same rules apply as for the spec.

`language_model::choose(messages, n)` gives the probability of each option
`1`..`n` (at most 10, one digit token each: `1`–`9`, then `0`): what
questions use. Its default asks `chat(messages, grammar)` under the grammar
`[1-n]` (`[0-9]` for 10) and is certain of the answer. `embed(texts)` gives unit
vectors (empty if unsupported). There are four implementations.
- **`builtin_model`**: llama.cpp `b11200`, statically linked.
  - Every layer runs on the first GPU llama.cpp finds (Metal on macOS;
    CUDA or Vulkan if llama.cpp is built with them), else on the CPU.
    `--cpu` forces the CPU. Reproducibility comes from `@[meaning: …]` lines
    and the lockfile, not the device; on a Mac (Metal, 1.5B) the
    probabilities differ from the CPU's by at most 0.02 and every decision
    agreed.
  - Reports where it loaded (device, or CPU and threads) through the
    progress log.
  - Loads lazily, so lockfile-only builds never load it. Weights are
    memory-mapped and not repacked for the CPU's vector units
    (`use_extra_bufts = false`): repacking 3B took ~7 s per load, and a
    build asks few questions, each ~0.3 s slower without it.
  - Greedy decoding. The grammar is checked on the top token first, and the
    whole vocabulary is scanned only if that token is refused; the result
    is identical either way.
  - Prompt KV is reused only in whole batches, which start only at
    multiples of `n_batch` (512), at the end of the system message, and
    before the last line starting `1. ` (a question's options). Generated
    tokens are never reused, so a reply never depends on earlier requests.
    The second start lets questions share a short system message; the
    third lets a run-off's questions share their line, names and question,
    so its later questions decode only their options (about 40% fewer
    tokens).
  - **Prompt cache:** the state after a system message (once read whole, in
    one batch or several) is saved in
    `~/.cache/ovis/prompts/<hash>.state` (`llama_state_seq_save_file`,
    written to `.part` and renamed) and loaded by later processes. The hash
    covers the llama.cpp version, the weights' fingerprint, the device, the
    threads, the CPU features and the tokens, so a loaded state is what
    computing it would give.
  - `choose`: one decode of the prompt, then a softmax over the logits of
    the option tokens. Nothing is generated.
  - Threads: `--threads`, or the performance cores (macOS) or all cores. Context 8192, up to 1024 tokens
    per reply (`chat`).
  - `embed`: last-token hidden states, normalised, from a second context
    (`LLAMA_POOLING_TYPE_LAST`, all texts in one decode) so the chat's
    cached prompt stays.
- **`daemon_model`** (`model_daemon.cpp`): what `ovis build`/`run` use, so
  loading is paid once per session. `ovis model serve` holds a
  `builtin_model` and answers `choose`, `chat` and `embed` on a Unix socket
  (one request per connection, length-prefixed fields, numbers as bytes)
  until `--daemon-idle` minutes unused (15) or `ovis model stop`. The socket,
  in `$XDG_RUNTIME_DIR/ovis/` or `/tmp/ovis-<uid>/` (0700 and owned, else no
  daemon), is named by a hash of `ovis`, the weights and every setting
  answers depend on. The first request finding none starts it (file lock,
  double fork, `ready` on a pipe; stderr to `<socket>.log`). Any failure
  loads the model in-process, said once; `--no-daemon`/`OVIS_NO_DAEMON=1`
  skip it. Name and fingerprint come from the file and answers are
  bit-identical to in-process (`[model]` test), so lockfiles don't depend
  on which process answered.
- **`endpoint_model`**: an OpenAI-compatible `/chat/completions` server. It
  sends `grammar` (a llama-server extension) unless `--no-grammar` is given.
  `choose` asks for one token with `top_logprobs` 20 and no grammar, and
  falls back to the default when no option is among them.
- **`missing_model`** (in `main.cpp`): stands in when no weights are found.
  It explains how to get a model only when a question actually needs one.

**Weights lookup order:**
1. `--model-path`
2. `$OVIS_MODEL_PATH`
3. weights embedded in the executable
4. a default model (`default_models()`) in `<exe dir>/`,
   `<exe dir>/../share/ovis/` or `$XDG_CACHE_HOME/ovis/models/`
   (`~/.cache/ovis/models/`): Qwen2.5-Coder-7B-Instruct Q4_K_M first when
   `has_gpu()` and no `--cpu`, else 3B first, then the other. 7B reads hard
   wording much better but takes ~2.3× as long per question, which only a
   GPU makes small. `ovis model pull [3b|7b]` downloads the same choice.

**Embedding format:** the executable, padding to a 4096-byte boundary, the
GGUF, then a 32-byte trailer:
- offset (u64, little-endian)
- size (u64, little-endian)
- `OVIS-EMBEDDED-1\0`

It is loaded with `llama_model_load_from_file_ptr` at that offset.
Re-embedding replaces the old weights.
