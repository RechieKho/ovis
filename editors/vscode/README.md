# Ovis for VS Code

- Problems of `.ovis` files, checked when a file is opened or saved, without
  the model (a build settles what needs it).
- Quick fixes for unclear lines: each reading Ovis offers is a fix that
  writes `@[meaning: …]` above the line (Ctrl+. on the problem).
- Hover on a line to see how Ovis reads it; on a `@[meaning: …]` line, to
  see whether Ovis or you wrote it.
- `@[meaning: …]` and `@[note: …]` lines are shown faint (`ovis.dimSigils`).
- Syntax highlighting.

It runs `ovis lsp`; set `ovis.path` if `ovis` isn't on your `PATH`.

Install from this folder:

```sh
cd editors/vscode
npm install
npx @vscode/vsce package     # ovis-0.1.0.vsix
code --install-extension ovis-0.1.0.vsix
```
