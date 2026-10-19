# Phrase front end: programs measured (M1.14 to M2)

Archived from docs/state.md → "Phrase front end: programs measured (M1.14)" on 2026-10-19 (dream at `53ad22a`).

`ovis_eval programs eval/programs`: 20 programs written the way a person
would, with the readings meant in `# expect` lines. Same machine, `--cpu`,
model load not counted (3B: 8–10 s warm).

| Run | Built | Questions per program | Shortcut | Model (right) | To the author (meant) | Time per program |
| --- | --- | --- | --- | --- | --- | --- |
| 1.5B | 8 | median 1, most 3 | 3 (3) | 1 (1) | 17 (3) | median 1.9 s |
| 3B | 15 | median 1, most 3 | 3 (3) | 15 (13) | 4 (2) | median 3.8 s, most 14 s |
| 7B (load 52 s) | 15 | median 1, most 3 | 3 (3) | 14 (13) | 5 (2) | median 8.3 s, most 33 s |
| 3B, + fit filter, loop scope | 16 | median 1, most 3 | 3 (3) | 14 (14) | 4 (3) | median 3.8 s, most 11 s |
| 3B, + aliases from the gaps | 16 | **median 0**, most 3 | 3 (3) | 4 (4) | 4 (3) | median 2 ms, most 9 s |
| 3B, M2: 23 programs (3 with records) | 20 | median 0, most 3 | 5 (5) | 8 (7) | 3 (2) | median 3 ms, most 7 s |

- Targets: median questions ≤ 2 met; confident picks right ≥ 90% met
  (100% after the fixes); a cold build under 5 s missed with 3B on 4 CPU
  cores then (loading 8–10 s); met after M1.15 (below).
- What the first runs found, now fixed: readings that don't fit where the
  span is (a list where a number is needed) were offered and picked, so
  candidates must now let the line read; a `for each` variable stayed in
  scope after its loop, and the elaborator repaired it to the list's name.
- Left to the author, as meant: "positive numbers", "roll a die", "no
  digits". Not as meant: `character i of sentence in lower case`, which
  reads two ways that give the same result; 3B can't choose.
- M2 (records): `reading`, `team` and `contacts` ask about unknown
  fields (`cost of novel` → price, `length of novel` → pages, `total
  points of team` → score; 3B right in all 3) and `the book`/`the player`
  (one variable each: the rule). The one wrong model pick is "roll a die"
  (now `ask for a whole number`, meant none); the build before M2 picks it
  too, so it changed earlier (likely `c043cee`), not with records.
- Gaps turned into aliases (`has`, `begins with`, `backwards`, `in
  uppercase`, `word count of`, `is below zero`, `max of`, `is an even
  number`, `how many items in`, `average value of`). `biggest`, `lowest`,
  `mean` and `add up` stay questions, because `eval/questions.txt` measures
  them.

Cold build (M1.15: 15.9 s → 2.6 s, no repacking, cached system message;
a cold disk cache takes 140 s to page in 2 GB): archived in
[dream/state](dream/state/INDEX.md).
