# Dream: the archive of inactive project memory

The tracking documents hold only what is **active**: current status, open
work, and how things work now. Information that is no longer used day to day
is moved here, word for word, by **dreaming** (see
`.claude/skills/dream/SKILL.md`). Nothing is deleted; it is moved.

| Active document | Archive |
| --- | --- |
| `docs/state.md` | [state/](state/INDEX.md) |
| `docs/remaining-tasks.md` | [remaining-tasks/](remaining-tasks/INDEX.md) |
| `docs/architecture-spec.md` | [architecture-spec/](architecture-spec/INDEX.md) |

- Every archive directory has an `INDEX.md` with one line per archived file:
  the date, the file, what it holds, and keywords to search for.
- Archive files are named `YYYY-MM-DD-topic.md`, say where they came from
  (document, section, commit), and are never edited afterwards. To correct
  something, add a new file.
- Read the archives only when needed: search the INDEX files first
  (`grep -ri <word> docs/dream/*/INDEX.md`).
