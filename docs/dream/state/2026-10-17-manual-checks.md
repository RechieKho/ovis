# Manual checks, sessions up to 2026-10-17

Archived from `docs/state.md` → "Verified" on 2026-10-17 (dream at
`b721795`). These were one-off checks done by hand, not by the test suite.
They show the features worked when they were added; current status is in
`docs/state.md`.

- The CLI end to end against a mock OpenAI-compatible server: detail,
  translate, write back, rebuild offline, edit a step.
- Deleting a sentence but leaving its steps through the real CLI: the steps
  are removed, with a warning.

How they were run:
- The mock server was a small Python `http.server` answering
  `/v1/chat/completions`. It chose the reply by the request's `grammar`
  field: detail, explanation or Ovis Core.
- The deletion scenario used a copy of `examples/primes.ovis` with the
  `is_prime` sentence deleted, built with `--offline`.
