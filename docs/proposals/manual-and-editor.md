# Proposal: a manual the model searches, and checking a line when it is done

Status: **proposed** (the owner's idea, 2026-10-23); plan L1–L9 and
V1–V7 below, decisions for the owner at the end.

The owner's idea: Ovis has a model and little documentation, so the model
should help an author find and understand the phrases Ovis offers, a
"smart manual". In the editor, completion doesn't fit (the compiler
already matches whole lines), so tools help in three ways: highlighting,
adding sigils (checking a line's phrases), and sending the author to the
manual when a line reads no phrase. A phrase is read as a whole, not word
by word, and asking the model costs time, so a line is checked only once
it is finished.

## 1. What exists, and what's missing

Already built ([quick-fix.md](quick-fix.md), [questions.md](../questions.md)):

- `ovis phrases WORDS`: the 12 built-in phrases whose words are like
  WORDS (`closest_templates`: the same word or a shared 4-letter start).
  With no words, all of them by file.
- A report of a line nothing reads ends with "Phrases with these words:",
  up to 4, from the same search.
- `ovis lsp` and the VS Code extension: problems underlined, a quick fix
  per reading (writes `@[meaning: …]`), hover on a meaning, sigils dimmed,
  a TextMate grammar (sigils, comments, texts, numbers, a fixed list of
  keywords). The program is checked when a file is opened or saved, with
  no model.
- Embeddings of phrases (kept, off: `--embeddings-only`) and `choose`
  with "none of these".

Missing, and why it matters:

1. **Nothing describes a phrase.** A template is a pattern, its aliases
   and its types. The `#` comments above some templates are dropped when
   they're read, and the examples are test programs
   (`test/phrase_library.txt`), not in the binary. So `ovis phrases` can
   show `TEXT in capitals` but can't say what it does or show it in use.
2. **The search needs the phrase's words.** "How do I make every
   letter big" shares no word with `in capitals` or its aliases (`in
   upper case`, `in uppercase`), so the lexical search misses it. Synonyms are what the model (and embeddings) are for.
3. **Only the library is searched.** `ovis phrases` knows the built-in
   phrases, not what the program and its packages define: their phrases,
   records, fields, traits and values, its variables, its C functions.
   And what a line can use depends on its file (`use NAME`, `internal/`,
   `use "x.h"`) and, for variables, on where the line is (§2.4).
4. **Highlighting can't know phrases.** A TextMate grammar sees words, and
   which words are a phrase's and which are names depends on the program.
   Only the parser knows.
5. **Checking waits for a save.** Problems go stale while typing; on the
   other hand, checking every keystroke would underline every half-typed
   line.

A note on words: an author writes phrases; Ovis Core is what the
compiler reads them into (`(sort $xs)`), and the prelude's Core functions
are behind the phrases. The manual is about the phrases (and the built-in
forms, records and C functions a line can use). It shows the Core of a
phrase only when asked (`--core`), for the curious and for library
authors.

## 2. The manual

### 2.1 Documentation lives beside each phrase

Each template gets a description and examples in its `.phr` file, as
indented lines like `alias:`:

```text
expr  {s: str} in capitals                      : str    => (prelude_upper $s)
      about: The text with every letter a capital: "Ada" in capitals is "ADA".
      example: show "Ada" in capitals
      example: shout = message in capitals
```

- `about:` is one or two sentences of plain English. Required: a test
  fails for a template without one (as `[docs]` does for the tracking
  documents), so the manual can't fall behind the library.
- `example:` lines are checked by a test: each reads, alone or after
  the template's test program in `phrase_library.txt` (which gives it its
  names), to this template. An example that stops reading fails the
  build, so the manual never shows a line Ovis can't read.
- What a program or package defines is documented by its `#` comment
  lines and its own uses (§2.4), with no new syntax for authors.

This is the base of everything below, and useful without the model:
`ovis phrases` shows `about` and the first example.

### 2.2 `ovis manual`

```text
$ ovis manual how do I make every letter big
TEXT in capitals                                         (text)
    The text with every letter a capital: "Ada" in capitals is "ADA".
    show "Ada" in capitals
    Also: TEXT in upper case, TEXT in uppercase

TEXT in small letters                                    (text)
    ...
```

- **Finding.** Three steps, cheapest first, like a question:
  1. Lexical (`closest_templates`), over the pattern, aliases and
     `about`.
  2. Embeddings of the query against each entry's pattern and `about`
     (the embedding code we have; entries embedded once and cached by
     the library's hash). This finds synonyms (`big letters` → `capitals`).
  3. When the top few are close, `choose` is asked which entries do what
     the query asks, with "none of these". Only entries found in 1–2 are
     options.
- **The model finds; it never writes.** Every word shown comes from the
  library: the pattern, `about`, examples, aliases. The model only orders
  real entries, the job it already does for questions (the owner's
  decision: no generative job). It can't invent a phrase, and an answer
  is as right as the documentation.
- **What a line can see.** In a program (`ovis manual --in FILE QUERY`),
  the entries are those in sight of FILE: the library, the program's own
  phrases, the dependencies it `use`s, its records and its C headers. An
  entry from a package names it ("from `geometry`"). Without a program,
  the library.
- **No model, still a manual.** `--offline` (or no model installed) uses
  steps 1–2 only. `ovis manual` with no query lists every entry by topic
  (the `.phr` file), and `ovis manual --html DIR` writes the whole
  reference as pages (for the website, or a package's own).
- **From a line.** `ovis manual --line main.ovis:12` searches with the
  line's words of meaning (as "Phrases with these words" does) and shows
  each entry's example rewritten with the line's own names where the
  slots fit, read by the parser before it is shown.

### 2.4 What the program and its dependencies define

The library is a small part of what an author needs to look up. Most of
a program's words are its own and its packages': `to …:` phrases,
records and their fields, traits, top-level values, variables, C
functions. They are entries too, and the same search finds them.

**Where entries come from.** Not from a second reading of the program:
from the check that already runs (on a line done, a save, or `ovis manual
--in`, which checks without the model). Every file's `to …:` headers,
records and traits are declared before any line is read, so they're
entries even while some lines still have problems. The index is per
file (what that file sees) and as fresh as the last check of the
editor's text.

| Entry | What it shows, from the compiler | Its documentation |
| --- | --- | --- |
| A `to …:` phrase | its pattern with slots as kinds, the result, where it's defined | the `#` lines above its header |
| A record | its fields with types and defaults, how a type of it is written (`stack of books`), the traits it implements | the `#` lines above `a NAME has:`, and above each field |
| A trait | its required and default phrases, the records and types that implement it here | the `#` lines above `a NAME can:` |
| A top-level value (a package's constant) | its type and value, if fixed | the `#` lines above it |
| A variable | its type, where it is first set and last set before this line, its fields if a record | the `#` lines above where it's first set |
| A parameter | its type and its phrase | the phrase's |
| A C function, constant or struct | its C signature and Ovis types, its header | the header's comment, when libclang gives one |

- **Examples are the program's own lines.** A program's phrase or record
  needs no `example:` lines: the lines that read to it are examples,
  already checked. The manual shows up to two, with their file and line
  (and a link in the editor). A package shows its own uses the same way,
  and may add `# example: LINE` comment lines for a phrase it never calls
  itself, checked when the package is checked (and by `ovis release`).
- **Nothing undocumented is missing.** An entry with no comment still
  shows what the compiler knows: its pattern, types, fields, where it's
  defined and used. The model doesn't fill the gap with a summary
  (decision 1).
- **Variables depend on the line.** A name means something only from
  where it's set. `ovis manual --line main.ovis:12` (and hover on a name
  in the editor) includes the names in scope there, their types, and
  where each was set; a name set further down isn't offered. Variables
  are found by their words like anything else (`how many guests` finds
  `guest count`), but only in the line's scope.
- **"What can I do with this?"** `ovis manual --for NAME` (or a record or
  type: `--for book`) lists the phrases whose slots take its type, the
  same type check questions use: for `scores`, a list of numbers, `sort
  …`, `total of …`, `average of …`; for a `book`, the program's own
  `to lend (b: book) to (p: person):` and the traits' phrases a book
  implements. This search needs no words, and so no model.
- **Packages, by what the file sees.** A dependency's entries name the
  package and its version line ("from `geometry` v2"); two lines side by
  side are two entries. A direct dependency the file doesn't `use` is
  listed after the rest, marked "add `use geometry` to use it" (as the
  `needs_use` problem says). Another folder's `internal/` and a
  dependency's dependencies are never listed: the file can't use them.
- **Same words, two meanings.** When the program's phrase and the
  library's (or two packages') answer one query, both are shown with
  where each is from, and which one a line would read (the parser's
  answer, not the model's).
- **Embeddings are cached per entry.** The library by its hash, a
  package by its tree hash in `ovis.lock`, the program's entries by each
  entry's text, in the build folder: after an edit only the changed
  entries are embedded again.

### 2.5 Measured, like the questions

`eval/manual-queries.txt`: queries in an author's words, about the
library and about the programs in `eval/programs` and a test package (held out, written
far from the phrases' words, as `hard-questions.txt` is) and the entry
meant. `ovis_eval manual` reports the entry meant first, and in the first
5, for lexical alone, with embeddings, and with the model; CI keeps a
floor without the model. The model is used here only if it beats
embeddings by enough to pay for its time.

## 3. The editor

### 3.1 A line is checked when it is done

Agreed: a phrase reads as a whole, so a half-written line is noise, and
the model is too slow for each keystroke. But a line isn't read alone: the
names set above it, the program's `to …:` phrases and the records decide
how it reads. So "checking a line" means checking the program again and
taking what changed.

- **When a line is done.** LSP has no such event, so the extension sends
  one: `ovis/lineDone` with the line, when the cursor leaves a line that
  was edited, or Enter is pressed at its end (VS Code's
  `onDidChangeTextEditorSelection`). Other editors still get a check on
  save, and after 2 s with no typing.
- **Two speeds.**
  1. *Without the model*, at once: the program is read again with the
     editor's text (rules, ranking shortcut, edits, as on save today).
     The line being edited keeps its old problems until it is done, so
     nothing flickers under the cursor. First measure how long this takes
     for the examples and a large package (V1); if over ~150 ms, keep
     each file's readings keyed by the line's text and what it sees, and
     read again only the lines whose key changed.
  2. *With the model*, in the background, for lines step 1 left unclear:
     through the daemon (`ovis model serve`, already started by builds),
     one question at a time, the line done last first, cancelled when
     the line changes again. Answers are kept by the lockfile's key, so
     the build that follows asks nothing new.
- **What the model's answer does in the editor.** The line is marked
  (a hint, not an error) "Ovis reads this as: …", with a quick fix to
  write the `@[meaning: …]` now; nothing is written by itself (decision 3).
  The build writes it anyway, as today. A line the model can't settle is
  an error, as now, with its fixes and a link to the manual.

### 3.2 Highlighting from the parser

The TextMate grammar stays for what never changes (sigils, comments,
texts, numbers, the built-in forms), so a file is coloured at once.
On top, `ovis lsp` gives **semantic tokens** from each line's reading:

| Token | Colour role | Example |
| --- | --- | --- |
| a phrase's word | function | `in capitals`, `add … to` |
| a name | variable | `scores`, `fan speed` |
| a field | property | `b's title` |
| a record or trait | type | `book`, `shape` |
| a `to …:` header's words | function declaration | `to greet (p):` |
| a sigil's own line | dimmed (as now) | `@[meaning: …]` |
| the unclear span | underlined (a problem) | |

The parser knows which tokens a template's words matched and which
filled its slots; it needs to keep that with each reading (a list of
word roles per line, V3). This also makes a name of several words
(`fan speed`) read as one, which no grammar can.

### 3.3 From a problem to the manual

- Each problem of kind `no_phrase` or `unclear` gets a code action "Look
  it up in the manual", which runs `ovis manual --line` and shows the
  entries in a side panel; each has "Use this" which puts its example,
  with the line's names, in place of the line (the author's own edit,
  checked when the line is done).
- **Hover on a phrase's word** shows its manual entry: pattern, `about`,
  the first example, and the package it's from. **Hover on a name, field
  or record** shows its entry (type, where set or defined, fields), and
  *go to definition* jumps to where it's defined or first set
  (`textDocument/definition`), which the index already knows.
- "Phrases with these words" in reports stays, and says
  `ovis manual --line FILE:N` for more.

### 3.4 Not completion

Agreed, with one small exception left to the owner (decision 4): while
typing, nothing is offered. The manual's "Use this" is the one way an
entry goes into the file, chosen by the author.

## 4. Plan

| Step | What | Test | Size |
| --- | --- | --- | --- |
| L1 | `about:` and `example:` lines in `.phr`; kept on `template`; `#` comments above `to …:` headers kept as their `about` | every template has `about`; every example reads to its template | M |
| L2 | `about` and examples written for the 171 templates | the L1 test | M |
| L3 | `ovis manual QUERY` lexical over pattern, aliases, `about`; `--in FILE` (phrases in sight), `--line`, no query lists by topic; `ovis phrases` shows `about` | the entries for known queries, in order | S |
| L7 | The program's and packages' entries (§2.4): from the check's vocabulary, `#` comments as documentation, uses as examples, origin and version, unused dependencies marked | the entries of `examples/shop`, a test package, a file that doesn't `use` it | M |
| L8 | Variables in scope at a line (`--line`); `--for NAME\|TYPE` | the names and phrases for known lines | S |
| L9 | A package's `# example:` lines, checked by `check` and `release` | a broken example fails | S |
| L4 | Embeddings for the manual (cached by library hash, tree hash, entry) | `eval/manual-queries.txt`, `ovis_eval manual` | M |
| L5 | `choose` among the entries; measured against L4 | the same eval; CI floor without the model | S |
| L6 | `ovis manual --html DIR` | pages for every entry, links checked | S |
| V1 | Measure a check without the model per example and for a large package | numbers in state.md | S |
| V2 | `ovis/lineDone` in the extension and the server; the edited line keeps its problems until done; check after 2 s idle for other editors; per-line cache if V1 says so | an LSP session by script: a half line isn't reported, a done one is | M |
| V3 | Word roles kept with each reading; `textDocument/semanticTokens/full` | the tokens of a known program | M |
| V4 | Hover on a phrase's word shows its manual entry; "Look it up" code action and the extension's panel | the LSP script; the panel by hand | M |
| V5 | The model in the editor: unclear lines asked through the daemon in the background, cancelled on change, a hint with "write the meaning" | a scripted model, as `question_test` | M |
| V7 | Hover on names, fields and records; go to definition | the LSP script | S |
| V6 | The grammar's keywords from the built-in forms (one list, shared with the lexer) | the grammar's words are the forms' | S |

Order: L1–L3, L7–L8 and V1 first (useful without the model, and they tell us
what V2 needs), then V2–V4, then L4–L5 and V5 (the model, measured), L6,
L9 and V6 last.

## 5. Decisions for the owner

1. **The model's job in the manual.** Recommended: it orders real entries
   and never writes the explanation (the answer is the documentation).
   The alternative, a sentence written by the model about the query,
   reads nicer but can be wrong with nothing to check it.
2. **Where documentation lives.** Recommended: `about:` / `example:` in
   the `.phr` files, checked by tests. The alternative, a separate manual
   written by hand, falls behind the library.
3. **Sigils from the editor.** Recommended: the model's reading is a hint
   with a one-click quick fix, and only a build writes sigils by itself.
   The alternative writes `@[meaning: …]` into the buffer as soon as the
   model settles a line, as a build would, which puts text under the
   author's hands while they type.
4. **Completion.** Recommended: none while typing. Possible later: names
   (variables, fields) only, which are the program's and never a
   phrase's, if authors ask.
5. **When a line is done.** Recommended: the cursor leaving an edited
   line, or Enter at its end. The alternative, only on save, is simpler
   but leaves problems stale.
6. **Documenting the program's own words.** Recommended: `#` comment
   lines above a definition, and its own uses as examples, so authors
   write nothing new. The alternative, `about:`-style lines in programs
   too, is more uniform but asks authors for syntax most won't write.
