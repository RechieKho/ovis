# Phrase front end

How the phrase front end (`ovis::phrase`, `src/ovis/phrase/`) works: the
replacement for architecture-spec.md §2–§5, a deterministic parser of
English-like lines where the model only picks between readings that
type-check. Design in [proposals/](proposals/phrase-grammar.md), plan in
[remaining-tasks.md](remaining-tasks.md) (M0–M4). It runs with `--phrases`;
M1.13 makes it the only front end and folds this file into
[architecture-spec.md](architecture-spec.md). Keep it under 500 lines.

**Lexer** (`split_lines`): logical lines with tokens, block depth and
annotations. Python-like blocks (a deeper line only under a `:` line; no
tabs; dedents must line up); open brackets continue a line; `"…"`, `'…'`,
`“…”` texts; `'s` after a word is a symbol, `don't` one word, `2nd` a word.
`Read as:`/`Compiler:` lines are one-line annotations of the line above,
indented under it.

**Templates** (`read_templates`, `.phr`): `expr PATTERN : TYPE => CORE` or
`stmt PATTERN [: BLOCK] => CORE`, then indented `alias:` lines. Patterns:
words, `[optional]`, `(a|b c)`, `word[suffix]`, `{name: type}`, one
`{name: type, ...}`. Types: `int float num str bool any type`, `[]`,
uppercase variables, a `type` slot's name. Checked (errors at
`file:line`): one Core form, every `$name` a slot and every slot used,
`$name...` for the variadic one, `BLOCK` iff a block, an always-present
word, a word between slots, result variables bound by a slot.
- `phrases/*.phr` and the Ovis Core prelude `phrases/*.core` (functions
  multi-step phrases call) are embedded at build time.

**Front end** (`compile_phrases`, `ovis run --phrases`; one file, no
lockfile yet; `parser.cpp`, `questions.cpp`, `front_end.cpp`). A line's
first words decide its shape (`x =`, `if`, `otherwise`, `while`, `for
each`, `repeat … times`, `stop`, `skip`, `to …:`, `give back`, else a
statement phrase); its span is read by a chart parser (fixed
operator precedence, phrases, optional articles, plurals) filtered by
types. One reading: lowered. Several, or none (then the shortest span
nothing reads): a question, `choose` over at most 8 readings plus "none of
these" (run-offs for more), accepted at p ≥ 0.6 and margin ≥ 0.3, written
as `Read as: "SPAN" is READING` and applied by text substitution on later
builds, or dropped once SPAN isn't in the line. Each top-level line is one
unit; variables first set in a block are hoisted to a `let` at the top of
the program or phrase. `to WORDS (p[: TYPE]) … [as NAME]:` headers are read
first, so a phrase works on any line, as a statement and an expression; it
is an Ovis Core `fn` named by its words (`pad_to`), also called as
`pad_to(a, b)` like C functions. Assigning in a phrase makes a local.
Variables named like Ovis Core words get a `_`; `;@LINE` comments in the
Core map checker problems back to source lines.
An unknown span's readings are the templates whose slots take its
arguments, ranked lexically against its other words (same word, a shared
4-letter start, trigrams; aliases count as the template's words).
`ovis_eval questions eval/questions.txt` measures ranking and model.
