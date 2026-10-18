# ovis_eval options

Archived from architecture-spec.md → "5. Reading a program" → "Questions" on 2026-10-18 (dream at `63809d4`). Tool details, not needed to work on the compiler; kept word for word.

`ovis_eval questions eval/questions.txt` measures ranking and model, and
`ovis_eval programs eval/programs` whole programs. `--orders N` asks each
question with its readings in N orders (as ranked, reversed, seeded
shuffles; "none of these" last), since the order alone moves the model's
picks; the run reports the spread. `--embeddings-only` settles every
question by `language_model::embed` instead (the experiments
`order_ties_by_embedding` and `choose_by_embedding` in
`front_end_options`, both off). `--min-first` and
`--min-shown` fail the questions run when fewer cases rank the right
reading first or show it; CI sets them to the current numbers so adding
phrases can't quietly make the ranking worse. `--with PROGRAM` reads every
case with and without a program's phrases (helper files whole, `to …:`
blocks and `use` lines of `main.ovis`), by the ranking alone, and lists
each case that gets worse: the right reading loses first place, drops out
of the 9 shown, or the line stops asking (it now reads as the new phrase).
It exits 1 if any does, 2 if the phrases don't read on their own.

