# M1.6 questions, M1.7 resolution

Archived from remaining-tasks.md → "M1: phrase front end, no records" on 2026-10-18 (dream at `941d496`). Finished in the commit after `941d496`: the kinds in phrase-front-end.md, `missing_argument` as lower-ranked `unknown_phrase` candidates, the ranking shortcut at score ≥ 3 and margin ≥ 3 (6 of 26 eval questions, all right), rules and shortcut written as `Read as:` lines. The lockfile step moved to M1.8. Files are `questions.cpp` and `front_end.cpp`, not `resolve.cpp`.

- [ ] **M1.6 Questions** (`questions.cpp`): `struct question` and
  `struct candidate` as in phrase-grammar §4, and one generator per kind:
  `unknown_phrase` (arguments + verb, slot-type match, ranking, top 8),
  `ambiguous_parse`, `unknown_name`, `reference`, `missing_argument`. The
  deterministic rule of each kind runs first. Tests with no model: exact
  candidate lists for about 15 lines.
- [ ] **M1.7 Resolution** (`resolve.cpp`), in this order:
  1. a `Read as:` line under the source line (authoritative);
  2. the lockfile, keyed by a hash of line, span, context and candidate
     readings;
  3. the ranking shortcut, when its margin is wide;
  4. `choose`, accepted at `p ≥ 0.6` and a margin `≥ 0.3` (tuned in M1.14:
     1.5B is right on all 12 it picks at 0.4 and 0.1);
  5. otherwise a report listing the options ("Write one of them in the
     source").
  `--model-budget SECONDS` (default 10): questions left when it runs out
  are reported like unclear ones. Tests use a scripted model that returns
  given probabilities.
