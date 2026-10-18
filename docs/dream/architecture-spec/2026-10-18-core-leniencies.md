# Ovis Core leniencies

Archived from architecture-spec.md → "6. Ovis Core" → "Statements" on 2026-10-18 (dream at `63809d4`). The reader still accepts all of these; the phrase front end writes strict Core and never needs them. Kept word for word.

(Following "`(set-field X FIELD VALUE)` sets a field of the record X.")

Leniencies:
- `let` of an existing variable acts as `set`, and `set` of a new name acts
  as `let`;
- an `if` without `then`;
- operator aliases (`==`, `mod`, `length`, `//`→`div`, …);
- a bare value as a function's last line.
- `(let NAME VALUE STATEMENT...)` is the let followed by the statements;
- a top-level `fn` with no body is skipped: the model copied the declaration
  of a function another paragraph defines from its context.

