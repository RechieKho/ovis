# Dropped: tasks of the LLM-writes-the-program pipeline

Archived from `docs/remaining-tasks.md` → "P0: make it work with a real
model", "P1: correctness and robustness", "P2: language features" and
"P2: model and tooling" on 2026-10-18.

Dropped because the project pivoted to the phrase grammar
(`docs/proposals/phrase-grammar.md`, `docs/proposals/types.md`): the model
no longer writes steps or Ovis Core, so the detail pass, translation,
repair and explanation tasks no longer apply. Tasks that still apply were
kept, reworded, in the new plan: GPU speed and default model size (M0),
records (M2), the clarifying-question mode (built into M1 as questions).

## P0: make it work with a real model

Nothing below matters until a real model is known to work.

- [ ] **Better steps from the detail pass** (verdicts are now 22 of 23):
  no work the paragraph didn't ask for, decimals stay decimals, counting
  down. Extend `ovis_eval` to check steps, not only verdicts.
- [ ] **Decide how to detail pure code paragraphs.** The model paraphrases
  exact code into poor steps. Options: skip the model for paragraphs that
  are all code (Compiler: notes still show inferred types), or keep it.
- [ ] Warn when a whole program compiles to nothing (every paragraph a
  note), in case a command is phrased without a leading verb.
- [ ] **Reject translations that copy a few-shot example**: e.g. require
  every text literal in the code to appear in the steps.
- [ ] **Measure GPU speed** on a Mac (Metal) and check determinism there.
- [ ] **Choose the default model size.** With the copy guards, 7B got 3 of 5
  programs right and 1.5B 1 of 5 (state.md). 7B is 4.7 GB and about 3x
  slower; measure it on a GPU.
- [ ] Judge trial output by meaning, not byte-equality: 7B's primes was
  right but printed an extra "true".
- [ ] Failure explanations talk about code ("uses a language (fn, return)
  that is not recognized") though the prompt says not to.
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

- [ ] Origin matching needs half of the steps to be identical. A heavily
  rewritten block is treated as written by the author and gets reviewed
  instead of revised. Consider fuzzy matching per step.
- [ ] The first translation uses the generic grammar, so it can call made-up
  functions. Consider restricting it to known names plus names that appear
  in any paragraph's steps.

## P2: language features

- [ ] Records/structs and maps (dictionaries).

## P2: model and tooling

- [ ] Fine-tune a tiny model on (paragraph → steps) and (steps → Ovis Core)
  pairs harvested from lockfiles. Embed it by default if it is small enough.
- [ ] A clarifying-question mode: instead of failing with `too_vague`, ask
  the author a question and record the answer in the source.
