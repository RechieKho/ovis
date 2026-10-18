# M1.14 evaluation

Archived from remaining-tasks.md → "M1: phrase front end, no records" on 2026-10-18 (dream at `6f19e00`). Finished in the commit after `6f19e00`: 20 programs in `eval/programs/` with `# expect` labels, `ovis_eval programs`, results in state.md. The cold-build target was missed; that became M1.15.

- [ ] **M1.14 Evaluation**: about 20 programs in `eval/programs/`, written
  the way a person would, extending `eval/questions.txt`. `ovis_eval questions` reports per program:
  questions asked, how each was settled (rule, ranking, model, author),
  model accuracy against hand labels, and time. Provisional targets:
  - median questions per program ≤ 2;
  - confident model picks right ≥ 90%;
  - a cold build with the model under 5 s on 4 CPU cores.
  Add aliases first, templates second, for the gaps it shows.

