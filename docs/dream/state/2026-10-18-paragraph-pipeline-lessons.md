# Lessons from the paragraph pipeline

Archived from docs/state.md → "Lessons learned" on 2026-10-18 (dream at `c043cee`).

- Mock model servers should match only the paragraph part of a request. A
  mock that matched words in the context section once gave two paragraphs
  the same steps, which looked like an Ovis bug.

- Test the path where nothing is translated yet. The first translation of
  a paragraph that introduces a C library couldn't see the library's
  functions until the context also imported headers named in the steps.
  A scripted-model test caught it.
