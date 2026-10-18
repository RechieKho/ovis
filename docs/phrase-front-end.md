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

**Front end** (`compile_phrase_program`, `compile_phrases` for one text,
`ovis run/build --phrases FILE|FOLDER`; `parser.cpp`, `questions.cpp`, `front_end.cpp`). A line's
first words decide its shape (`x =`, `if`, `otherwise`, `while`, `for
each`, `repeat … times`, `stop`, `skip`, `to …:`, `give back`, else a
statement phrase); its span is read by a chart parser (fixed
operator precedence, phrases, optional articles, plurals) filtered by
types. One reading: lowered. Several, or none: a question (below), whose
answer is written as `Read as: "SPAN" is READING` and applied by
whole-word text substitution on later builds, or dropped once SPAN isn't
in the line. Each top-level line is one
unit; variables first set in a block are hoisted to a `let` at the top of
the program or phrase. `to WORDS (p[: TYPE]) … [as NAME]:` headers are read
first, so a phrase works on any line, as a statement and an expression; it
is an Ovis Core `fn` named by its words (`pad_to`), also called as
`pad_to(a, b)` like C functions. Assigning in a phrase makes a local.
Variables named like Ovis Core words get a `_`; `;@LINE` comments in the
Core map checker problems back to source lines. A variable first set in a
block is declared `(let x TYPE)` (or `?`) at the top.

**Programs and C.** `load_phrase_program` lays out files as the old
`load_program` (helpers sorted, `main.ovis` last; modules from paths).
All files share one `program_context`: the vocabulary, the asker and its
budget, phrase names (a phrase is defined in one file only), and the
top-level variables of the files read so far (assigning one in a later
file makes that file's own). Units carry their module; the prelude is
module `prelude`; then `resolve_modules` (helper rule, names) and
`resolve_c_imports` run. `use [words] "x.h" [words "lib"]` lowers to
`(use-c …)` and imports the header while headers are collected, so its
functions (`name(args)`) and constants work on every line; `ref x` as a
call argument is `(ref x)`. Header hashes go in each file's lockfile, with
a warning when one changed.

**Questions** (`question_of`, `questions.cpp`). The unclear span is the
shortest one nothing reads, in a reading of the rest. Kinds and their rule:
- `ambiguous_parse`: several readings; candidates are the readings.
- `unknown_name`: one word, no name: candidates are the names in scope with
  which the line reads, most recently set first; rule: the one close
  spelling (`core::closest_name`). In a longer unclear span a word that is
  no phrase word and a close spelling of a name is fixed first.
- `reference`: `it`/`them`, same candidates; rule: only one fits.
- `unknown_phrase`: templates whose slots take the span's arguments (in
  any order the types allow), ranked lexically against its other words
  (same word 3, a shared 4-letter start 2, trigrams 1, function words ×0.2;
  aliases count as the template's words). Also, ranked 1 lower, with one
  recent name as a left-out argument of a fixed slot (`missing_argument`).

Resolution: the lockfile (below); else the rule; else the ranking shortcut (best score ≥ 3 and 3
above the next); else `choose` over at most 8 readings plus "none of
these" (run-offs for more), accepted at p ≥ 0.6 and margin ≥ 0.3, within
`--model-budget`; else a report listing the options. All but the report
write a `Read as:` line. `front_end_timing` counts how each was settled.
`ovis_eval questions eval/questions.txt` measures ranking and model.

**Lockfile** (`<file>.lock`, `ovis::phrase::lockfile`, version 3; the old
pipeline's version 2 is refused): `model`, `model_fingerprint`,
`c_headers`, and `readings` of `{key, span, reading}` for questions the
model settled. The key is an FNV-1a hash of the line, the span, the names
the model is told about and the candidate readings, so any change asks
again; an entry is used only if its reading is still a candidate. Entries
are kept while the question was asked or its `Read as:` line is still in
the source, sorted by key, so rebuilding changes neither file. `ovis run
--phrases` saves it when it changed, after a failed build only if the
model answered something.
