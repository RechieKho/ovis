# Proposal: fewer phrases in sight of a line

Status: **accepted** (2026-10-21; the owner took every recommendation in
§4: S1 at most one added word, S2 per-file `use NAME`, S4 `internal/`). The hypothesis was the owner's:
*the more phrases a line could match, the more often the model gets it
wrong*, so phrases might need scoping (public/private). Measured below;
plan S1–S5.

## 1. The hypothesis, measured

The model never matches a line against every phrase: the parser reads the
line deterministically, and only an unclear span becomes a question of at
most 9 readings (questions.md). So "more phrases" can hurt in four places,
measured separately: the candidates offered, where the right one ranks,
the ranking shortcut (which settles without the model), and the model's
pick.

**Method** (`eval/scope/`): `distractors.py` writes N `to …:` phrases of
two kinds: *near*, made of words the library and the cases use (`count`,
`total`, `largest`, `items`, `mean`…), as a utility package might
export; *far*, made of unrelated words (`harvest the violin in`, `the
stormy compass of`). Every phrase takes a list, text or number, so the
type filter lets it in wherever such an argument is. `prefix_cases.py`
puts them before every case's program; `sweep.sh` runs `ovis_eval
questions --no-model` per set, kind and N. The model runs used 3B on 4
CPUs, one order. All phrases read on their own (`--with` checks it).

**Ranking (no model), right reading in the 9 shown / mean candidates:**

| N phrases | questions.txt near | far | hard-questions near | far | wording near | far |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | 24/24, 10.2 | 24/24, 10.2 | 11/20, 15.2 | 11/20, 15.2 | 25/28, 8.6 | 25/28, 8.6 |
| 50 | 23/24, 14.8 | 24/24, 13.3 | 10/20, 21.1 | 9/20, 19.2 | 24/28, 10.8 | 24/28, 8.8 |
| 100 | 21/24, 19.2 | 24/24, 18.2 | 7/20, 25.0 | 9/20, 23.2 | 24/28, 13.2 | 24/28, 10.3 |
| 200 | 20/24, 18.5 | 24/24, 21.3 | 6/20, 24.1 | 9/20, 24.1 | 24/28, 13.0 | 24/28, 11.8 |

**3B's picks, right of 26 + 20 (questions + hard), asks:**

| Phrases in sight | Right | Confident picks right | Asks |
| --- | --- | --- | --- |
| library only | 23 + 11 = 34 | 95% / 73% | 40 + 41 |
| + 50 near | 21 + 12 = 33 | 90% / 80% | 63 + 57 |
| + 200 near | 19 + 9 = **28** | 94% / 60% | 62 + 59 |
| + 200 far | 22 + 9 = 31 | 91% / 69% | 82 + 60 |

**Whole programs** (`ovis_eval programs`, 25 programs, phrases appended):
200 near phrases settle `mean of scores` in `grades.ovis` **by the ranking
shortcut as `count the smallest mean of scores`**, silently; it was left
to the author before. Nothing else changed; the median build went from 6
to 33 ms (25 ms with far ones).

**Removing built-in phrases the lines can't take** (`pointers.phr`,
`memory.phr`, 27 templates, embedded library rebuilt) changed nothing: the
same candidates, ranks and picks. Their slots (`ptr`, `type`) already fail
the type filter.

So the hypothesis holds, with three refinements:

1. **It is the phrases that take the line's arguments, not the phrase
   count.** Unrelated phrases of the right types double the candidates
   and the asks (run-offs) and cost the hard set 2 of 11 shown; phrases
   sharing the library's words cost more (11 → 6 shown, 34 → 28 right).
   Phrases whose types don't fit cost nothing.
2. **Much of the damage isn't the model.** At 200 near, the ranking
   shortcut settled 4 questions.txt cases before the model was asked, all
   wrongly (2 of them had been right): a new phrase containing the span's
   words (`count the smallest mean of numbers` for `mean of numbers`,
   `add up the last prices of prices` for `add up prices`) covers every
   content word of the span, so it wins by 3 or more. The shortcut checks
   the words a reading *covers*, never the words it *adds*. These are
   confident and silent.
3. **The model's share**: of the 6 right answers lost (4 + 2), 4 were
   the model's. It said "none" where it had been right (2), picked a new
   phrase on an `expect: none` case (`the middle figures of numbers`), or
   had no right reading to pick (`sum of prices` was cut from the
   candidates; it picked `total the entries in prices`). Where the right
   reading fell to #12 of 27, it went from unsure to confidently wrong
   (`last item of numbers`). One order moves 3B by about 3 of 26
   (state.md), so 34 → 33 at 50 is noise; 34 → 28 at 200 is not.

Caveats: synthetic phrases of four shapes; 3B only; one order for the
model runs (the ranking runs are deterministic).

## 2. Who adds phrases today

| Source | In sight of | Grows by |
| --- | --- | --- |
| The library (171) | every line | Ovis releases; CI keeps the ranking floor |
| The program's `to …:` phrases | every line of every file | the author |
| Direct dependencies | every file of the program (`vocabulary::sees`) | `ovis add`; `internal/` hides a package's own files |

The program's and dependencies' phrases are what grows without a check,
and dependencies are made by someone else. That is where scoping pays.

## 3. Plan

Ordered by what each buys for its cost. Each is measured by re-running
`eval/scope/sweep.sh` and the model runs above; *done* means the 200-near
numbers come back to within one order's noise of the library-only row.

**S1. The shortcut must not take a reading that adds meaning** (not
scoping; the cheapest and the one that stops silent wrong readings).
`ranking_shortcut` also requires the reading to add at most one content
word the span doesn't have (function words and the template's own
optional words don't count). `first of names` → `first item of names`
(adds `item`) still settles; `mean of numbers` → `count the smallest mean
of numbers` (adds `count`, `smallest`) is asked. Expected: the 4
wrong shortcuts above go back to the model, `grades.ovis` back to the
author, and the CI floor and the programs' 3 right shortcuts unchanged.

**S2. A file sees a dependency's phrases only if it says so.** Today
every file sees every direct dependency. A file names the packages it
uses, `use csv` (as `use "x.h"` names a C header; `NAME's phrase` keeps
working where it names one), and only those files see it. A file that
needs nothing from a package is unaffected by it, whatever its size.
`ovis add` adds the line to the file it's run from, or `main.ovis`.
Breaking for packages written since H1–H9: `ovis fix` can add the `use`
lines for lines that read only with a dependency's phrases, and the
lockfile keeps old decisions settled.

**S3. Dependencies' phrases are offered only when they share a word.**
For an `unknown_phrase` question, a phrase from a dependency is a
candidate only if it shares a content word (or a 4-letter start) with
the span. The library keeps offering phrases by type alone, since
synonyms (`mean` → `average`) are the model's job and the library is
measured. A package that wants a synonym reached adds an alias.
This is the far row's fix: unrelated package phrases stop filling the 9
and causing run-offs.

**S4. Private phrases within a program.** A program's own phrases in a
file under `X/internal/` are seen only by the files under `X/` (Go's
rule). A package only knows an `internal/` at its root, where the parent
is the whole package; in a program that would hide nothing, so S4 needs
nested `internal/` folders, each scoped to its parent
(`pricing/internal/rates.ovis` is seen by `pricing/**`, not
`main.ovis`). Packages get the nested rule too. A program's phrases are few today (the eval programs have 0–3),
so this comes after S1–S3. If a per-phrase marker is wanted later, it
should be a word (`to … privately:`) not a sigil, since sigils are the
compiler's.

**S5. Keep it measured.** Commit a near-100 sweep to CI beside the
ranking floor (`--min-shown` on the prefixed `questions.txt`), so a
change to the ranking or the library that makes lines fragile to other
people's phrases fails. And do the open task `ovis check-phrases
--dependencies`: before `ovis add` or `ovis update`, list the user's
lines a dependency's phrases would change (the 200-near programs run
above is that check, by hand).

Not proposed: hiding parts of the library behind `use` (pointers, bits,
memory). Measured above, it buys nothing while the type filter does its
job, and it would make every C or bit program say more.

## 4. Decisions (taken: the recommendations)

1. S1's threshold: at most one added content word (taken), or none
   (stricter: `first of names` would be asked too).
2. S2: per-file `use NAME` for packages (recommended), or keep packages
   program-wide and rely on S3.
3. S4: `internal/` for a program's own folders (recommended), a
   per-phrase word, or neither until programs grow.
