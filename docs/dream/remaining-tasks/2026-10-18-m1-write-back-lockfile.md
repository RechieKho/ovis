# M1.8 write-back and lockfile

Archived from remaining-tasks.md → "M1: phrase front end, no records" on 2026-10-18 (dream at `5a6e999`). Finished in the commit after `5a6e999`: `ovis::phrase::lockfile` (version 3) in phrase-front-end.md. The front end writes no `Compiler:` notes yet (the elaborator's notes are dropped); existing ones are kept.

- [ ] **M1.8 Write-back**: `Read as:` lines rendered under their line,
  `Compiler:` notes kept as now. Rebuilding gives byte-identical files (the
  lesson in state.md). Lockfile entries for questions, with the model
  fingerprint, keyed by a hash of line, span, context and candidate
  readings, tried after `Read as:` lines and before rules (M1.7's step 2).
