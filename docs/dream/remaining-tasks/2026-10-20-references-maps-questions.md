# Finished: references R1–R8, maps follow-ups, cheaper questions

Archived from docs/remaining-tasks.md → "P1" and "P2: language features" on 2026-10-20 (dream at 3fa5131). Finished in c22e8b5 (R1b), a20df80 (questions), 3fa5131 (decimal keys) and the commits state.md's history names.

- [x] Cheaper questions: fewer tokens per option (a repeated argument
  said once lost 2 right of 26) and skipping a run-off one sure group
  settles (saved 1% of asks): measured, neither kept (state.md).

- [x] **Pointers, C structs and references**
  ([proposals/references.md](proposals/references.md), accepted). Covers
  the C struct, callback and included-header tasks below and the P0
  `fgets` buffer task. Each step leaves the suite green:
  - [x] R1 Core: C number types, Zig's pointer kinds, `load store offset
    cast size-of`, `ref` of anything (phrases: R7).
  - [x] R1b Non-null records, `or nothing`, narrowing (lists were never
    null; the prelude, examples and eval programs needed no change).
  - [x] R2 Import structs as C records (layout from clang), `a new T`
    (the header's own structs; `struct tm` from `time.h` needs R6).
  - [x] R3 Arrays (slices) in Core: `(list u8)`, `(array u8 N)`, `(view T
    P N)`, lists given to C as their items (phrases: R7).
  - [x] R4 Structs by value through C wrappers.
  - [x] R5 Callbacks, phrase values and prototypes on import, in Core
    (`phrase-of`, `call`, `(phrase …)`, `(c-phrase …)`; phrases: R7).
  - [x] R6 Headers beside the header and types its functions reach.
  - [x] R7 Phrases (`pointers.phr`, type words, phrase values) and docs.
  - [x] R8 Warning for a dropped result; `a copy of x` (deep): built with
    memory E2–E7 (`core::warn`, Core `copy`).

- [x] Maps: decimals as keys (done). Records as keys stay out: `keep`
  and `copy` copy a record, so a key by identity would be lost. `m at k`
  beside an operator asks (`counts at word + 1`), as `square root of x
  + 1` does: kept, a question by design.

Confirmed green on the macOS CI job at 3fa5131:

- [x] macOS: libclang is now given its resource directory and the SDK
  (`platform_flags`) and the real-C-library test is unskipped. macOS CI
  builds now (`<algorithm>`); `qsort` and `gmtime` were missing because
  the SDK declares them in private `_*.h` files beside `stdlib.h` and
  `time.h`, which now count as the header. Confirm on the macOS CI job.
