# Proposal: explicit memory

Status: **accepted** (decisions at the end). Built: E1–E4 (the runtime's
memories, the Core forms, the phrases), E6 (checked mode) and E5 behind
`--strict-memory` (it is not yet the default: state.md, remaining-tasks.md);
the plan is E1–E7 in [remaining-tasks.md](../remaining-tasks.md). It answers the P1
task "lists and text are never freed" and the P2 task on memory C
functions return. It builds on `at the end` ([defer.md](defer.md)).

Memory management is explicit. The author decides when memory is freed,
and Ovis never frees anything behind their back. There is no garbage
collector and no reference counting. Like Zig, no new value is hidden:
every one is made where it is stored, so it can be freed from there.

## Why

Ovis allocates lists, records and text on the heap and never frees them.
This loop peaks at **783 MB** (measured 2026-10-18), because each pass
keeps the joined text:

```
count = 0
while count < 10000000:
    label = "item " + count
    count = count + 1
show count
```

Under this proposal that program doesn't build. Each pass gives `label`
new text and loses the old text (§4). The author picks a fix:

```
while count < 10000000:
    with new memory:                 # everything made here is freed at its end
        label = "item " + count
    count = count + 1
```

```
label = ""
while count < 10000000:
    free label                       # free the old text before replacing it
    label = "item " + count
    count = count + 1
```

## 1. Model

The design takes three ideas, one from each of three languages:

- **Zig's arenas, `defer` and no hidden allocations.** Memory is freed in
  bulk at a point the author chose. `at the end` puts the freeing next to
  the making, and every new value is visible in the source (§2).
- **Odin's current allocator.** New values are made in the *current
  memory*, so phrases and functions don't take an allocator parameter
  (which would add a slot to every English phrase).
- **Zig's safe build mode.** A checked mode, on by default, turns
  use-after-free, double free and leaks into Ovis errors with line
  numbers (§6).

There are two kinds of memory:

| Memory | Made by | Freed |
| --- | --- | --- |
| **the program's memory** | always there; current outside any `with` | value by value with `free x`; whatever is left at exit |
| **a new memory** (an arena) | `with new memory:`, or `scratch = new memory` | all at once, at the end of its `with` block, or by `free scratch` / `clear scratch` |

## 2. Every new value has a place

**These make a new value:**
- built-ins:
  - joining text (`+` with text, `concat`), `text of` and `as text` on
    numbers and booleans;
  - `substring`, `chr`, `buffer`, `ask for a line`;
  - `empty list of`, a written-out list, `a NAME with …` (a record);
  - `keep x` (§5);
- a call to a phrase or function that **gives back a new value** (below).

Numbers, booleans and text written in the program (`"hello"`) are never
new values. Neither is reading a variable, a field or a list item. Neither
is a C function's result: C's own documentation says whether it must be
freed (§7).

**The rule.** A new value may only be made as the value a statement
stores:

| Statement | Example |
| --- | --- |
| setting a variable | `label = "item " + count` |
| setting a field | `the title of b = name + "!"` |
| adding or putting into a list | `add a book with title t to books` |
| giving it back | `give back first + " " + last` |

Anywhere else is an error: inside another expression, as an argument, in
a condition, in `show`. The value would have nowhere to be freed from.

```
show the names sorted
```
```
ovis: report.ovis, line 7: `the names sorted` makes a new list inside
`show`, where nothing can free it. Store it first:
    sorted_names = the names sorted
    show sorted_names
```

**One join is one value.** `line + (row * column as text) + " "` makes
one text, not three: the code generator writes all the parts into one
allocation. Number and boolean conversions inside a join are part of it.
A part that is a call giving back a new value (a record's `text of`) is
not, and has to be stored first.

**Which phrases give back new values.** The elaborator works this out for
every function, including the library's (the prelude):
- *new*: every `give back` is a new value, or a variable that only ever
  holds new values;
- *existing*: no `give back` is a new value (a parameter, a field, an
  item, a global);
- *mixed*: an error at the definition, because a caller couldn't know
  whether to free the result:
  ```
  ovis: names.ovis, line 4: `the longer of (a) and (b)` gives back new text
  on line 6 and existing text (`a`) on line 8. Give back `keep a` there to
  make both new, or give back no new text.
  ```

A call to a *new* phrase follows the rule above. A call to an *existing*
phrase is like reading a variable.

**Why strict.** Every value that can be freed has a name or place to free
it from. `--report-leaks` names it (§6), and the checks in §4 can follow
it. The cost is extra lines where a value would otherwise be made and
used at once. The checker's message gives the line to write.

The rule is checked after a line is read. It never changes which reading
is chosen, and the model is never asked about it.

## 3. Freeing one value: `free`

```
line = ask for a line
at the end, free line
```

- `free x` works on text, lists, records and C pointers. The checker
  picks the runtime call from x's type, so one word covers all of them.
  It replaces `free(x)` from `use "stdlib.h"`, which still works.
- **It is shallow, like Zig's `allocator.free`.** Freeing a list frees the
  list and its storage, not its items, which may be shared. Freeing a
  record frees the record, not what its fields hold. To free the items:
  ```
  for each name in names:
      free name
  free names
  ```
  Freeing a whole structure is what a new memory is for (§5).
- Freeing a value made in a new memory does nothing at that point: the
  memory frees it with everything else. (In checked mode it is marked
  freed, so a later use is still reported.)
- **Freeing text written in the program, or `nothing`, does nothing.**
  Unlike Zig. A name often starts as `""` and later holds joined text, as
  in the §Why loop, and `free label` has to be right on every pass.

## 4. The checker: lost and stale values

**A lost value.** Giving a name a new value while it may still hold a new
value from the program's memory loses the old one. The old value was not
freed and is not stored anywhere else, so nothing can free it:

```
ovis: count.ovis, line 3: `label` gets new text here on every pass of the
loop, and its old text is never freed. Free it first (`free label`), or
make it in a new memory (`with new memory:`).
```

The check does not report:
- an old value that was stored (in a list, a field or another variable),
  passed to a phrase that keeps its argument, or given back;
- code lexically inside `with new memory:`, where the memory frees
  everything.

A function's body is checked as if it ran in the program's memory, since
it may be called from either kind. A `free` it needs is harmless in a new
memory (§3).

**A stale value.** A value made in a `with new memory:` block dies with
it:

```
with new memory:
    line = ask for a line
    add line to names        # names outlives the memory
show line                    # line's memory is gone
```

The checker reports a value made inside the block when it is stored
somewhere that outlives the block: a variable, list or record from
outside it, or the function's result. It also reports reading the value
after the block ends (variables are hoisted to the function, so `line` is
still a name there):

```
ovis: notes.ovis, line 4: `line` is made in the new memory of line 2, which
is freed when that block ends; it can't be added to `names`, which is from
outside it. Write `add keep line to names` to keep a copy.
```

Both checks are a forward pass over each function. They mark each local
with where its value may come from, and the §2 rule means every new value
starts in a local or a place the pass can see. They don't follow values
through calls (a phrase that adds its argument to a global list), or
values in a named memory, since `free m` may happen anywhere. Checked mode
catches those when the program runs (§6).

## 5. New memory

**Block form.** This is the common case:

```
for each path in paths:
    with new memory:
        text = read the file path        # a phrase of the program
        words = number of words in text
        show words
```

`with new memory:` makes an arena, makes it the current memory for the
block, and frees it on every way out: falling off the end, `give back`,
`stop`, `skip`, the end of the program. It uses the same exit frames as
`at the end` (architecture-spec §8). Blocks nest, and the inner memory is
freed first. A list's storage always grows in the memory the list was
made in, so `add` inside a block never leaves an outside list's storage
in the block's memory.

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

A named memory is a value of type `memory`. It can be passed to a phrase,
or kept in a record next to what it holds (a parsed document and its
memory). `clear m` frees what it holds and keeps its pages for reuse,
which is Zig's `arena.reset(.retain_capacity)`.

**Keeping a value.** `keep x` copies x into the memory that was current
*outside* the innermost `with` block:

```
names = empty list of text
while true:
    with new memory:
        line = ask for a line
        if line is nothing:
            stop
        add keep line to names
```

The copy is deep for values made in the block's memory (text, lists,
records, and what they hold), and values reachable twice are copied once,
so sharing and cycles survive. Values from outside the block are not
copied, because they already live long enough. Outside any `with`, `keep
x` copies into the program's memory.

## 6. Checked mode

Programs are **checked by default**, as in Zig's Debug mode. `ovis run
--fast` and `ovis build --fast` turn the checks off.

In checked mode the runtime keeps a table of every value Ovis made: where
it starts and ends, its memory, the line that made it, and the line that
freed it. Freed blocks are filled with a pattern and held back from reuse
for a while, so a stale pointer stays recognisable. Every list access,
field access and text operation checks its value:

| Error | Message (example) |
| --- | --- |
| use after free | `names (line 9) holds text freed on line 7; it was made on line 5` |
| double free | `line was already freed on line 7` |
| freeing the middle of a value | `free was given a place inside text made on line 5, not the text itself` |

The generated code keeps the current line in a global (`ovis_line`), set
at each statement in checked mode only, so runtime errors can name lines.
Every runtime error gains the line this way, including today's list-index
error.

`--report-leaks` lists, at exit, each value in the program's memory that
was never freed, by where it was stored (§2):

```
ovis: `label` (line 3): 10000000 texts never freed
```

The report is opt-in. Leaving top-level values until the end of the
program is a legitimate choice, as in a Zig program that frees its arena
at exit, and most short programs do just that.

In fast mode there is no table, `free` frees straight away, and a
use-after-free is undefined, as in C or Zig's ReleaseFast.

## 7. C libraries

- Memory a C function returns is the author's, and the same `free` frees
  it: `copy = strdup(name)` then `at the end, free copy`. This settles the
  P2 ownership task: C ownership follows the C library's documentation,
  with no import magic. C results are not new values under §2, since
  Ovis can't tell `strdup` from `getenv`.
- C pointers into Ovis text are fine: `fgets(line, 256, file)` returns
  `line` itself, and `strchr` a place inside text. They are valid while
  the text is.
- Ovis values given to C are the author's to keep alive while C uses
  them, for example text bound with SQLite's `SQLITE_STATIC`.
- `free` on a C pointer calls C's `free`. Checked mode knows which
  pointers Ovis made, so it never mixes the two up.

## 8. Implementation

**Runtime** (`runtime.cpp`):
- `ovis_memory` is either the program's memory (`malloc`-backed) or an
  arena of chunks, which starts at 4 KB and doubles. Memories form a
  stack: `ovis_memory_push`/`pop`, `ovis_memory_new`/`free`/`clear`.
- Every allocation goes through `ovis_alloc(size)` on the current memory,
  except list growth, which uses the list's own memory (stored in its
  header).
- `ovis_free_text`, `ovis_free_list` and `ovis_free_record`: a value in an
  arena, or text from the binary's constants, is left alone (checked mode
  marks an arena value freed), and anything else is passed to `free`. In
  fast mode, telling these apart takes a walk over the live arenas' chunk
  ranges, and there are few of them. Constants are in their own section.
- `ovis_keep(value, kind)`: the deep copy, with a pointer map.
- `ovis_join(parts…)`: one allocation for a whole join (§2).
- Checked mode (`-DOVIS_CHECKED` when the runtime is compiled): the value
  table (a hash map by start address, plus a sorted index for "inside a
  value"), the poison pattern, the reuse delay, and `ovis_check_value`.

**Ovis Core:** a type `memory`, and the forms `(free X)`, `(new-memory)`,
`(with-memory M BODY...)`, `(with-new-memory BODY...)`, `(clear-memory
M)`, `(keep X)`. The elaborator lowers `with-new-memory` to `new-memory`,
`with-memory` and a `defer` that frees the memory. Codegen lowers
`with-memory` to a push and a pop in the block's exit frame. The checker
types `free` and runs §2 and §4; the elaborator marks functions new,
existing or mixed.

**Phrases:** `free (x)`, `(name) = new memory`, `with new memory:`, `with
(m):`, `clear (m)` and `keep (x)`. None of them is ambiguous.

**Library and programs.** The prelude must follow §2 itself and free its
own temporaries (`prelude_list_text` joins item after item). Examples and
eval programs are rewritten where they make values inline, for example
`show line trimmed` in `eval/programs/table.ovis`:

```
for each row in the numbers from 1 to 5:
    with new memory:
        line = ""
        for each column in the numbers from 1 to 5:
            line = line + (row * column as text) + " "
        trimmed_line = line trimmed
        show trimmed_line
```

## 9. Considered and rejected

- **Garbage collection** (Boehm GC, embedded; an earlier draft). It needs
  no code from the author and handles cycles and C pointers, but it frees
  when the collector decides. The author chose explicit memory.
- **Reference counting.** Also implicit. It leaks cycles, and its header
  before the characters conflicts with text being a plain `char *` shared
  with C.
- **Ownership and borrowing** (Rust). It is explicit, but it needs
  lifetime words and fights Ovis's shared references.
- **Inferred regions** (MLKit). Not explicit: the compiler chooses the
  regions.
- **An allocator parameter on every function** (Zig). Every phrase would
  grow a slot. The current memory is as explicit about *when* memory is
  freed, and §2 is as explicit about *what* is made.
- **Copying an escaping value automatically** (§4). That would be a hidden
  allocation.
- **Finalizers that close C handles.** These are hidden and late. Use
  `at the end`.

## Decisions

Agreed with the author (2026-10-19):

1. **Strict:** no hidden new values. Each is made as the value a statement
   stores (§2). Joins count as one value.
2. **Wording:** `free x`, `with new memory:`, `(name) = new memory`, `with
   m:`, `clear m`, `keep x`.
3. **Checked by default,** with `--fast` to turn checks off (§6).
4. **`free` is shallow** (§3).
5. **A value escaping its memory is a compile error** that suggests
   `keep` (§4).
6. **The leak report is opt-in,** `--report-leaks` (§6).

Added while writing it down: `free` of a text written in the program does
nothing (§3), and a phrase that gives back both new and existing values is
an error (§2).
