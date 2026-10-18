# First phrase-question measurements (four questions)

Archived from state.md → "Phrase front end: first measurements" on 2026-10-18 (dream at `a2d5a67`). Replaced by the 26-question measurements of `ovis_eval questions`.

`choose` with real Qwen2.5-Coder models (Q4_K_M, 4 CPU cores), on four
hand-written questions (mean→average, cost→price, add up→sum, "it"→the
list; the first asked twice), after the model was loaded:

| Model | Per question | Right | Right and confident (p ≥ 0.6, margin ≥ 0.3) |
| --- | --- | --- | --- |
| 1.5B | ~250 ms | 4 of 4 | 0 of 4 (best p 0.39–0.55) |
| 3B | ~500 ms | 4 of 4 | 4 of 4 (p 0.86–0.97) |

The repeated question gave identical probabilities, reusing the
system-message batch. These were one-off runs, not a test, and four
questions only hint at M0.3: 3B looks like the smallest model that settles
questions on its own; 1.5B would send them all to the author. Loading took
3 s (1.5B) to 140 s (3B, cold disk cache) and isn't counted.

