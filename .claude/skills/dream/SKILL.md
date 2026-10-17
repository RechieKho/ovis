---
name: dream
description: Consolidate this project's memory documents (docs/state.md, docs/remaining-tasks.md, docs/architecture-spec.md, CLAUDE.md) against the code and git history - verify claims, merge duplicates, drop stale or contradicted entries, move finished tasks to history and record lessons learned. Use when asked to "dream", consolidate, tidy or refresh the project docs or memory, or on a schedule.
---

# Dream: consolidate the project's memory

The project remembers itself in four committed files:

| File | Holds | Must not hold |
| --- | --- | --- |
| `docs/state.md` | what is built, what is verified and how, environment notes, pinned versions, decisions, history, lessons | plans, design detail |
| `docs/remaining-tasks.md` | open work by priority, as checkboxes | finished work, status |
| `docs/architecture-spec.md` | how things work now, and the invariants to keep | plans, status, history |
| `CLAUDE.md` | short orientation and working rules for any session | anything that goes stale quickly |

Sessions add to these files one change at a time. Over time they collect
duplicates, contradictions, stale claims and finished tasks that were never
ticked. A dream is one pass that cleans this up. **The code and git history
are the source of truth; the documents describe them.**

Only edit those four files. Never change code, tests, examples or lockfiles
during a dream. If you find a bug, write it down as a task.

## 1. Gather evidence

1. Find the last dream: the `Last dreamed:` line in `docs/state.md`. If there
   is none, use the first commit that touches `docs/`.
2. Read `git log --stat <last>..HEAD` and skim the diffs. They show what
   changed that the documents may not reflect.
3. Build and test (see CLAUDE.md for the commands; on a fresh cloud container
   see "Environment notes" in `docs/state.md` first). Record the exact test
   case and assertion counts. If the build or tests fail, say so in state.md
   and add a P0 task. Never write "passing" without having run them.
4. Read all four documents in full.

## 2. Check every claim

Check each factual statement against the code: file paths, function and
type names, defaults (attempts, context size, token limits, thresholds),
pinned versions in `CMakeLists.txt`, CLI options in `src/main.cpp`, the
lockfile format, the number of examples, and anything else that can be
checked. Correct anything wrong. If you can't check a claim, mark it as
unverified rather than deleting or keeping it silently.

## 3. Consolidate

- **Duplicates:** state each fact once, in the file that owns it (see the
  table); other files link to it.
- **Contradictions:** keep what the code and newest commits support.
- **Finished tasks:** remove them from remaining-tasks.md. Add a line to the
  history in state.md, with the commit that did the work.
- **New gaps:** add bugs, limitations and TODOs you noticed in the diffs to
  remaining-tasks.md, in the right priority.
- **Dates:** turn relative dates ("yesterday", "last session") into absolute
  ones (YYYY-MM-DD) and use commit hashes where possible.
- **Spec drift:** if the code now works differently from the spec, fix the
  spec. If the code breaks a stated invariant, keep the invariant and add a
  P0 task.
- **Lessons:** under "Lessons learned" in state.md, record patterns that
  would save a future session time. Examples: a class of bug that recurred,
  a check that caught a real problem, a tool or environment trap. Keep it
  short; merge similar lessons, and drop ones that no longer apply.
- **Size:** keep CLAUDE.md under about 40 lines. Keep the others readable in
  one sitting: summarise old history rather than letting it grow forever.

## 4. Finish

1. Update `Last dreamed:` in state.md to today's date and the current HEAD
   commit.
2. Read the documents through once more to make sure they agree with each
   other.
3. Commit only the documents, with the message `Dream: consolidate project
   docs`. Put a short list of the changes in the body. Push to the current
   branch. Don't open a pull request unless asked.
4. Report what changed: corrected claims, merged duplicates, tasks closed
   and added, and lessons recorded. Report anything you couldn't verify,
   too.

If nothing needs changing, update only the `Last dreamed:` line, commit, and
say the documents were already consistent.
