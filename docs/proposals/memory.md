# Proposal: explicit memory

Status: **draft; the direction is decided, the details are for the author**
(open questions at the end). It answers the P1 task "lists and text are
never freed" in [remaining-tasks.md](../remaining-tasks.md), and settles
the P2 task on memory C functions return. It builds on `at the end`
([defer.md](defer.md)).

**Decided by the author (2026-10-18):** memory management is explicit.
The author decides when memory is freed, and Ovis never frees anything
behind their back. There is no garbage collector and no reference
counting. An earlier draft recommended a garbage collector; it is
summarised in §9.

## Why

Ovis allocates lists, records and text on the heap and never frees them.
This loop peaks at **783 MB** (measured 2026-10-18), because each pass
keeps two texts (`count` as text, and the join):

```
count = 0
while count < 10000000:
    label = "item " + count
    count = count + 1
show count
```

With this proposal the author says where that memory goes:

```
count = 0
while count < 10000000:
    with new memory:
        label = "item " + count
    count = count + 1
show count
```

Everything made inside `with new memory:` is freed when the block ends,
on every way out of it, the same way `at the end` runs.

## 1. Model

The design takes three ideas, one from each of three languages:

- **Zig's arenas and `defer`.** Memory is freed in bulk at a point the
  author chose, and `at the end` puts the freeing next to the making.
- **Odin's context allocator.** Allocation goes to the *current memory*.
  Phrases and functions don't take an allocator parameter, which would put
  an extra slot in every English phrase.
- **Zig's safe build modes.** A checked mode, on by default, turns
  use-after-free, double free and leaks into Ovis errors with line
  numbers, instead of crashes.

There are two kinds of memory:

| Memory | Made by | Freed |
| --- | --- | --- |
| **the program's memory** | always there; current outside any `with` | value by value with `free x`; whatever is left at exit |
| **a new memory** (an arena) | `with new memory:`, or `scratch = new memory` | all at once, at the end of its `with` block or by `free scratch` |

Every operation that makes a list, record or text makes it in the current
memory:
- `+` and `concat` on text, `text of`, `as text`, `buffer`;
- `ask for a line`, `substring`;
- `empty list of`, written-out lists, `a NAME with …`;
- `sorted`, and any library phrase that gives back a new list;
- appending to a list, when its storage grows.

Nothing else allocates. Numbers, booleans and locals stay on the stack.

## 2. Freeing one value: `free`

```
line = ask for a line
at the end, free line
```

- `free x` works on text, lists, records and C pointers. The checker
  picks the runtime call from x's type, so one word covers all of them.
  It replaces `free(x)` from `use "stdlib.h"`, which still works.
- **It is shallow, like Zig's `allocator.free`.** Freeing a list frees the
  list and its storage, not its items, which may be shared. Freeing a
  record frees the record, not what its fields hold. To free items too:
  ```
  for each name in names:
      free name
  free names
  ```
  Freeing a whole structure is what a new memory is for (§3).
- Freeing a value made in a new memory does nothing at that point: the
  memory frees it with everything else. (In checked mode, §5, it is marked
  freed, so a later use is still reported.)
- Freeing `nothing` (null) does nothing, as in C.

## 3. Freeing many values at once: new memory

**Block form.** This is the common case:

```
for each path in paths:
    with new memory:
        text = read the file path        # a phrase of the program
        show number of words in text
```

`with new memory:` makes an arena, makes it the current memory for the
block, and frees it on every way out: falling off the end, `give back`,
`stop`, `skip`, the end of the program. It is built on the same exit
frames as `at the end` (architecture-spec §8). Blocks nest: the inner
memory is freed first.

**Named form.** Use this when a memory is filled in several places, or
reused pass after pass. (`there are requests` and `handle the next
request` stand for the program's own phrases.)

```
scratch = new memory
at the end, free scratch
while there are requests:
    with scratch:
        handle the next request
    clear scratch        # frees what is in it, keeps it for reuse
```

A named memory is an ordinary value of type `memory`. It can be passed to
a phrase, or kept in a record next to what it holds (a parsed document
and its memory). Its values live until `free m` or `clear m`.

`clear m` frees what a memory holds and keeps it for reuse, which saves
asking the system for pages on every pass. That is Zig's
`arena.reset(.retain_capacity)`.

**Keeping a value.** A value made in a new memory dies with it. To keep
one, copy it out:

```
names = empty list of text
while true:
    with new memory:
        line = ask for a line
        if line is nothing:
            stop
        add keep line to names
```

`keep x` copies x into the memory that was current *outside* the block.
The copy is deep (text, lists, records and what they hold, made in the
block's memory), and shared values stay shared in the copy, so cycles are
fine. Values from outside the block aren't copied, because they already
live long enough.

## 4. The checker: values that outlive their memory

A new memory makes use-after-free easy:

```
with new memory:
    line = ask for a line
    add line to names        # names outlives the memory
show line                    # line's memory is gone
```

The checker follows every value made inside a `with` block and reports
when one is stored where it outlives the block:
- in a variable, list or record from outside the block;
- given back from the function;
- read after the block ends (variables are hoisted to the function, so
  `line` is still a name there).

```
ovis: notes.ovis, line 4: `line` is made in the new memory of line 2, which
is freed when that block ends; it can't be added to `names`, which is from
outside it. Write `add keep line to names` to keep a copy.
```

The analysis is a forward pass over each function. Each local is marked
"may hold memory of block B" when it is set from an allocating
expression, from a call made inside B, or from another marked local. A
store into something from outside B is an error unless it goes through
`keep`. It does not follow values through calls: a function that adds its
argument to a global list escapes the check. Values in a named memory are
not checked either, since `free m` may happen anywhere. Checked mode
catches both when the program runs (§5).

## 5. Checked mode

Like Zig's Debug and ReleaseSafe modes, programs are **checked by
default**. `ovis run --fast` and `ovis build --fast` turn the checks off.

In checked mode the runtime keeps a table of every value Ovis made: where
it starts and ends, its memory, the line that made it, and the line that
freed it. Freed blocks are filled with a pattern and held back from reuse
for a while, so a stale pointer stays recognisable. Every list access,
field access and text operation checks its value. These are reported:

| Error | Message (example) |
| --- | --- |
| use after free | `names (line 9) holds text freed on line 7; it was made on line 5` |
| double free | `line was already freed on line 7` |
| freeing what Ovis didn't make | `free can't free a text written in the program ("hello", line 3)` |
| freeing the middle of a value | `free was given a place inside text made on line 5, not the text itself` |

The generated code keeps the current line in a global (`ovis_line`), set
at each statement in checked mode only, so runtime errors can name lines.
All runtime errors gain the line this way, including today's list-index
error.

`--report-leaks` lists, at exit, every value made in the program's memory
that was never freed, grouped by the line that made it:

```
ovis: 10000000 texts made on line 3 were never freed (232 MB)
```

The report is opt-in. Leaving top-level values to the end of the program
is a legitimate choice, as in a Zig program that frees its arena at exit,
and most short programs do exactly that. It is how an author finds the
loop that grows.

In fast mode there is no table, `free` frees straight away, and a
use-after-free is undefined, as in C or Zig's ReleaseFast.

## 6. C libraries

- Memory a C function returns is the author's, and the same `free` frees
  it: `copy = strdup(name)` then `at the end, free copy`. This settles the
  P2 ownership task: C ownership follows the C library's documentation,
  with no import magic.
- C pointers into Ovis text are fine: `fgets(line, 256, file)` returns
  `line` itself, `strchr` a place inside text. They are valid while the
  text is.
- Ovis values given to C are the author's to keep alive while C uses
  them, for example text bound with SQLite's `SQLITE_STATIC`.
- `free` on a C pointer calls C's `free`. Checked mode knows which
  pointers Ovis made, so it never mixes the two up.

## 7. Implementation

**Runtime** (`runtime.cpp`):
- A `ovis_memory` is either the program's memory (`malloc`-backed) or an
  arena of chunks, which starts at 4 KB and doubles. Memories form a stack:
  `ovis_memory_push` and `ovis_memory_pop` change the current memory, and
  `ovis_memory_new`, `ovis_memory_free` and `ovis_memory_clear` handle the
  rest.
- Every allocation goes through `ovis_alloc(size)` on the current memory.
  A list's storage is allocated from the memory the list was made in, not
  the current one, so growing a list never leaves its storage in a memory
  that dies first.
- `ovis_free_text`, `ovis_free_list` and `ovis_free_record`: for a value
  in an arena they do nothing (checked mode marks it), and for the
  program's memory they call `free`. Telling the two apart in fast mode
  costs one walk over the live arenas' chunk ranges, and there are few of
  them.
- `ovis_keep(value, kind)`: the deep copy, with a pointer map so shared
  values are copied once.
- Checked mode: the value table (a hash map from start address, plus a
  sorted index for "inside a value"), the poison pattern, the reuse delay,
  and `ovis_check_value(pointer)` calls. These are compiled in by
  `-DOVIS_CHECKED` when the runtime is linked.

**Ovis Core:**
- a type `memory`;
- forms `(free X)`, `(new-memory)`, `(with-memory M BODY...)`,
  `(with-new-memory BODY...)`, `(clear-memory M)` and `(keep X)`.

`with-new-memory` is lowered by the elaborator to a `new-memory`, a
`with-memory`, and a `defer` that frees the memory. `with-memory` is
lowered by codegen to a push, plus a pop in the block's exit frame.
`free` is typed by the checker.

**Phrases:** `free (x)`, `(name) = new memory`, `with new memory:`, `with
(m):`, `clear (m)` and `keep (x)`. None of these is ambiguous, so the
model is never asked about them.

## 8. Plan

- **E1. Measure.** Add the §Why loop and two variants as test programs:
  one that makes and drops a list of records each pass, and one that
  builds a parent-linked tree. Record their peak memory as the baseline.
- **E2. Runtime memories.** Route every allocation through `ovis_alloc`
  and add the memory stack, arenas and `ovis_free_*`. Still no surface
  syntax; all tests pass unchanged, because everything goes to the
  program's memory.
- **E3. Ovis Core and codegen.** Add the forms of §7, the exit-frame pop,
  and `keep`. Core tests: a `with-new-memory` in a loop stays under a
  fixed bound, and `give back` and `stop` inside it free the memory.
- **E4. Phrases.** Add the phrases of §7 and update
  `examples/wordcount.ovis`, plus one new example that uses a named memory
  with `clear`.
- **E5. The checker** (§4), with its messages, and tests for each kind of
  escape and for `keep`.
- **E6. Checked mode** (§5): the value table, `ovis_line`, `--fast` and
  `--report-leaks`. Measure the checked-mode overhead on the eval
  programs.
- **E7. Docs.** Update architecture-spec §6 and §8, c-libraries.md and
  state.md, and tick the P1 and P2 tasks.

Each step leaves the suite green. E2 and E3 alone already fix the leak
for authors who write Ovis Core.

## 9. Considered and rejected

- **Garbage collection** (Boehm GC, embedded; the earlier draft). It
  needs no code from the author and handles cycles and C pointers, but
  memory is freed when the collector decides. Rejected by the author in
  favour of explicit memory.
- **Reference counting.** Also implicit. It leaks cycles, and its header
  before the characters conflicts with text being a plain `char *` shared
  with C.
- **Ownership and borrowing** (Rust). It is explicit, but it needs
  lifetime words and fights Ovis's shared references.
- **Inferred regions** (MLKit). The compiler would choose the regions,
  which is not explicit, and it fails on loops that build lists.
- **An allocator parameter on every function** (Zig). It is the most
  explicit option, but every phrase would grow an allocator slot. The
  current memory (Odin) is just as explicit about *when* memory is freed.
- **Finalizers that close C handles.** These are hidden and late. Use
  `at the end`.

## Open questions for the author

1. **Implicit allocation into the current memory** (§1), or must every
   allocation be named, as in strict Zig (no `+` on text without a
   variable)? I recommend the current memory: `+` on text stays English.
2. **Wording:** `with new memory:`, `free x`, `keep x`, `clear m`,
   `scratch = new memory`. Alternatives include `using fresh memory:`,
   `throw away x` and `a lasting copy of x`.
3. **Checked by default, with `--fast`** (§5)? I recommend yes, as Zig's
   Debug is the default.
4. **`free` shallow** (§2), or does freeing a list free its items too? I
   recommend shallow: items may be shared, and a new memory frees
   structures whole.
5. **A value escaping its memory** (§4): compile error suggesting `keep`
   (recommended), or copy it automatically? Automatic copying would hide
   an allocation.
6. **Leak report opt-in** (`--report-leaks`, §5), or always on in checked
   mode? I recommend opt-in, so short programs that leave values to the
   end stay quiet.
