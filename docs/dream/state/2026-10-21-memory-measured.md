# Memory measured (E1–E7)

Archived from state.md → "Memory measured" on 2026-10-21 (dream at 962e068).

`tools/peak_memory.py build/ovis eval/memory/*.ovis` (4 CPUs, peak
resident MB / seconds). Before E2 (E1's baseline), then now unchecked
(`--fast`) and checked (the default):

| Program | Before | `--fast` | Checked |
| --- | --- | --- | --- |
| `join_loop` (2M passes joining text, never freed) | 154.2 / 0.38 | 62.7 / 0.20 | 321.6 / 1.09 |
| `churn` (20000 lists of 50 records) | 54.1 / 0.08 | 54.6 / 0.07 | 172.1 / 0.43 |
| `tree` (20000 parent-linked trees of 31 nodes) | 81.6 / 0.12 | 54.4 / 0.09 | 334.3 / 0.72 |
| `join_loop_in_memory` (the loop with `with new memory:`) | — | 11.9 / 0.51 | 33.0 / 0.55 |
| `join_loop_freed` (the loop with `free label`) | — | 12.1 / 0.21 | 12.0 / 0.32 |

One join is one allocation now (`ovis_join`), which is why `join_loop`
shrank. Checked mode is 3–6× in time and memory for programs that make
many values (the table keeps an entry per live value); with `with new
memory:` or `free` in the loop the table stays small and memory is flat.
Making and freeing an arena each pass costs extra time (0.51 s against
0.21 s for 2M passes). `--strict-memory` (now the default) on the examples:
10 lines fail, all of two kinds: `for each i in the numbers from 1 to N`
and `show X with 2 decimals` / `show TEXT + …` (a list or text made inside
another statement); see remaining-tasks.md.
