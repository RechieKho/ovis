# Proposal: phrase grammar and candidate format

Status: **sketch, not decided.** It describes a pivot away from the
LLM writing the program. Nothing here is built.

## Why

With a real model, the LLM-writes-everything pipeline is slow and
unreliable. 7B gets 3 of 5 programs right in about 12 minutes on CPU
([state.md](../state.md)). The new conditions are:

1. the model is local;
2. it helps compilation, for example by inferring what the author left out;
3. compiling, model included, stays fast.

So the roles are turned around. **A deterministic parser reads the
program.** When the parser can't decide, the compiler lists the readings
that type-check, and **the model picks one**. The model never generates
code, only a choice between written-out options.

## 1. What a program looks like

```
count = ask for a whole number
numbers = empty list of whole numbers
repeat count times:
    add (ask for a whole number) to numbers

smallest = smallest of numbers
largest = largest of numbers
average = mean of numbers

show "Numbers:", numbers
show "Smallest:", smallest, "Largest:", largest
show "Average:", average with 2 decimals
```

`mean of numbers` matches no phrase, so it becomes a question for the model
(§4). Every other line parses deterministically.

## 2. Surface grammar

The structure is line-based, with Python-like indentation; `#` starts a
comment.

```
program    := { line }
line       := INDENT statement [ ":" ] NEWLINE   ; ":" opens an indented block
statement  := NAME "=" expr                      ; assignment (built in)
            | "to" header ":"                    ; defines a new phrase (§3)
            | "give back" expr                   ; return
            | phrase                             ; a statement phrase
expr       := operand { OP operand }             ; + - * / mod = != < > <= >= and or
operand    := NUMBER | STRING | NAME | "(" expr ")" | phrase | "not" operand
phrase     := words and operands matching a template (§3)
```

- Operators and their precedence are fixed, as in Python, and are never
  sent to the model.
- `a`, `an` and `the` are optional everywhere: `add x to the list` and
  `add x to list` mean the same.
- `,` separates the arguments of a variadic slot (`show a, b, c`).
- Words match case-insensitively, and plural and singular match
  (`decimals`/`decimal`).

**Parsing** uses an Earley chart parser, with every template as a rule and
every slot as `expr`. Earley returns all parses of a line. They are filtered
in three steps:
1. names must be in scope, or be the left side of `=`;
2. each parse must type-check, using the elaborator's inference, run per
   statement;
3. if one parse is left, it's used. If several or none are left, the
   compiler asks a question (§4).

## 3. Phrase templates

Built-in phrases live in `phrases/*.phr` and are compiled into `ovis`. One
template per line:

```
# kind  pattern                                : result  => Ovis Core
expr    average of {xs: num[]}                 : float   => (/ (float (sum $xs)) (float (len $xs)))
expr    smallest of {xs: T[]}                  : T       => (min_of $xs)
expr    {x: float} with {n: int} decimal[s]    : str     => (format_fixed $x $n)
expr    ask for a whole number                 : int     => (read_int)
expr    empty list of {t: type}                : t[]     => (list $t)
stmt    add {x: T} to {xs: T[]}                          => (append $xs $x)
stmt    show {items: any, ...}                           => (print $items...)
stmt    repeat {n: int} times: BLOCK                     => (for _ 1 $n BLOCK)
        alias: do {n} times
```

The fields are:
- **kind:** `expr` or `stmt`. A `stmt` ending in `: BLOCK` takes the
  indented block.
- **pattern:**
  - literal words;
  - `[x]` for an optional word or suffix;
  - `(a|b)` for alternatives;
  - `{name: type}` for a slot.
- **slot types:** `int float num str bool any type`, lists `T[]`, and type
  variables `T`. `num` is int or float, and `type` is a type name in words
  (`whole number`, `decimal`, `text`).
- **result type:** what an `expr` gives back.
- **Core:** the expansion, where `$name` is the slot's Ovis Core.
- **alias:** extra patterns for the same template. These are deterministic
  synonyms and never involve the model.

The **canonical reading** of a template is its pattern with the slots filled
in, for example `average of numbers`. That reading is what the model sees
and what the source records.

### User phrases

A function is defined by its phrase, Inform-style. Parameters go in
parentheses, and the elaborator infers their types.

```
to pad (text) to (width):
    give back text + spaces (width - length of text)
```

This defines `stmt/expr  pad {text} to {width}` with the inferred types.
The template is visible to every line of the program, in any order, as
functions are now.

## 4. Questions and candidates

The compiler asks a question only where deterministic rules can't decide.
Each kind of question below comes with the deterministic rule that is tried
first:

| Kind | Example | Rule tried first | Candidates |
| --- | --- | --- | --- |
| `unknown_phrase` | `mean of numbers` | aliases | templates whose slots accept the line's arguments (below) |
| `ambiguous_parse` | `show a and b` (bool `and`, or two items?) | type filter | the type-correct parses |
| `unknown_name` | `show totl` | edit distance (as now) | names in scope of a fitting type, plus "a new variable" |
| `reference` | `sort it` | only one name of the right type in scope | names in scope of the slot type, the most recent first |
| `missing_argument` | `show average with 2 decimals` | only one fitting name in scope | names in scope of the slot type |

**Candidates for `unknown_phrase`:**
1. Parse the parts of the line that can be parsed: names, literals and
   known sub-phrases. These are the *arguments*. The leftover words are the
   *verb* (`mean of`).
2. Keep the templates whose slots accept the arguments' types, trying each
   argument order the types allow.
3. Rank them by embedding similarity between the verb and the template's
   words, using the existing embedding lookup, with no generation.
4. Keep the top 8.

**Shortcut:** if the top embedding score clears the second by a wide margin,
it is taken without asking the model. The threshold is to be tuned.

Every candidate has already been parsed and type-checked, so whichever one
is picked, the program compiles.

### The data structure

```cpp
struct candidate {
  std::string reading;  // canonical English: "average of numbers"
  std::string core;     // Ovis Core: (/ (float (sum numbers)) ...)
  double prior;         // embedding / recency score, used for order and ties
};

struct question {
  question_kind kind;                // unknown_phrase, ambiguous_parse, ...
  std::string line;                  // the source line, as written
  std::string span;                  // the words in doubt: "mean of numbers"
  std::string context;               // names in scope with types, in words
  std::vector<candidate> candidates; // 2..8, all type-correct
};
```

### What the model sees

There is one fixed instruction prefix, whose KV is cached once per run and
can be reused across runs. Then comes a short question, about 60 to 120
tokens:

```
Line: average = mean of numbers
Known: numbers is a list of whole numbers.
"mean of numbers" most likely means:
1. average of numbers
2. smallest of numbers
3. largest of numbers
4. sum of numbers
Answer:
```

- **Grammar:** `root ::= [1-4]`. Only one token is decoded.
- **Score:** the softmax of the logits of the tokens `1`..`n`, taken from a
  single forward pass. Nothing is sampled, so the result is deterministic.
- **Accept** if `p(best) ≥ 0.6` and `p(best) − p(second) ≥ 0.3`; the numbers
  are to be tuned. Otherwise the build stops and asks the author:

  ```
  main.ovis:7: "mean of numbers" is unclear. Did you mean:
    1. average of numbers
    2. sum of numbers
  Write one of them in the source.
  ```
- **Position bias:** small models favour option 1. Candidates are ordered by
  `prior` so that bias does the least harm. The order could also be scored
  a second time reversed and the two averaged, which doubles the cost; to
  be measured.

### Recording the decision

The choice is written under the line, as `Compiler:` notes are now:

```
average = mean of numbers
    Read as: average of numbers
```

- The `Read as:` line is **authoritative**. The next build parses it instead
  of asking again, and the author can edit it to override the model.
- The lockfile also caches the decision, keyed by a hash of `line`, `span`,
  `context` and the candidate readings. Adding a function changes the
  candidates only for the questions it touches.
- A build with a complete source or lockfile never loads the model.

## 5. Cost

- **Per question:** one forward pass over about 100 new tokens, with one
  decoded token. To be measured for 0.5B and 1.5B on CPU; the guess is under
  a second.
- **Per program:** most lines parse with no question. Only new or changed
  questions reach the model.
- **Budget:** `--model-budget SECONDS`, default 10. Questions left over when
  it runs out are reported to the author like unclear ones, so the build
  never hangs on the model.

## 6. What carries over

| Current part | Fate |
| --- | --- |
| Ovis Core, elaborate, checker, LLVM codegen, runtime, `use-c` | kept: templates expand to Ovis Core |
| llama.cpp built-in model, embedding lookup, model pull | kept: used for scoring and ranking |
| lockfile, `Compiler:` notes, reproducible builds | kept: decisions as `Read as:` lines |
| modules / helper files | kept |
| detail pass, translation, repair loop, explanations, few-shot examples | dropped |
| `english.cpp` paragraph parsing | replaced by the line parser |

## 7. Open questions

- C functions imported with `use-c`: should they be phrases (`zlib compress
  {data}`), or called as `compress(data)` with a plain call syntax?
- Should a template be allowed to expand to several statements?
- Loops and conditionals: should they be built-in syntax (`if`, `for each
  x in xs:`, `repeat`, `while`), or `stmt ... : BLOCK` templates? The
  templates are more uniform, but built-in syntax gives better error
  messages.
- How large must the built-in phrase library be before ordinary programs
  need few questions? Possibly 150 to 300 templates.
