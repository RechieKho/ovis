# Phrases in sight measured (S1–S5)

Archived from state.md → "Phrases in sight measured" on 2026-10-22 (dream at 979d641).

Whether more phrases in sight of a line make questions worse
([proposals/phrase-scope.md](proposals/phrase-scope.md), `eval/scope/`),
2026-10-21: 200 generated phrases sharing the library's words cost
questions.txt 24 → 20 shown and the hard set 11 → 6, and 3B 34 → 28 right
of 46; 200 unrelated ones of the same argument types double the
candidates and asks. The ranking shortcut settled 4 lines wrongly with
them, before the model. Built-in phrases whose types don't fit cost
nothing (`pointers.phr` and `memory.phr` removed: no change). After S1
(the shortcut refuses a reading adding two or more words of meaning),
200 near: questions.txt 19 → 22 of 26 right (library alone 23), the hard
set unchanged (9; its losses are the model's), no wrong shortcut.
After S3, the phrases given as a dependency's (`sweep.sh OUT package`,
`ovis_eval --package`): 200 far ones change nothing in any set (shown and
candidates as with none); 200 near ones: questions.txt 24/24 shown (10.6
candidates, was 18.5 as the program's own), the hard set 8 of 20 shown
(11 with none, 6 as the program's own), wording 25/28 (as with none).
3B, one order: 23 + 10 = 33 of 46 right with either kind (34 with none;
28 near and 31 far as the program's own), asks 40 + 41 far (as with none)
and 49 + 47 near: within one order's noise of the library alone.
S5, 2026-10-22: 100 near phrases as the program's own (no `use` or shared
word keeps them out) leave questions.txt at 11 first, 24/24 shown, and
wording at 22 first, 24/28 shown (23 and 25 alone); CI holds those floors.
