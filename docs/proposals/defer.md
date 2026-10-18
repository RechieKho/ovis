# Proposal: `at the end` (Zig's `defer`)

Status: **accepted and built** (D1–D3; decisions at the end).
It builds on the built-in control forms of
[phrase-grammar.md](phrase-grammar.md) §7. Built as written, except: the
§4 note is `runs at the end of every pass of the loop.` / `of this branch.`
/ `of this block.`, and a helper file's top-level `at the end` is rejected
by the existing helper-file rule (it is not a definition).

## Why

`use-c` makes C resources common: files, SQLite handles, zlib streams. Each
needs a matching close on every way out of a function or loop, and an early
`give back` or `stop` makes it easy to forget one. Zig's `defer` puts the
cleanup next to the acquisition and the compiler runs it on every exit:

```zig
const file = try open(path);
defer file.close();
```

Ovis has no destructors and no garbage collection of C handles, so this is
the simplest way to make cleanup reliable. It is also plain to read in
English, which keeps the "nothing is hidden" principle.

## 1. What it looks like

```
to count lines in (path):
    file = fopen(path, "r")
    if file = null:
        give back 0
    at the end, fclose(file)
    count = 0
    line = buffer(256)
    while fgets(line, 256, file) != null:
        count = count + 1
    give back count
```

- `at the end, STATEMENT` defers one statement; the comma is optional.
- `at the end:` with an indented block defers the whole block.
- `defer` is an alias of `at the end`, for readers who know Zig or Go.

Both are **built-in control forms**, like `if` and `repeat` (decided by the
line's first words, `shape_of`), never templates and never a question for
the model. `at the end` is reserved as the start of a line.

## 2. Semantics (as in Zig)

1. **Scope is the enclosing block**, not the function: the body of a
   function, an `if`/`otherwise` branch, a loop body, or the top level of
   `main.ovis`. In a loop body it runs at the end of **every iteration**.
2. **It runs whenever control leaves that block:** falling off its end,
   `stop`, `skip`, `give back`, and, at the top level, the end of the
   program.
3. **Only if reached.** A deferred statement is registered when its line
   runs. An early `give back` above the `at the end` line skips it (the
   example above returns 0 without closing a file it never opened).
4. **Reverse order.** Several deferred statements in one block run last
   first, so resources are released in the reverse order they were taken.
5. **Values at exit time.** The deferred statement is evaluated when it
   runs, not when it is registered: `at the end, show count` shows the final
   count. (Go evaluates arguments at the `defer`; Zig does not. Ovis follows
   Zig.)
6. **`give back` evaluates its value first,** then runs the deferred
   statements, then returns. A deferred statement that changes the variable
   does not change what was given back.
7. **Not run** on `exit()` from C, on runtime errors (index out of range,
   division by zero) or crashes. Zig's panics behave the same way.

## 3. Rules (checked; each is an error with a suggestion)

- **A deferred block can't leave early.** `give back`, or a `stop`/`skip`
  aimed at a loop outside the deferred block, is an error: "An 'at the end'
  block can't give back a value; it runs while leaving." A loop entirely
  inside the deferred block may use `stop` and `skip`.
- **Names must already be set.** Every variable a deferred statement uses
  must be set by an earlier line of the same or an enclosing block. Writing
  `at the end, fclose(file)` above `file = …` is an error: "`file` is set
  only after this line; move 'at the end' below it."
- **Names first set inside a deferred block are local to it.** They are
  never hoisted out (the front end hoists other blocks' first assignments
  to a top-level `let`), because no line runs after the deferred block in
  that scope to use them.
- **Helper files** may use `at the end` inside functions only, since only
  `main.ovis` does things at the top level (architecture-spec §3).
- Nesting is allowed: an `at the end` inside a deferred block belongs to
  that block.

## 4. Nothing hidden: a note for the surprising case

Rule 1 is what Zig does, but in English "at the end" can be read as "at the
end of the function". When the enclosing block is not a function body or
the top level, the compiler writes a note under the line:

```
for each path in paths:
    file = fopen(path, "r")
    at the end, fclose(file)
        Compiler: runs at the end of every pass of the loop.
```

The note comes from the lowering (deterministic, like the elaborator's
notes) and follows the existing `Compiler:` rules: rewritten only by a
successful build, and byte-identical on rebuilds.

## 5. Ovis Core

One new statement:

```
(defer STATEMENT...)
```

It is a block like `if`'s `then`. Keeping it in Core (rather than expanding
it in the phrase front end) means the rules of §3 are checked once, Core
diagnostics point at the right line, and the old pipeline's model could
also write it until M1.13 removes that pipeline.

- **Parser** (`ast.cpp`): a new `statement` kind with a body.
- **Elaborator:** inference walks the body as any block. The implicit
  return (architecture-spec §7.3) takes the last line that is **not** a
  `defer`, so a trailing `at the end` doesn't hide a function's value.
- **Checker:** the "can't leave early" rule; `defer` at the top level of a
  helper file is an error (the helper-file rule in `modules.cpp`).
- **`describe_in_english`:** "when this block ends: …".

## 6. Code generation

Zig lowers `defer` by copying the deferred code to every exit of the block.
Ovis does the same in `llvm_codegen.cpp`, which already keeps a stack of
`scopes` and of `loops`:

- **Cleanup frames.** `generate_block` pushes a frame (a list of deferred
  bodies) beside its scope and pops it at the end. `(defer …)` emits no
  code; it appends its body to the top frame.
- **Falling off the end** of a block that isn't terminated: emit the top
  frame's bodies, last first.
- **`break`/`continue`:** `loop_targets` records the frame depth at the
  loop body. Emit every frame from the top down to that depth, then branch.
- **`return`:** generate and coerce the value into a temporary first
  (rule 6), emit every frame of the function, then `ret` the temporary.
- **`main`:** the top-level statements of `main.ovis` form one frame; it is
  emitted before `ret 0`, and before any top-level `return`.
- While a frame's bodies are emitted, the frame stack is cut below that
  frame, so a nested `defer` or an inner loop inside the body behaves as
  in any other block and a body never re-runs itself.

Copies cost code size per exit; deferred bodies are short (a close call),
and `-O2` merges identical tails. A body is a fresh block each time it is
emitted, so its `let`s get fresh allocas.

## 7. Phrase front end

- `shape_of` gains two shapes, `defer_line` (`at the end[,] STATEMENT`,
  `defer STATEMENT`) and `defer_block` (`at the end:`, `defer:`).
- The deferred statement is read like any statement: its span goes through
  the same chart parser, type filter and questions, and its `Read as:` lines
  sit under it as usual. A `defer_line` is one unit, as every top-level line
  is today.
- Lowering: `(defer S)` for the line form, `(defer BODY…)` for the block,
  with the hoisting exception of §3 and the note of §4.
- The scope filter (M1.4) applies rule "names must already be set" for
  free: it walks lines in order, so a name set later is not yet in scope.

## 8. Left out

- **`errdefer`.** Ovis has no error values yet (C status codes are plain
  integers). Revisit with them; the frame stack above extends to it by
  tagging each deferred body as "always" or "on error".
- **Function-scoped defer** (Go). Block scope does everything it does and
  more, and per-iteration cleanup in loops is the common case.
- **Freeing Ovis lists and text** (P1 memory task). `at the end` lets the
  author release C resources; it is not a memory manager.

## Decisions

Agreed with the author, as recommended:

1. **Wording:** `at the end`. `when done` was the runner-up; `afterwards`
   reads as "after the next line", and `finally` suggests a `try`.
2. **`defer` stays as an alias,** the only non-English keyword, for
   programmers who know Zig or Go.
3. **The §4 note** is written for every `at the end` whose block is not a
   function body or the top level.
