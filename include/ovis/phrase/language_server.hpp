#ifndef OVIS_PHRASE_LANGUAGE_SERVER_HPP
#define OVIS_PHRASE_LANGUAGE_SERVER_HPP

#include <iosfwd>

#include <ovis/core/c_import.hpp>

// `ovis lsp`: a Language Server Protocol server for editors
// (proposals/quick-fix.md, Q6).
namespace ovis::phrase {

// Serves one editor on the streams (JSON-RPC with `Content-Length`
// headers) until it says `exit`. A program is checked when a file of it is
// opened or saved: its problems are published, each fix of an unclear line
// is a quick fix writing `@[meaning: …]` above it, and hovering a line
// shows the meaning it is read with. The model is never asked: an editor
// can't wait for it, and a build settles what the model must.
auto serve_language(std::istream &p_input, std::ostream &p_output,
                    const core::c_import_options &p_imports) -> int;

} // namespace ovis::phrase

#endif // OVIS_PHRASE_LANGUAGE_SERVER_HPP
