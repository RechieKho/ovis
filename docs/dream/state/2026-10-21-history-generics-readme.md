# History: generics G1–G8, numbers leftovers, the README rewrite

Archived from docs/state.md → "History" on 2026-10-21 (dream at 4b06922).

| Commit | Summary |
| --- | --- |
| (P2) | **Generics G1–G8** ([proposals/generics.md](proposals/generics.md)): Core generic functions and records (`forall`, `(var T)`, `(record stack int)`), rigid slots and instantiation in the elaborator, bounds checked once, copies per use in the checker (`monomorphise.cpp`), generic impls and impls for numbers and text; `any NAME`, generic record headers and type words in phrases, `examples/stack.ovis`; `(alloc T N)`/`new room for N items` checked against its end; the library's per-type prelude functions, `traits.generic` and `fields.generic` as Core generics (`copy_field_generics`), no `for records:` lines; `examples/collections` measured; C macros as generic C phrases copied per call's C types, answers in the lockfile (`c_macros`) |
| (P2) | Numbers leftovers: lists of lists of numbers of a size keep it (`list of lists of bytes`, `list of bytes` as a parameter type is `(list u8)`); map keys of a size (`map of bytes to text`, pinned, `map-keys` at their size); `to … gives an int32:` states a phrase's result type (`number_test`, `phrase_library.txt`) |
| (P2) | README rewritten for ease of reading: a contents line, the language by topic (records, traits, generics, sized numbers, `at the end`, maps, C structs added), the model, usage and CMake in their own sections, links to the docs |

Archived from docs/state.md → "Decisions made with the project owner" on
2026-10-21 (dream at 4b06922): superseded on 2026-10-19 by rewrites.

| When | Decision |
| --- | --- |
| 2026-10-18 | The model only chooses (no generative job; superseded 2026-10-19 by rewrites). GPU first: 7B where a GPU runs it, 3B otherwise; make questions cheaper. Judge model changes over several orders (`--orders`). |
