# M0.2 speed and M0.4 ranking

Archived from remaining-tasks.md → "M0: model scoring and ranking" on 2026-10-18 (dream at `a2d5a67`). Finished in the commit after `a2d5a67`: `ovis_eval questions` and `eval/questions.txt` measure both, 0.5B–7B are recorded in state.md, the embedded-model workflow reports the questions, and a question shows 8 readings. The lexical ranking (same word, a shared 4-letter start standing in for stems, trigrams, aliases) was already in place; embeddings were not needed (the right reading was among the 8 shown in 24 of 24).

- [ ] **M0.2 Speed.** First numbers are in state.md (1.5B ~250 ms, 3B
  ~500 ms per question on 4 CPU cores). Still to do: 0.5B, and a repeatable
  measurement in `embedded-model.yml` with the M1.14 questions.
- [ ] **M0.4 Ranking.** Ovis has **no embedding similarity** today, despite
  the proposal's wording (fix it there). Start lexical: shared word stems
  between the verb and a template's words, plus `alias:` lines, plus
  character-trigram similarity for near spellings. Add model embeddings
  (llama.cpp embeddings mode, mean-pooled, same model) only if M1.14 shows
  the right template outside the top 8 too often.
