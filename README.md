# OVIS

> [!CAUTION]
> This is an experiment. The language is changing and it hasn't been tested much with real models yet.

Ovis is a programming language whose source code is plain English. It has
no syntax to learn. A small language model built into the compiler
translates what you wrote into a tiny strict language. A deterministic pass
fills in the details you left vague, and LLVM compiles the result into a
native executable that can use the C standard library.

```text
$ cat wordcount.ovis
Read lines of text from the user until the input ends.

For every line, count its words and its letters. A word is a run of letters
or digits.

At the end, report the number of lines, words and letters, and the average
word length with one decimal.

$ printf 'Hello world\nThe quick brown fox\n' | ovis run wordcount.ovis
Lines: 2, words: 6, letters: 26
Average word length: 4.3
```

## How it works

```text
 English ─► built-in LLM ─► Ovis Core ─► elaboration ─► checker ─► LLVM ─► executable
 paragraphs  (llama.cpp,      (may leave   (deterministic:  │        + C runtime
              grammar-         details     types, defaults, │        + libc
              constrained)     out)        small fixes)     │
                  ▲                                         │
                  └──────── problems: "fix it" ◄────────────┘
```

1. **Paragraphs.** A program is a text file. Blank lines split it into
   paragraphs, and that's the only structure there is.
2. **Translation.** The model runs inside `ovis` (llama.cpp, CPU, greedy
   decoding). It is told what the rest of the program defines and translates
   each paragraph into [Ovis Core](#ovis-core). A GBNF grammar constrains its
   output, so the reply is always well-formed.
3. **Elaboration.** The model is allowed to leave details out. A
   rule-based pass fills them in the same way every time:
   - types are inferred across the whole program; anything unconstrained is `int`;
   - a misspelled name is read as the single closest known name;
   - a function without `return` returns its last value;
   - a number used as a condition means "not zero";
   - a left-out trailing pointer argument of a C function becomes `null`;
   - printf arguments are converted to what the format shows.

   Every decision is recorded and shown by `ovis explain`.
4. **Checking.** The program is type checked as a whole. Paragraphs with
   problems go back to the model together with the problems. This time the
   grammar only allows functions that exist, so the model can't invent a
   name.
5. **Compilation.** LLVM IR, `-O2`, an object file, linked by the system C
   compiler with a small runtime (lists, text, input) and libc.
6. **Lockfile.** Translations are saved in `<file>.lock`. Unchanged paragraphs
   are never translated again, so builds are fast, reproducible, and work
   without the model. Commit the lockfile, and read or edit it when a
   translation is wrong.

The model is deterministic: the same model file and request give the same
reply on the same machine. The lockfile records a fingerprint of the
weights that made each translation.

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
ovis run <file>        translate, compile and run
ovis build <file>      translate and compile to an executable (-o <path>)
ovis translate <file>  show the Ovis Core and assumptions of every paragraph
ovis explain <file>    show only the assumptions
ovis ir <file>         print the optimized LLVM IR
ovis model path        show which model would be used
ovis model pull        download the default model
ovis model embed MODEL -o OUTPUT

--offline          only use the lockfile
--retranslate      ignore the lockfile
--attempts <n>     translation attempts per paragraph (default 3)
--model-path <f>   GGUF file for the built-in model
--threads <n>      CPU threads (default: up to 4)
--verbose          show llama.cpp's log
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
- Paragraphs that only explain something are fine; they turn into `(pass)`.
- Run `ovis explain` to see what Ovis assumed. If something is wrong, say it
  in the English. A changed paragraph gets translated again.

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
| `src/ovis/english.cpp` | paragraphs, lockfile, prompt and grammars, the HTTP model client |
| `src/ovis/compiler.cpp` | translate → elaborate → check → repair loop |
| `src/ovis/sexpr.cpp`, `ast.cpp` | Ovis Core reader and parser |
| `src/ovis/elaborate.cpp` | type inference and the other deterministic fill-ins |
| `src/ovis/checker.cpp` | name and type checking; its messages are written for the model |
| `src/ovis/libc.cpp` | the C standard library bindings and constants |
| `src/ovis/llvm_codegen.cpp` | LLVM IR generation, optimization, object emission, linking |
| `src/ovis/runtime.cpp` | the C runtime linked into every program |
| `examples/` | example programs with hand-written reference lockfiles |
| `tools/make_tiny_model.py` | makes a tiny random model for testing inference |

## Ideas

- Fine-tune a tiny model on English → Ovis Core pairs (the lockfiles are
  exactly that data), small enough to embed by default.
- Records/structs and maps.
- `(include "header.h")`: import C declarations with libclang instead of
  `extern`.
- Ask the user a clarifying question when a paragraph is ambiguous instead
  of guessing.
