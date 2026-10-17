# OVIS

> [!CAUTION]
> This is an experiment. The language is changing and it hasn't been tested much with real models yet.

Ovis is a programming language whose source code is plain English. It has
no syntax to learn. A small local language model translates what you wrote
into a tiny strict language, and LLVM compiles that into a native executable.

```text
$ cat primes.ovis
A number is prime when it is at least 2 and none of the numbers from 2 up to
its square root divide it evenly.

Collect every prime below 50 into a list. Print the list, and then print how
many primes were found.

$ ovis run primes.ovis
ovis: Translating the paragraph at line 1...
ovis: Translating the paragraph at line 4...
[2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47]
Found 15 primes
```

## How it works

```text
 English paragraphs ──► small LLM ──► Ovis Core ──► checker ──► LLVM IR ──► executable
                          ▲                            │
                          └──── problems, fix it ◄─────┘
```

1. **Paragraphs.** A program is a text file. Blank lines split it into
   paragraphs, and that's the only structure there is. Each paragraph is
   translated on its own, which keeps the work small enough for a small
   model.
2. **Translation.** The model is told what the rest of the program has
   already defined (functions, global variables) and asked to translate the
   paragraph into [Ovis Core](#ovis-core). With llama.cpp the reply is
   constrained by a GBNF grammar, so it is always well-formed.
3. **Checking.** All translations are parsed and type checked as one
   program. When a paragraph has problems (an unknown name, a type mismatch),
   the problems go back to the model so it can fix its translation, up to
   `--attempts` times.
4. **Compilation.** The checked program is lowered to LLVM IR, optimized at
   `-O2`, emitted as an object file and linked with a small C runtime (lists,
   printing, input) by the system C compiler.
5. **Lockfile.** Every translation is saved in `<file>.lock` next to the
   program. When you build again, unchanged paragraphs aren't sent to the
   model, so builds are fast, reproducible and work offline. The lockfile also
   shows exactly what the model understood. Commit it, and read or edit it
   when a translation is wrong.

The model never writes LLVM IR itself. It only has to pick from a few
dozen simple forms, and everything that turns those into machine code is
ordinary compiler code you can rely on.

## Getting started

You need CMake 3.20+, a C++23 compiler, LLVM (tested with 18) and a C compiler
for linking.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/test_ovis

# The examples have committed lockfiles, so these work without a model:
./build/ovis run examples/primes.ovis --offline
./build/ovis translate examples/temperature.ovis --offline
```

To write your own programs, run any OpenAI-compatible server. A small
coding model is enough to start with, for example with
[llama.cpp](https://github.com/ggml-org/llama.cpp):

```sh
llama-server -m qwen2.5-coder-1.5b-instruct-q4_k_m.gguf --port 8080
./build/ovis run my_program.ovis
```

Ollama works too (`--endpoint http://127.0.0.1:11434/v1 --model qwen2.5-coder:1.5b`),
but it ignores the grammar, so you depend more on the repair loop.

## Usage

```text
ovis run <file>        translate, compile and run
ovis build <file>      translate and compile to an executable (-o <path>)
ovis translate <file>  show the Ovis Core each paragraph became
ovis ir <file>         print the optimized LLVM IR

--endpoint <url>   OpenAI-compatible API (default $OVIS_ENDPOINT or http://127.0.0.1:8080/v1)
--model <name>     model to request ($OVIS_MODEL)
--api-key <key>    bearer token ($OVIS_API_KEY)
--offline          only use the lockfile
--retranslate      ignore the lockfile
--no-grammar       don't send the GBNF grammar
--attempts <n>     translation attempts per paragraph (default 3)
```

## Writing programs

Write what you want the program to do, the way you'd explain it to a person.
Some tips:

- Put one idea in each paragraph: a function ("To convert Celsius to
  Fahrenheit, ..."), some data ("The scores are 90, 72 and 85."), or a step
  ("Print every score above the average.").
- Refer back to earlier things by the same name. The model is told what has
  already been defined.
- Paragraphs that only explain something are fine; they turn into `(pass)`.
- If a paragraph is misunderstood, run `ovis translate`, then rephrase it.
  Changing a paragraph's text makes it get translated again. You can also fix
  the code in the lockfile by hand.

## Ovis Core

This is what the model writes. You can ignore it unless you want to read a
translation.

```lisp
; Types: int float bool str, and lists: int[] float[] bool[] str[]
(fn name ((param int) ...) float body...)    ; return type may be void
(let name value)  (let name type value)  (set name value)
(put list index value)  (append list value)
(print value ...)
(if condition (then ...) (else ...))
(while condition body...)
(for i first last body...)                   ; first..last, both included
(each item list body...)
(break) (continue) (return value) (pass)

(+ a b ...) (- a b) (* a b) (/ a b) (% a b)  ; int / int is integer division
(= a b) (!= a b) (< a b) (<= a b) (> a b) (>= a b) (and a b) (or a b) (not a)
(int x) (float x) (sqrt x) (pow x y) (floor x) (abs x) (min a b) (max a b)
(list 1 2 3) (array int n) (get list i) (len list)
(read_int) (read_float) (random low high)
```

Top-level statements run in paragraph order when the program starts.
Top-level variables are globals, so every function can use them. Functions
can be used before the paragraph that defines them. The parser also accepts
common variations a model may produce (`==`, `mod`, `length`, an `if` without
`then`, and so on), and the checker treats `let` of an existing variable as
`set`, and `set` of a new name as `let`.

Lists have reference semantics and grow with `append`. Indexing is
bounds-checked, and integer division by zero is a runtime error.

## Layout

| Path | What |
| --- | --- |
| `src/ovis/english.cpp` | paragraphs, lockfile, prompt and grammar, the HTTP model client |
| `src/ovis/compiler.cpp` | translate → check → repair loop |
| `src/ovis/sexpr.cpp`, `ast.cpp` | Ovis Core reader and parser |
| `src/ovis/checker.cpp` | name and type checking; its messages are written for the model |
| `src/ovis/llvm_codegen.cpp` | LLVM IR generation, optimization, object emission, linking |
| `src/ovis/runtime.cpp` | the C runtime linked into every program |
| `examples/` | example programs with hand-written reference lockfiles |

## Ideas

- Fine-tune a tiny model on English → Ovis Core pairs (the lockfiles are
  exactly that data).
- Records/structs, maps, strings as values (concatenation, reading lines).
- `ovis explain`: translate Ovis Core back to English, to check the model's
  understanding in your own words.
- Ask the user a clarifying question when a paragraph is ambiguous instead
  of guessing.
