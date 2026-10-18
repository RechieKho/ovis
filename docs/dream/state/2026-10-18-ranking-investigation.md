# Ranking investigation: ties, embeddings, option order

Archived from docs/state.md → "Phrase front end: questions measured" on 2026-10-18 (dream at `c043cee`).

Why (2026-10-18): in all 24 cases the right reading ties for the top
lexical score; the score never puts a wrong reading above it. It is blind
to synonyms (`floored` shares no word, prefix or trigram with any
template), so ties fall back to library order, and templates added before
`rounded down` pushed it to 10th. Only 8 cases have a unique top. The
miss costs a run-off ask, not a wrong reading: `choose` asks in groups of
9, so the model still sees every tied reading. A scratch probe (not in the
repository) broke ties by cosine similarity of the span and each reading,
using last-token-pooled hidden states of the Qwen models already used:
first in 20 of 23 non-shortcut cases (9 today), shown in 23 of 23, with
both 0.5B and 3B. Mean pooling: 15 (0.5B) and 18 (3B). Remaining misses
(0.5B): `last item`, `rounded down`, `as text`, each 2nd or 3rd. Cost on 4
CPUs, one text at a time: about 0.5 s per question with 0.5B, 1.6 s with
3B; batching the readings in one decode should cut this. 23 cases and 4
variants tried, so expect some overfit.

Built in `8ffe5c3` and reverted: ranking first rose to 21 of 24 (3B), 18
(1.5B), 20 (0.5B), but right picks did not: 3B 23 → 23 of 26 (fixes
"mean", but "median" becomes a confident "average", now listed first),
1.5B 8 → 4, 0.5B 3 → 3. The run-off already shows the model every tied
reading, and its answer depends on option order. Choosing the 9 shown by
embedding, in library order with no run-off, asked 25 questions instead
of 37 but 3B fell to 21. Ranking first is a poor stand-in for right picks.

Option order alone (same readings, 6 orders: as ranked, reversed, 4
shuffles): 3B right in 20–23 of 26, 1.5B in 8–12. The best guess barely
moves (3B 22–23, 1.5B 17–19); the order decides whether it clears the
60%/30% confidence bar. 3B is right in every order on 17 cases. "none"
cases are fragile: "median" and "standard deviation" become a confident
"average" in 3 and 2 of 6 orders. So one order can't measure a change of
3 or less: the tie-break (23) and the 9-shown variant (21) are both within
it. The 1.5B tie-break (4) is below every order seen.

`ovis_eval questions --orders N` now reports this. 3B, 6 orders: right in
22 to 23 of 26 (mean 22.7); 21 cases right in every order, 2 in none
("put 5 into", "write": always unsure), 3 depend on the order ("mean" 2
of 6, "highest" 3 of 6, "read a line of text" 5 of 6).

Embeddings only (`--embeddings-only`: ties ordered by embedding, the
first taken, nothing asked): 3B 21 of 26 right, 0.5B 20, against 3B asked
22–23. Per case 0.3 s (0.5B, 0.5 GB) and 1.2 s (3B), against about 2 s
per question asked with 3B. The difference is in the misses: all of the
embeddings' misses are wrong picks (3B: "first item" read as "last item",
"as a string" as "as a whole number", "order" as "show", and "median" and
"standard deviation" as "average"), while 3B asked is mostly unsure,
which goes to the author. The similarity margin doesn't tell them apart:
wrong picks have margins 0.001 to 0.075, like right ones. So the model's
edge as a matchmaker is 2–3 cases of 26 and knowing when it doesn't know.
