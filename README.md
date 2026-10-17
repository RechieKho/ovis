# OVIS

> [!CAUTION]
> This is an experiment. The language is changing and it hasn't been tested much with real models yet.

Ovis is a programming language whose source code is plain English. It has
no syntax to learn. A small language model built into the compiler first
spells out what each paragraph means as concrete steps, and writes those
steps and every assumption it made into your source file, under your own
words. It then translates the steps into a tiny strict language, and LLVM
compiles the result into a native executable that can use the C standard
library.

You write:

```text
Read lines of text from the user until the input ends.
```

and after the first build the file says:

```text
Read lines of text from the user until the input ends.
    1. Make an empty list of texts named lines.
    2. Read a line from the user into line.
    3. While line is not null: add line to the end of lines, then read the next
       line into line.
    Assumption: The input has ended when reading a line gives null.
```

When a paragraph is too vague or isn't about the program at all, the build
stops and tells you why and how to fix it:

```text
$ ovis run examples/vague.ovis
Paragraph at line 3 is too vague to turn into a program:
  | Make the inventory system work the way a real business would want.
  Why: It doesn't say what the inventory holds or what the program should do with it.
  Try: List the stock and say what to work out, for example "The shop has 12 apples at 0.50 each and 3 melons at 2.00 each. Print the total value of the stock."

Paragraph at line 5 is not about what the program does:
  | My cat walked across the keyboard while I was writing this.
  Why: It is about the author's cat, not about what the program should do.
  Try: Remove this paragraph.
```

## How it works

```text
 your paragraphs ──► detail pass ──► steps + assumptions ──► translation ──► Ovis Core
                     (LLM)          written into the source   (LLM)            │
                       │                                                       ▼
                       ├─ only describes the program → kept as a note     elaboration
                       └─ too vague / irrelevant → build fails:           (deterministic)
                          why, and a suggested rewrite                         │
                                                                               ▼
                                     problems: "fix it" ◄── checker ──► LLVM ──► executable
```

1. **Paragraphs.** A program is a text file. Blank lines split it into
   paragraphs, and that's the only structure there is.
2. **Detail pass.** For each paragraph, the model (built in, using
   llama.cpp) decides one of four things:
   - it can be spelled out as concrete numbered steps, with every
     assumption it had to make listed;
   - it only describes the program;
   - it is too vague;
   - it is irrelevant.

   The steps and assumptions are written into the source file, indented
   under your words. They are yours to read and edit. Ovis makes sure they
   keep matching the sentence they belong to (see
   [Writing programs](#writing-programs)). A paragraph that is too vague or
   irrelevant stops the build with the model's reason and a suggested
   rewrite.
3. **Translation.** The model turns the steps (not the abstract paragraph)
   into [Ovis Core](#ovis-core), constrained by a grammar so the output is
   always well-formed.
4. **Elaboration.** A rule-based pass fills in what the translation still
   left out:
   - types, inferred across the whole program, with whole numbers when
     nothing says otherwise;
   - misspelled names, read as the closest known name;
   - implicit return values;
   - "not zero" conditions;
   - printf argument conversions.

   Its decisions are written into the source as `Compiler:` lines after
   each successful build.
5. **Checking and repair.** The program is type checked as a whole.
   Paragraphs with problems go back to the model, and this time the grammar
   only allows functions that exist. If one still fails after `--attempts`
   tries, the model explains in plain English why and suggests a rewrite.
   `--verbose` shows the code behind it.
6. **Compilation.** LLVM IR, `-O2`, an object file, linked by the system C
   compiler with a small runtime (lists, text, input) and libc.
7. **Lockfile.** Every verdict, step list and translation is saved in
   `<file>.lock`. Unchanged paragraphs never go back to the model, so builds
   are fast and reproducible, and a detailed program builds without the
   model at all. Commit both the source and the lockfile.

The model is deterministic: the same model file and request give the same
reply on the same machine. Rendering the detail into the source is
deterministic too, so building again changes nothing. The lockfile records
a fingerprint of the weights.

## Getting started

You need CMake 3.20+, a C++23 compiler, LLVM (tested with 18) and a C compiler
for linking. llama.cpp is fetched and built with Ovis.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/test_ovis

# The examples have committed lockfiles, so they work without a model:
./build/ovis run examples/primes.ovis
./build/ovis explain examples/wordcount.ovis

# Get the default model (Qwen2.5-Coder-1.5B-Instruct, Q4_K_M, about 1 GB):
./build/ovis model pull
./build/ovis run my_program.ovis
```

Where the weights come from, in order:
1. `--model-path`
2. `$OVIS_MODEL_PATH`
3. weights embedded in the `ovis` executable
4. the default model next to `ovis` or in `../share/ovis`
5. the cache directory (`~/.cache/ovis/models`)

To ship a single file with the model built in:

```sh
ovis model embed qwen2.5-coder-1.5b-instruct-q4_k_m.gguf -o ovis-with-model
# or at build time:
cmake -S . -B build -DOVIS_EMBED_MODEL=/path/to/model.gguf
```

Any instruct GGUF that llama.cpp supports works. Smaller models translate
worse but faster; elaboration and the repair loop make up for some of that.

A model server can be used instead: `--endpoint http://127.0.0.1:8080/v1`
for any OpenAI-compatible API (llama-server, Ollama, ...). Configure with
`-DOVIS_BUILTIN_LLM=OFF` to build without llama.cpp.

## Usage

```text
ovis run <file|folder> translate, compile and run
ovis build <file>      translate and compile to an executable (-o <path>)
ovis translate <file>  show every paragraph's steps, assumptions and Ovis Core
ovis explain <file>    show every paragraph's steps and assumptions
ovis ir <file>         print the optimized LLVM IR
ovis model path        show which model would be used
ovis model pull        download the default model
ovis model embed MODEL -o OUTPUT

-I <dir>, -L <dir>  where to find C headers and libraries
--offline          only use the lockfile
--retranslate      ignore the lockfile
--attempts <n>     translation attempts per paragraph (default 3)
--keep-source      don't write the detail into the source file
--verbose          show the code behind failures, and llama.cpp's log
--model-path <f>   GGUF file for the built-in model
--threads <n>      CPU threads (default: up to 4)
--endpoint <url>   use a model server instead ($OVIS_ENDPOINT)
--model <name>, --api-key <key>, --no-grammar   options for the server
```

## Writing programs

Write what you want the program to do, the way you'd explain it to a person.

- Put one idea in each paragraph: a function ("To convert Celsius to
  Fahrenheit, ..."), some data ("The scores are 90, 72 and 85."), or a step
  ("Print every score above the average.").
- Refer back to earlier things by the same name.
- Mention C functions when you want something specific ("print it with
  printf using two decimals", "open the file with fopen").
- Paragraphs that only describe the program are fine; they are kept as
  notes.
- After a build, read the steps and assumptions under each paragraph. If
  something is wrong, change it. Steps never outlive the sentence they
  were written for:

  | You change | Ovis does |
  | --- | --- |
  | the steps (the sentence stays) | asks the model to check your steps still do what the sentence says, without rewriting them; refreshes the assumptions, so stale ones go; stops with the reason if a step the sentence needs is missing or one is unrelated |
  | the sentence (steps left as they were, edited or not) | writes the steps again, keeping what still fits the new sentence and dropping steps and assumptions for anything it no longer asks for |
  | deletes the sentence, leaves its steps | removes the leftover steps and shows what it removed; steps Ovis never wrote are not touched, the build stops and asks instead |
  | the `Assumption:` lines only | nothing; they are notes for you |

- To have a paragraph detailed again from scratch, delete the indented lines
  under it and build with `--retranslate`.

## Programs with several files

A folder with a `main.ovis` is a program. Every other `.ovis` file in the
folder and its subfolders is a module, and subfolders nest:
`pricing/tax.ovis` is the module `pricing.tax`. `main.ovis` says what the
program does; the other files only define things (functions and data).
Names from other files can be used as they are. When two files define the
same name, say which one, e.g. "the add_tax from tax". Each file keeps its
own lockfile.

```text
examples/shop/
  main.ovis               "Print the basket's total after the discount, ..."
  pricing/discount.ovis   "Orders of 40 or more get 10% off. ..."
  pricing/tax.ovis        "The tax rate is 8%. ..."

$ ovis run examples/shop
After discount: 42.08
With tax: 45.44
```

## Using C libraries

Say which library you want ("Use SQLite to store the scores."). The detail
pass writes it as a step you can see and edit: "Use the C library sqlite3
(header sqlite3.h)."

Ovis reads that header with libclang, and makes the header's functions,
enum values and numeric `#define` constants available. The model is shown
the ones that match what the paragraph asks for.

Include and link flags come from `pkg-config`, or `-l<library>`. Use `-I`
and `-L` for libraries in other places. C functions that write results
through a pointer (`sqlite3_open(path, &db)`) take `(ref variable)`. Not
supported yet:
- structs passed by value;
- callbacks other than `null`;
- function-like macros.

## Ovis Core

This is what the model writes. You can ignore it unless you want to read a
translation.

```lisp
; Types: int float bool str ptr, lists int[] float[] bool[] str[]; any type
; can be left out or written ?
(fn name (a (b float)) body...)              ; return type optional, may be void
(extern name (cint str ...) cint)            ; any other C function
(link "z")                                   ; link a C library
(let name value)  (set name value)  (put list index value)
(append list value) (sort list) (reverse list)
(print value ...)
(if condition (then ...) (else ...))
(while condition body...)
(for i first last body...)                   ; first..last, both included
(each item list body...)
(break) (continue) (return value) (pass)

(+ a b ...) (- a b) (* a b) (/ a b) (div a b) (% a b)  ; / gives a float, div an int
(= a b) (!= a b) (< a b) (<= a b) (> a b) (>= a b) (and a b) (or a b) (not a)
(int x) (float x) (sqrt x) (pow x y) (floor x) (abs x) (min a b) (max a b)
(list 1 2 3) (list) (array int n) (get list i) (len list)
(+ "text" 42) (concat a b) (substring s start count) (char_at s i) (chr n) (to_str x) (buffer n)
(read_int) (read_float) (read_line) (random low high)
null EOF pi stdin stdout stderr RAND_MAX CLOCKS_PER_SEC
```

**C standard library.** About 100 functions can be called directly:
- math: `sin`, `log`, `round`, `fmod`, …
- stdlib: `atoi`, `rand`, `getenv`, `system`, `exit`
- string: `strlen`, `strcmp`, `strstr`, `strtok`, …
- ctype: `isdigit`, `toupper`, …
- stdio: `printf`, `fprintf`, `snprintf`, `fopen`, `fgets`, `fputs`, …
- time: `time`, `clock`, `difftime`

Conversions between Ovis and C types happen automatically. For example, C
`int` is 32 bits and Ovis `int` is 64. printf-family calls with a written-out
format are checked against their arguments. Anything else can be declared
with `extern`.

Top-level statements run in paragraph order when the program starts.
Top-level variables are globals, so every function can use them. Functions
can be used before the paragraph that defines them. Lists have reference
semantics, grow with `append`, and are bounds-checked. Integer division by
zero is a runtime error.

## Layout

| Path | What |
| --- | --- |
| `src/ovis/builtin_model.cpp` | in-process llama.cpp inference, model lookup and embedding |
| `src/ovis/english.cpp` | paragraphs, detail written into the source, lockfile, the HTTP model client |
| `src/ovis/prompt.cpp` | prompts and grammars for the detail pass, translation and explanations |
| `src/ovis/compiler.cpp` | detail → translate → elaborate → check → repair → explain |
| `src/ovis/sexpr.cpp`, `ast.cpp` | Ovis Core reader and parser |
| `src/ovis/elaborate.cpp` | type inference and the other deterministic fill-ins |
| `src/ovis/checker.cpp` | name and type checking; its messages are written for the model |
| `src/ovis/libc.cpp` | the C standard library bindings and constants |
| `src/ovis/llvm_codegen.cpp` | LLVM IR generation, optimization, object emission, linking |
| `src/ovis/runtime.cpp` | the C runtime linked into every program |
| `examples/` | example programs, already detailed, with hand-written reference lockfiles (`vague.ovis` shows a rejected program) |
| `tools/make_tiny_model.py` | makes a tiny random model for testing inference |

## Ideas

- Fine-tune a tiny model on English → Ovis Core pairs (the lockfiles are
  exactly that data), small enough to embed by default.
- Records/structs and maps.
- `(include "header.h")`: import C declarations with libclang instead of
  `extern`.
- Ask the user a clarifying question when a paragraph is ambiguous instead
  of guessing.
