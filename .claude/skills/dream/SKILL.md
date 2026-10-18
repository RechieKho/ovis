---
name: dream
description: Archive inactive information out of this project's tracking documents (docs/state.md, docs/remaining-tasks.md, docs/architecture-spec.md, CLAUDE.md) into docs/dream/<document>/, keeping each tracking document active, correct and under 500 lines. Run after implementing a feature, when a tracking document nears 500 lines, or when asked to "dream".
---

# Dream: archive inactive project memory

The tracking documents are the project's working memory, and every session
reads them. Over time they fill with information that is no longer used
day to day. Dreaming moves that inactive information, **word for word**,
into an archive directory per document. The active files stay short and
current, and nothing is lost.

| Active document | Archive directory |
| --- | --- |
| `docs/state.md` | `docs/dream/state/` |
| `docs/remaining-tasks.md` | `docs/dream/remaining-tasks/` |
| `docs/architecture-spec.md` | `docs/dream/architecture-spec/` |
| `CLAUDE.md` | `docs/dream/state/` (CLAUDE.md should hold only rules) |

**Hard limit: every tracking document, and every `INDEX.md`, must stay
under 500 lines.** The test suite enforces this ("Keep tracking documents
under 500 lines" in `test/test.cpp`). Aim to leave each document well below
the limit after a dream, so the next few features fit.

## When to dream

- After implementing a feature, once its state and tasks are updated: in the
  same session, before you finish.
- Whenever a tracking document is past about 400 lines.
- When asked.

Only touch documentation (the tracking documents and `docs/dream/`). Never
change code, tests, examples or lockfiles in a dream.

## What is inactive

Archive it if nobody needs it to work on the project *now*:

- **state.md:**
  - history entries older than the most recent few;
  - verification logs of finished features (keep the current test counts);
  - superseded decisions (keep the current rule, archive how it changed);
  - lessons that no longer apply;
  - environment notes for setups no longer used.
- **remaining-tasks.md:** finished tasks and dropped tasks, each with the
  commit that finished it or the reason it was dropped.
- **architecture-spec.md:** descriptions of designs that were replaced or
  removed, and old formats that are only kept for compatibility (keep one
  line saying they exist).

Keep it active if it describes how things are now, what is open, a decision
still in force, or something a session must know to avoid breaking things.
**When unsure, keep it.**

## How to dream

1. **Measure.** `wc -l CLAUDE.md docs/*.md docs/dream/*/INDEX.md`
2. **Check what is current.** Read `git log --stat` since the `Last dreamed:`
   line in `docs/state.md`, and read each tracking document. While reading:
   - fix claims the code contradicts;
   - fix broken references;
   - tick finished tasks.

   The code is the source of truth.
3. **Pick the inactive parts** (see above), per document.
4. **Move each part.**
   - Create `docs/dream/<document>/YYYY-MM-DD-topic.md`, starting with a
     heading and the line `Archived from <document> → "<section>" on
     <date> (dream at <commit>).`
   - Paste the text unchanged under that line. Add context only if the text
     would be unclear on its own.
   - Archive files are never edited later. To correct one, add a new file.
5. **Index it.** Add a row to `docs/dream/<document>/INDEX.md`: the date, a
   link to the file, what it holds, and the keywords someone would search
   for. When an INDEX nears the limit, move its oldest rows into
   `INDEX-<year>.md` and link that file from the INDEX.
6. **Leave a pointer** in the active document where the text was, if
   someone might look for it there. One line is enough, e.g.
   `Older history: [dream/state](dream/state/INDEX.md).`
7. **Update** `Last dreamed: <date> at <HEAD commit>` in `docs/state.md`.
8. **Verify.**
   - Every tracking document is under 500 lines.
   - `./build/test_ovis "[docs]"` passes.
   - The links in the active documents point at files that exist.
9. **Commit** only the documents, with the message `Dream: archive inactive
   project memory`. List what moved where in the body. Push to the current
   branch.

## Reading archived memory

Search the indexes first:
`grep -ri <word> docs/dream/*/INDEX.md`
Then open only the files that match.
