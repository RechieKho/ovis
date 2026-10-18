# M1.3 parser, M1.4 scope and type filter, M1.5 lowering

Archived from remaining-tasks.md → "M1: phrase front end, no records" on 2026-10-18 (dream at `a65ae35`). Finished in the commit after `a65ae35`, with two differences from the plan:
- the parser is the memoised chart parser of the first slice (every reading of every span), not Earley: it returns every parse, which is what the plan needs;
- lowering writes Ovis Core text (with `;@LINE` comments mapping problems back to source lines) that `parse_unit` reads, not the AST directly: parsing it takes well under a millisecond.

- [ ] **M1.3 Parser** (`parser.cpp`): Earley over the templates, the
  built-in statements (`=`, `to … :`, `give back`, control flow of
  phrase-grammar §7) and expressions with fixed precedence. Articles are
  optional, words match case-insensitively and singular/plural. Returns
  every parse of a line. Tests: each line of the ported examples gives the
  expected parses.
- [ ] **M1.4 Scope and type filter** (`scope.cpp`):
  - first pass: collect every `to … :` header, so phrases and functions are
    visible in any order, as functions are now;
  - in order: a scope of names and types, inferred from assignments, with
    unknown types left as variables;
  - drop parses with names not in scope or slot types that don't fit.
  The full elaborate and check still run once on the whole lowered
  program; this filter only needs to be fast and never reject a parse they
  would accept.
- [ ] **M1.5 Lowering** (`lower.cpp`): parses to `core::program` AST
  directly, not to text. Blocks to `if`/`while`/`each`/`for`; `to … :` to
  `fn` named from its fixed words (`pad_to`, or `as NAME`); `give back` to
  `return`; `f(args)` to a call. Core diagnostics map back to source lines
  (fixes the P1 task about Ovis Core line numbers for this front end).
