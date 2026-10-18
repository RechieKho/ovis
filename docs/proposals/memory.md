# Proposal: freeing memory

Status: **draft, for the author to decide** (the open questions are at the
end). It answers the P1 task "lists and text are never freed" in
[remaining-tasks.md](../remaining-tasks.md), and touches the P2 task on
memory C functions return.

## Why

Ovis allocates lists, records and text on the heap and never frees them.
Short programs don't notice: the operating system takes everything back at
exit. A loop that makes text does notice:

```
count = 0
while count < 10000000:
    label = "item " + count
    count = count + 1
show count
```

Each pass allocates two texts (`count` as text, then the join) and keeps
both. Built with `ovis build` on Linux, this program peaks at **783 MB**
(measured 2026-10-18). It needs a few hundred bytes. A server, a game loop
or a long file filter runs out of memory the same way.

Authors write English, not allocation code. Ovis should free memory itself,
without `free` lines or ownership words in the source.

## 1. What Ovis allocates today

All allocation is in the C runtime (`runtime.cpp`). The generated code
only calls it.

| Value | Allocated by | Holds pointers? |
| --- | --- | --- |
| list header `{length, capacity, data}` | `ovis_list_new` (`malloc`) | yes (`data`) |
| list items | `ovis_list_new` (`calloc`), `ovis_list_push` (`realloc`) | for lists of text, records or trait values |
| record | `ovis_record_new` (`calloc`) | when a field is text, a list, a record or a C pointer |
| text | `ovis_concat`, `ovis_substring`, `ovis_int_to_str`, `ovis_float_to_str`, `ovis_chr`, `ovis_read_line` (`malloc`) | no |
| `buffer(n)` | `ovis_buffer` (`calloc`) | no |
| text literals, `true`/`false` as text | constants in the binary | no |
| numbers, booleans, locals | the stack (`create_entry_alloca`) | n/a |

## 2. What any scheme must handle

These facts about Ovis decide which schemes work:

1. **Sharing is the rule.** Lists and records are by reference: a copy, an
   argument and a list item all point at the same record
   (architecture-spec §6). A trait value is the record pointer itself. So
   one value often has many owners, and nothing in the source says which
   one is last.
2. **Cycles are possible.** A record may hold records of its own kind (a
   tree with parent links, a doubly linked list). A record can also reach
   itself through a list.
3. **Text is a plain `char *`, and it goes back and forth with C.**
   - C returns pointers *into* Ovis text: `fgets(line, 256, file)` returns
     `line`, `strchr` and `strstr` return a place inside their argument,
     and `strtol(text, (ref end), 10)` writes one into `end`.
   - C may keep Ovis text after the call returns: SQLite's
     `SQLITE_STATIC`, `setvbuf`, `putenv`.
   - C returns its own memory, either static (`getenv`) or memory the
     caller must `free` (`strdup`, `sqlite3_mprintf`).
   - Text may also be passed where C takes any data pointer.
4. **Text literals are constants** and can't be freed.
5. **No threads.** Ovis has none, and the runtime does not need to be
   thread-safe.
6. **Linking needs only a C compiler.** The runtime is embedded as C
   source and compiled by `cc` when a program is linked (§8). A new
   dependency must not break `ovis build` on a machine with just `cc`.
7. **Output must not change.** The program prints the same things with
   any scheme. Only how much memory it uses changes.

## 3. Options

### A. Leak (today)

Keep allocating and never free. This costs nothing and it is wrong for any
long loop (§Why). It stays available as the fallback (§4, "Switching it
off").

### B. Regions or arenas

Allocate into an arena per block, loop pass or function, and free the
whole arena when it ends. This is what I suggested first, and it doesn't
fit Ovis:

- Values escape all the time because of point 1: `add x to items`, `set
  the owner of b to p`, `give back x` and a global each move a value out
  of the block that made it. Freeing the pass's arena would free a record
  that a list outside the loop still holds.
- Inferring which region a value belongs to (Tofte–Talpin region
  inference, as in MLKit) is a research problem. Programs that build a
  list in a loop defeat it, and that is the usual Ovis program.
- Asking the author to name regions breaks "authors write English".

What survives is an optimisation for **statement temporaries**: the
`count` text in `"item " + count` dies at the end of its statement. That
only covers temporaries, and §4 makes it unnecessary.

### C. Reference counting (Swift, CPython)

Each heap value carries a count. The generated code increases the count
when it stores a pointer and decreases it when the pointer is overwritten
or goes out of scope. The value is freed at zero.

For:
- Frees promptly and deterministically, with no pauses.
- No new dependency.
- The `defer` frames in `generate_block` already reach every exit of a
  block (§8), so releases at scope exit have a place to go.

Against:
- **Text and C (point 3).** The count has to sit in a header before the
  characters. A `char *` that C made, or one pointing inside Ovis text,
  has no header, and reading one there is undefined. So every `char *`
  from C would have to be copied into Ovis text when it arrives. Then
  `line = fgets(line, …)` makes a copy, `strtol`'s `end` gets a copy
  through `ref`, and `free(strdup(x))` frees the wrong pointer. The P2
  ownership task gets harder, not easier.
- **Cycles leak (point 2)**, unless we add a cycle collector (as CPython
  did) or weak references (as Swift did, which is an ownership word in
  the source).
- **It is the largest change.** Every `let`, `set`, `put`, `set-field`,
  argument, `give back`, temporary, `break`, `continue` and implicit
  return has to retain or release in the right order. A mistake shows up
  as a use-after-free in the author's program, which is the worst kind of
  compiler bug for a beginner to meet.
- It costs time on every pointer store, including list writes in tight
  loops.

### D. A conservative tracing collector: Boehm GC (bdwgc)

Replace `malloc` in the runtime with the Boehm–Demers–Weiser collector
(`GC_MALLOC`). The collector scans the stack, registers and global data
for anything that looks like a pointer into its heap, and frees what is
unreachable. It is used by GNU Guile, Mono, Inkscape and the GCC Java
runtime, and has been maintained since 1988.

For:
- **The generated code doesn't change.** Only the runtime's allocation
  calls and `ovis_init` change. LLVM IR, the checker and the phrase layer
  are untouched.
- **C interop just works (point 3).** Pointers from C into Ovis text keep
  that text alive, because interior pointers are recognised by default.
  Memory that C allocated isn't in the GC heap, so the collector ignores
  it and `at the end, free(x)` still works for it.
- **Cycles are collected (point 2).**
- Allocation is fast, often faster than `malloc`. Text, buffers and lists
  of numbers can use `GC_MALLOC_ATOMIC`, which is never scanned.

Against:
- **A new dependency.** §4 says how to keep "only `cc` is needed" (point 6).
- **Memory C keeps but no Ovis variable holds (point 3).** The collector
  doesn't scan memory C allocated, so text handed to `sqlite3_bind_text`
  with `SQLITE_STATIC` can be freed while SQLite still uses it. The rule
  is that Ovis must hold a value as long as C uses it. Most C APIs copy
  what they are given (SQLite's `SQLITE_TRANSIENT`, `fputs`, zlib), so
  this rarely comes up. Reference counting has the same problem.
- **Conservative.** A number that happens to look like a heap address
  keeps a block alive. On 64-bit systems this is rare, and it can only
  waste memory, never break a program.
- Collection pauses. These are short at Ovis program sizes, and Ovis has
  no real-time goals.

### E. Ovis's own collector

Write a small conservative mark-and-sweep (about 400 lines of C) into the
runtime string. It keeps the runtime a single C file. It also means
re-solving finding the stack bottom, spilling registers (`setjmp`), and
finding data-segment bounds per platform (`etext`/`end` on Linux,
`getsectbyname` on macOS), which bdwgc already handles with decades of
testing. This is the fallback if the author rejects embedding bdwgc. A
*precise* collector (LLVM statepoints or a shadow stack, since Ovis knows
every type) is more work still, and would only be worth it if
conservative retention is measured as a real problem.

### F. Ownership and borrowing (Rust)

Ownership and borrowing would need words in the source (`moves`,
`borrows`, lifetimes) and errors about them. Ovis's reference semantics
(point 1) would have to change. Rejected: it fights the language.

### Summary

| | A leak | B regions | C ref counting | D Boehm | E own GC |
| --- | --- | --- | --- | --- | --- |
| Long loops stay small | no | partly | yes | yes | yes |
| Cycles | leak | n/a | leak | collected | collected |
| C pointers into text | fine | unsafe | copy at boundary | fine | fine |
| Codegen changes | none | large | largest | none | none |
| New dependency | none | none | none | bdwgc | none |
| Frees promptly | never | at block end | yes | at collection | at collection |
| Risk of use-after-free bugs | none | high | high | low | medium |

## 4. Recommendation: Boehm GC, embedded

Use **D**. It is the smallest change that fixes the task. It leaves the
compiler's front and middle untouched, and it is the only option that
handles both cycles and Ovis's text-is-`char *` interop without new
rules.

### Allocation

| Value | Call |
| --- | --- |
| list header, list items holding text/records/trait values, record | `GC_MALLOC` |
| text, `buffer`, items of `int[]` `float[]` `bool[]` | `GC_MALLOC_ATOMIC` (never scanned, and must be cleared where the code relies on zeroes: `buffer`, new list items) |
| growing a list | `GC_REALLOC` |
| `ovis_init` | `GC_INIT()` first |

`ovis_list_new` gets a third argument, whether its items hold pointers.
The code generator knows that from the list's element type. Records are
always scanned. A record whose fields are all numbers could be atomic, but
that is a later optimisation.

Globals are LLVM globals in `.data`/`.bss`, and Boehm scans those by
default. Locals in `alloca`s and registers are on the stack, which it also
scans. Interior pointers stay on (the default), so the derived pointers
that `-O2` makes, and pointers from C into text, keep their block alive.

### Delivery (point 6)

- Pin bdwgc through `FetchContent`, like llama.cpp, and build it as a
  **static library** with threads off (`-Denable_threads=OFF`, no parallel
  mark) and no C++ support. Record the version in state.md's pinned
  dependencies.
- Embed the built `libgc.a` in the `ovis` binary as bytes, the way
  `runtime_source()` embeds the runtime. Write it next to the object file
  at link time, and add it to the `cc` command after the runtime. Linking
  still needs only `cc`. Programs are linked statically against the
  collector, so they have no new run-time dependency either.
- The bytes are built for the host. Cross-compiling isn't supported today,
  so nothing is lost.

### Switching it off

The runtime calls `ovis_alloc`, `ovis_alloc_atomic` and `ovis_realloc`
instead of `malloc`. A preprocessor flag decides what those are:

- with the collector (default): the `GC_` calls;
- without it: `malloc`/`calloc`/`realloc`, which is today's behaviour.

`ovis build --no-gc` (and a CMake option `OVIS_GC=OFF` for platforms
bdwgc doesn't build on) links without the collector. This keeps A as an
escape hatch and lets tests compare both.

### C libraries

Add to [c-libraries.md](../c-libraries.md):

- Memory C allocates is never collected. Free it with `at the end,
  free(x)` as today (the P2 ownership task is unchanged).
- Ovis values given to C are kept alive only while an Ovis variable, list
  or record holds them. If C keeps a pointer after the call (SQLite's
  `SQLITE_STATIC`, `setvbuf`), keep the value in a variable for as long.
- Never call `free` on Ovis text or lists. Today that frees memory Ovis
  may still use. With the collector it corrupts the collector's heap.

### What stays the same

- `at the end` stays the way to close C resources. The collector runs **no
  finalizers**: closing a file when its handle becomes unreachable would be
  hidden, late and nondeterministic. That is the opposite of "nothing is
  hidden".
- Program output, `Read as:` lines and lockfiles don't change.
- No new phrases or words.

## 5. Plan

- **G1. Measure.** Add the §Why loop as `eval/programs/` or a test fixture,
  with a variant that builds and drops a list of records per pass and one
  that keeps a parent-linked cycle. Add `OVIS_MEMORY_REPORT=1`, which makes
  the runtime print peak heap use to stderr at exit
  (`GC_get_heap_size`, or `getrusage` without the collector). Record the
  leak baseline in state.md.
- **G2. Runtime allocation seam.** Route every runtime allocation through
  `ovis_alloc`, `ovis_alloc_atomic` and `ovis_realloc`, and give
  `ovis_list_new` the holds-pointers argument from codegen. Still
  `malloc`-backed, with no change in behaviour, and all tests pass.
- **G3. bdwgc.** Fetch, build, embed and link it, call `GC_INIT` and add
  `--no-gc`. CI on Linux and macOS. Test that the §Why loop stays under a
  fixed bound (a few MB) and that every example prints the same with and
  without the collector.
- **G4. C boundary.** Tests for `fgets` into a `buffer`, `strchr` into
  text, `strtol` with `(ref end)`, and a C library keeping text across
  calls (in `test/clib`), all run with collections forced
  (`GC_gcollect()` through an `OVIS_GC_STRESS=1` switch in the runtime).
  Document the rules in c-libraries.md.
- **G5. Docs.** Update architecture-spec §8 (runtime and linking), tick
  the P1 task and add a history line.

Each step is a commit that leaves the suite green. G2 is worth doing even
if the author picks another option, since every scheme needs the seam.

## 6. Left out

- **Freeing statement temporaries early** (§3 B). The collector reclaims
  them. Revisit if G1's measurements show collection time matters.
- **Precise collection** (§3 E). Revisit if conservative retention is
  measured as a problem.
- **Finalizers for C handles.** See §4, "What stays the same".
- **Weak references** and any memory words in the source.
- **Threads.** If Ovis gains them, bdwgc has a threaded build, and
  `GC_INIT` and thread registration go with them.
- **The P2 task on memory C returns.** It is unchanged by this proposal and
  is decided separately.

## Open questions for the author

1. **Boehm (D) or reference counting (C)?** I recommend D. Pick C if
   prompt, deterministic freeing matters more than the C-interop rules and
   the larger codegen change. In that case this proposal needs a §4 of its
   own.
2. **Embed `libgc.a` in `ovis`, or require a system bdwgc** (`pkg-config
   bdw-gc`, `libgc-dev`)? I recommend embedding, so linking still needs
   only `cc`.
3. **Collector on by default, with `--no-gc`?** I recommend yes.
4. **C keeping Ovis values (§4 C libraries): documentation only, or a
   phrase** (for example `keep (x) for C`) that registers a root with
   `GC_add_roots`? I recommend documentation first, and a phrase only if a
   real program needs it.
