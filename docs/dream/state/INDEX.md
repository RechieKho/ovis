# Archive of docs/state.md

| Date | File | Holds | Keywords |
| --- | --- | --- | --- |
| 2026-10-17 | [2026-10-17-manual-checks.md](2026-10-17-manual-checks.md) | one-off manual checks of the CLI against a mock model server, and of removing leftover steps | mock server, CLI, end to end, leftover steps, primes |
| 2026-10-18 | [2026-10-18-first-question-measurements.md](2026-10-18-first-question-measurements.md) | first `choose` timings on four hand-written questions, 1.5B and 3B | choose, speed, 1.5B, 3B, confidence, M0 |
| 2026-10-18 | [2026-10-18-paragraph-pipeline.md](2026-10-18-paragraph-pipeline.md) | the old paragraph pipeline: status, verification, CI, real-model trials (detail verdicts, copying, model sizes), early history | paragraphs, detail pass, translation, repair, few-shot, copying, verdicts, 1.5B, 7B, history |
| 2026-10-18 | [2026-10-18-m0-questions-measured.md](2026-10-18-m0-questions-measured.md) | M0: `ovis_eval questions` on 26 questions, 0.5B–7B table, ranking, 8 readings per question, thresholds, the 3B decision | questions, ranking, thresholds, 0.5B, 1.5B, 3B, 7B, M0.3, M0.4, eval |
| 2026-10-18 | [2026-10-18-ranking-investigation.md](2026-10-18-ranking-investigation.md) | why the ranking ties, the embedding tie-break built and reverted, option order, embeddings only | ranking, ties, synonyms, embeddings, tie-break, option order, orders, 8ffe5c3 |
| 2026-10-18 | [2026-10-18-cold-build-m1-15.md](2026-10-18-cold-build-m1-15.md) | M1.15 cold build timings: repacking, context, cached system message, 9 readings | cold build, repacking, use_extra_bufts, load time, M1.15, run-off |
| 2026-10-18 | [2026-10-18-superseded-decisions.md](2026-10-18-superseded-decisions.md) | the paragraph pipeline's decisions with the owner (Session 1, Plans 1–2) | decisions, paragraph pipeline, detail pass, 1.5B default, libc table |
| 2026-10-18 | [2026-10-18-paragraph-pipeline-lessons.md](2026-10-18-paragraph-pipeline-lessons.md) | lessons from the paragraph pipeline: mock servers, first translation | lessons, mock server, translation, paragraph |
| 2026-10-18 | [2026-10-18-history-m0-m1.md](2026-10-18-history-m0-m1.md) | history from the phrase pivot (`a3d9d95`) to M1.14 (`e58c93a`) | history, commits, M0, M1, pivot |
| 2026-10-18 | [2026-10-18-history-m1-15-to-m2-1.md](2026-10-18-history-m1-15-to-m2-1.md) | history rows: M1.15, M1.16, M2.1, ranking floor, phrase self-test, --orders, embedding experiments, daemon, candidates | history, cold build, messages, records Core, ranking floor, daemon, embeddings |
| 2026-10-19 | [2026-10-19-history-m2-to-ci.md](2026-10-19-history-m2-to-ci.md) | history rows from `c043cee` (GPU first) through M2–M5, `at the end`, the memory proposal and the macOS CI job | history, M2, M3, M4, M5, records, traits, defer, memory proposal, macOS |
| 2026-10-19 | [2026-10-19-programs-measured-m1-14.md](2026-10-19-programs-measured-m1-14.md) | `ovis_eval programs` runs M1.14–M2: 1.5B/3B/7B tables, targets, gaps turned into aliases | programs eval, M1.14, M2, 1.5B, 3B, 7B, aliases, targets |
