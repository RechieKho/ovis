# Phrase front end: questions measured (M0)

Archived from state.md → "Phrase front end: questions measured" on 2026-10-18 (dream at 9d57501).

`ovis_eval questions eval/questions.txt`: 26 one-line programs with one
unclear span each and the reading a person means (2 of them: none fits).
Qwen2.5-Coder Q4_K_M, `--cpu`, 4 cores of an Intel Xeon @ 2.8 GHz, 8
readings plus "none of these" per question, so one question per case:

| Model | Right | Confident (p ≥ 0.6, margin ≥ 0.3) | Of those right | Best guess right | Per question |
| --- | --- | --- | --- | --- | --- |
| 0.5B | 2 | 0 | – | 1 | ~0.5 s |
| 1.5B | 7 | 5 | 5 | 18 | ~0.9 s |
| 3B | 23 | 21 | 21 | 23 | ~1.8 s |
| 7B | 25 | 23 | 23 | 25 | ~4.2 s |

- **Ranking (M0.4):** the right reading is first in 13 of 24 and among
  the 8 shown in 24 of 24, so embeddings aren't needed for now. Lexical
  ranking can't tell `mean`, `biggest` or `flip` from the other one-list
  phrases; aliases cover what M1.14 shows is common.
- **8 readings per question, not 7:** a list has 8 one-list readings, so
  with 7 every such question needed a run-off (3 asks). With 8: 26 asks
  instead of 54, and 3B got one more right (23, was 22, with 2 of 23
  confident picks wrong).
- **Thresholds:** 1.5B is rarely sure, but right when it is: at p ≥ 0.4
  and margin ≥ 0.1 it picks 12, all right; 3B picks 24, 22 right. M1.14
  tunes them.
- **M0.3:** 0.5B guesses at random. 1.5B is right when sure,
  but sends 21 of 26 to the author. 3B settles 21 of 26, all right, and is
  the smallest that meets the goal of few questions. Decided: 3B becomes
  the default with M1.13.
- Loading is not counted (1.5B ~3 s, 3B ~9 s warm, up to 140 s cold).

Since then (M1.9 library, M1.14) the ranking shortcut only takes a reading
that covers every content word of the span, after it read "is below zero"
as `is zero`: on this set it settles 1 case, not 6.
