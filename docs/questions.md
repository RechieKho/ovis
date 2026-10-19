# Questions

How an unclear line becomes a question, how it is settled, and the
model's rewrites. Part of [architecture-spec.md](architecture-spec.md) §5,
moved here to keep the spec under 500 lines.

**Questions** (`question_of`, `questions.cpp`). The unclear span is the
shortest one nothing reads, in a reading of the rest. Kinds and their rule:
- `ambiguous_parse`: several readings; candidates are the readings.
- `unknown_name`: one word, no name: candidates are the names in scope with
  which the line reads, most recently set first; rule: the one close
  spelling (`core::closest_name`). In a longer unclear span a word that is
  no phrase word and a close spelling of a name is fixed first.
- `reference`: `it`/`them`, same candidates; rule: only one fits. Also a
  record's name (`the book`, with its article): the variables holding one.
- A word where a field's name goes that no field is called (a hole with
  its record, `cost of b`, `b's titl`, `total cost of books`): an
  `unknown_name` whose candidates are the record's fields with which the
  line reads; rule: the one close spelling.
- `unknown_phrase`: templates whose slots take the span's arguments (in
  any order the types allow), ranked lexically against its other words
  (same word 3, a shared 4-letter start 2, trigrams 1, function words ×0.2;
  aliases count as the template's words). Also, ranked 1 lower, with one
  recent name as a left-out argument of a fixed slot (`missing_argument`),
  kept only if it ranks above every reading of the arguments as written.
  A span starting with a record's name also gets `a NAME with F V, …` (each
  value a field its type fits, written order; record words 3 each).
  Only readings with which the whole line still reads are offered (the
  span replaced by the reading). A name that is also one of the span's
  words (`how many items in items`) may be part of the verb instead, if the
  template has a word like it. Never offered: a reading without a field
  word of the span, or one filling a variadic slot (`show`) with several
  values that shares no word with the span (`drops_meaning`). The
  9 best are kept, and any as good as the 9th (then a run-off).
- **Edits** (`edit_candidates`, put first by `with_edits`): the span
  edited a little and read by the parser, so a candidate keeps the line's
  own values, fields and structure and any built-in form (`with`, `'s`,
  `set … to`, comparisons) or type slot can be one, which the readings
  above can't. One edit: a word put in (from phrases sharing a word with
  the span, the forms' words, `of`/`is`/`to`/`,`/`'s`…; a joining word is
  cheaper), up to three joining words taken out (`of`, `the`, `does`,
  `which`…; never a word with meaning: `w's age in months` is not `w's
  age`), or a word replaced by one spelt like it (`decrement` →
  `decrease`; `median` → `average` is the model's choice, not an edit).
  Names, values, fields and records are never taken out or replaced.
  Cheapest first, at most 9. An edit leaving only values goes last (and
  is dropped when the span is the whole line). Edits never make the
  ranking shortcut. When nothing at all is offered for the unclear part,
  edits of the whole line are tried (`c's count not 0`, `b title`).
  Measured by `eval/wording-questions.txt` (near misses; CI keeps its
  floor): 3B right on 14 of 28 before, 22 after, every confident pick right;
  the other sets unchanged.

Resolution: the lockfile (below); else the rule; else the ranking shortcut
(a reading covering every content word of the span, best score ≥ 3 and 3
above the next); else `choose` over at most 9 readings plus "none of
these" (`0` when there are 9; run-offs for more, a group of one goes on
unasked; a run-off's final makes "none" less likely, so it is avoided), accepted at p ≥ 0.6 and margin ≥ 0.3, within
`--model-budget`; else, for an `unknown_phrase` (none offered, or none
taken), a **rewrite**; else a report listing the options. All but the report
change the line's meaning. `front_end_timing` counts how each was settled,
and `front_end_result::decisions` lists this build's (`ovis_eval` reads it).

**Rewrite** (`rewritten`, `rewrite_request_of`): the model writes the line's
whole span (`p_shape.begin..end`) again with `chat`, once. The system
message (the built-in forms, two examples, the library's phrases with slots
as `NUMBER`, `LIST`, `FIELD`...) is the same for every program, so its state
is cached; the user message has the records, the program's phrases, the
names in scope and the line. A GBNF grammar allows only the template words,
the forms' words, records, fields, names in scope, the span's own texts,
numbers, operators, `'s` and brackets, so it can't copy an example or invent
a name. It is kept only if it differs from the span, keeps every name,
number, text, field and record word of the span (`rewrite_loss`; a record
word may become a variable holding that record), and the line reads with
it. Its lockfile entry's key is the line's with the one reading
`(rewrite)`, and is used only while it still passes. Name questions are
never rewritten (a rewrite can't invent a name). `front_end_options::rewrite`
turns it off (the questions eval does, to measure choosing alone).

`ovis_eval questions` and `ovis_eval programs` measure ranking, the model and
whole programs; their options (`--orders`, `--embeddings-only`, `--min-first`,
`--min-shown`, `--with`) are in
[dream/architecture-spec](dream/architecture-spec/INDEX.md).
