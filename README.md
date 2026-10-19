# OVIS

> [!CAUTION]
> This is an experiment. The language is changing.

Ovis is a programming language whose source code is plain English. You write
lines made of phrases, the way you'd say them, and Ovis compiles them through
LLVM into a native executable that can use any C library.

```text
# Reads some whole numbers and reports the smallest, the largest and the
# average.
count = ask "How many numbers?" for a whole number
numbers = empty list of whole numbers
repeat count times:
    add (ask for a whole number) to numbers

show "Numbers:", numbers
show "Smallest:", smallest of numbers, "Largest:", largest of numbers
show "Average:", mean of numbers rounded to 2 decimals
```

A deterministic parser reads every line. "mean of numbers" is no phrase Ovis
knows, so it lists the readings that make sense there, and a small language
model built into the compiler picks one. Ovis writes the line as it reads
it just above, as a `@[meaning: …]` sigil, where you can see it and change
it:

```text
@[meaning: show "Average:", average of numbers rounded to 2 decimals]
show "Average:", mean of numbers rounded to 2 decimals
```

The model never writes code: it chooses between readings that already
type-check. When none fits, it may write the line again in words Ovis
reads, kept only if the line then reads and keeps every name and value.
When it isn't sure, the build stops and asks you:

```text
$ ovis run examples/vague.ovis
examples/vague.ovis, line 4: "show square root of x + 7" is unclear. Did you
mean one of these? Write it in the source:
    show ((square root of x) + 7)
    show square root of (x + 7)
```

## How it works

```text
 your lines ──► chart parser ──► one reading ─────────────────► Ovis Core ──► LLVM ──► executable
                (phrases, types)   │                               ▲
                                   └─ none, or several: a question ┘
                                      @[meaning] / lockfile / rule /
                                      ranking / the model chooses / you decide
```

1. **Lines and blocks.** A line ending in `:` opens a block indented under it,
   as in Python. `#` starts a comment.
2. **Phrases.** About 90 built-in phrases (`average of numbers`, `add x to
   list`, `text in capitals`, `x rounded to 2 decimals`, ...) are templates
   with typed slots, each standing for one Ovis Core form. Articles are
   optional, and singular and plural match. Your own phrases are defined with
   `to`. Operators (`+ - * / mod = != < > <= >= and or not`) work as in
   Python.
3. **Questions.** A line that reads several ways, or uses words no phrase
   has, becomes a question with a few candidate readings, each one the line
   reads and type-checks with. In order, what settles it:
   - a `@[meaning: …]` sigil above it (yours or Ovis's; the lockfile
     notices when Ovis's was for a line you have since edited, and asks
     again);
   - the lockfile;
   - a rule: the one name a misspelling is close to, the one value `it` can
     mean;
   - the ranking, when one reading clearly shares the line's words;
   - the model, if it is confident (built in, using llama.cpp);
   - you: the build stops and lists the readings.
4. **Compilation.** The readings become Ovis Core, which is type checked as
   a whole program, then turned into LLVM IR, optimised, and linked with a
   small runtime (lists, text, input) and libc.

Everything the model chose is also kept in `<file>.lock`. Commit it with the
source: builds then never need the model, and building again changes no file.

## Getting started

You need CMake 3.20+, a C++23 compiler, LLVM (tested with 18) and a C compiler
for linking. llama.cpp is fetched and built with Ovis; libclang is optional
(for C libraries).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/test_ovis

# The examples have their readings written in, so they need no model:
./build/ovis run examples/primes.ovis

# Get the default model: Qwen2.5-Coder-7B-Instruct (Q4_K_M, about 4.7 GB)
# where a GPU runs it, else 3B (about 2 GB). `model pull 3b` or `7b` to choose.
./build/ovis model pull
./build/ovis run my_program.ovis
```

Under CMake, `find_package(Ovis)` after `cmake --install build --component
ovis --prefix PREFIX` gives `ovis_add_executable(app main.ovis LINK mylib)`:
Ovis writes an object file and CMake links it with `mylib` and the runtime.
See `examples/cmake` and [docs/c-libraries.md](docs/c-libraries.md).

A binary you will run on other machines: add `-DOVIS_PORTABLE=ON` (no
CPU-specific tuning). Builds are the same across machines through the
committed `.lock` file and `@[meaning: …]` lines, not through the model:
another CPU or GPU may settle a close question the other way.


Where the weights come from, in order: `--model-path`, `$OVIS_MODEL_PATH`,
weights embedded in the `ovis` executable, then a default model next to
`ovis`, in `../share/ovis` or in the cache directory (`~/.cache/ovis/models`):
7B first where a GPU runs it, else 3B first.

**A GPU is the intended setup.** 7B reads wording far from the phrases'
much better than 3B (17.5 against 13.5 of 20 held-out cases, and 89%
against 68% of its confident picks right), but each question is a pass
over the prompt: on four CPU cores 3B takes about 2.3 s and 7B about 5.3 s,
while a GPU takes a fraction of a second. Macs use Metal with no setup. For
other GPUs, build llama.cpp's backend in:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DGGML_CUDA=ON    # NVIDIA
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DGGML_VULKAN=ON  # others (Vulkan SDK)
```

`--cpu` keeps the model off the GPU (and prefers 3B). Programs whose lines
are all phrases Ovis knows never load the model. The first build that
needs it starts a copy of `ovis` in the background that keeps it loaded and
quits after 15 minutes unused (`ovis model stop` sooner, `--no-daemon` to
load it in the build instead); that saves the loading, under a second from
the file cache, in every later build.

A build with the model already inside is made by the **Embedded model**
workflow in GitHub Actions. To make one yourself:

```sh
ovis model embed qwen2.5-coder-3b-instruct-q4_k_m.gguf -o ovis-with-model
# or at build time:
cmake -S . -B build -DOVIS_EMBED_MODEL=/path/to/model.gguf
```

A model server can be used instead: `--endpoint http://127.0.0.1:8080/v1`
for any OpenAI-compatible API that gives log-probabilities (llama-server,
...). Configure with `-DOVIS_BUILTIN_LLM=OFF` to build without llama.cpp.

## Usage

```text
ovis run   <file or folder>   compile and run
ovis build <file or folder>   compile to an executable (-o to name it)
ovis ir    <file or folder>   print the LLVM IR
ovis check <file or folder>   report problems; build nothing, write nothing
                              (--json for editors)
ovis fix   <file or folder>   choose what each unclear line means; written
                              above it as @[meaning: …]
ovis fmt   <file or folder>   put the @[…] lines in order (--check for CI)
ovis phrases [words…]         the built-in phrases with these words
ovis lsp                      serve an editor (VS Code: editors/vscode)
ovis model path | pull [3b|7b] | embed MODEL -o OUTPUT | stop

  --offline           never ask the model; unclear lines are reported
  --model-budget <s>  seconds the model may spend on questions (default 10)
  --keep-source       don't write @[meaning: …] lines into the source
  --no-prompt         in a terminal, report unclear lines instead of
                      offering their readings

In a terminal, a live line at the bottom shows what the build is doing
(the line being read, the words the model is asked about, linking) and
for how long; in a pipe or CI the output stays plain.
  -I <dir>, -L <dir>  where C headers and libraries are
  --cpu, --threads <n>, --model-path <gguf>, --endpoint <url>
  --no-daemon, --daemon-idle <minutes>
```

## Writing programs

```text
# Finds the prime numbers below 50.
to (n) is prime:
    if n < 2:
        give back false
    divisor = 2
    while divisor * divisor <= n:
        if n is divisible by divisor:
            give back false
        divisor = divisor + 1
    give back true

primes = empty list of whole numbers
for each n in the numbers from 2 to 49:
    if n is prime:
        add n to primes
show primes
show "Found", length of primes, "primes"
```

- **Variables:** `name = value`. Types are worked out for you.
- **Blocks:** `if … :`, `otherwise if … :`, `otherwise:`, `while … :`,
  `for each x in list:`, `repeat n times:`, `stop`, `skip`.
- **Your own phrases:** `to WORDS (parameter) WORDS …:` with `give back` for
  the result. Parameters may say their type: `(width: whole number)`,
  `(names: list of text)`. A phrase works on any line, before or after it is
  defined, as a statement or a value. It is also a function named by its
  words: `to pad (text) to (width):` is `pad_to(text, width)`; `as NAME`
  before the `:` names it yourself. Variables set in a phrase are its own.
- **Calls:** `name(arguments)` calls your phrases and C functions.
- **Unclear lines:** read the `@[meaning: …]` lines Ovis writes above
  them. Change one to change what the line means, or delete it to ask
  again. Writing the reading into the line itself works too. Lines written
  by older builds as `Read as:` under a line are still read, and rewritten.
- The phrases are in [`phrases/`](phrases/); every one has an example in
  [`test/phrase_library.txt`](test/phrase_library.txt).

## Programs with several files

A folder with a `main.ovis` is a program. Every other `.ovis` file in the
folder and its subfolders is a module (`pricing/tax.ovis` is
`pricing.tax`). `main.ovis` says what the program does; the other files
only define phrases, `use` C libraries and set constants, which every file
can then use.

```text
examples/shop/
  main.ovis               discounted = apply the discount to the total of basket ...
  pricing/discount.ovis   to apply the discount to (total): ...
  pricing/tax.ovis        to add tax to (amount: decimal): ...

$ ovis run examples/shop
After discount: 42.08
With tax: 45.44
```

## Using C libraries

```text
use "sqlite3.h" from "sqlite3"
db = null
if sqlite3_open(":memory:", ref db) = SQLITE_OK:
    show "opened"
```

Ovis reads the header with libclang: its functions, enum values and numeric
`#define`s can be used on every line. `ref x` passes a variable for a C
function to write into. Include and link flags come from `pkg-config`, or
`-l<library>`; use `-I` and `-L` for other places. The C standard library
(`printf`, `strlen`, `sqrt`, `fopen`, ...) needs no `use`. Not supported yet:
structs passed by value, callbacks other than `null`, function-like macros.

## Ovis Core

What phrases stand for. You can ignore it unless you write phrase templates.

```lisp
; Types: int float bool str ptr, lists int[] float[] bool[] str[]; any type
; can be left out or written ?
(fn name (a (b float)) body...)              ; return type optional, may be void
(extern name (cint str ...) cint)            ; any other C function
(use-c "header.h" "library")                 ; import a C library
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

A template in `phrases/*.phr` looks like this:

```text
expr  average of {xs: int[]}   : float  => (/ (float (prelude_sum_int $xs)) (float (len $xs)))
      alias: average value of {xs}
stmt  add {x: T} to {xs: T[]}           => (append $xs $x)
```

## Layout

| Path | What |
| --- | --- |
| `src/ovis/phrase/` | the front end: lexer, templates, chart parser, questions, reading programs, lockfile |
| `phrases/` | the built-in phrases and the Ovis Core prelude behind them |
| `src/ovis/builtin_model.cpp`, `language_model.cpp` | in-process llama.cpp, the HTTP model client, `choose` |
| `src/ovis/sexpr.cpp`, `ast.cpp` | Ovis Core reader and parser |
| `src/ovis/elaborate.cpp`, `checker.cpp` | type inference, checking |
| `src/ovis/libc.cpp`, `c_import.cpp`, `modules.cpp` | the C standard library, C headers, modules |
| `src/ovis/llvm_codegen.cpp`, `runtime.cpp` | LLVM code generation and the runtime linked into every program |
| `examples/` | example programs |
| `eval/` | labelled questions and programs for `ovis_eval`, which measures the model |
| `docs/` | how it works (`architecture-spec.md`), state and plans |
