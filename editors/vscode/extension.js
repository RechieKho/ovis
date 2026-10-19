// Starts `ovis lsp` for .ovis files (problems, quick fixes for unclear
// lines, hover), and shows the lines Ovis writes (`@[meaning: …]`,
// `@[note: …]`) faint, so the author's own lines stand out. VS Code can't
// fold a line away on its own, so they are dimmed instead
// (docs/proposals/quick-fix.md).
const vscode = require('vscode');
const { LanguageClient, TransportKind } = require('vscode-languageclient/node');

let client;
const sigil = /^\s*@\[(meaning|note):.*\]\s*$/;

function dimSigils(editor, decoration) {
  if (!editor || editor.document.languageId !== 'ovis') {
    return;
  }
  const enabled = vscode.workspace.getConfiguration('ovis').get('dimSigils', true);
  const ranges = [];
  if (enabled) {
    for (let line = 0; line < editor.document.lineCount; ++line) {
      const text = editor.document.lineAt(line);
      if (sigil.test(text.text)) {
        ranges.push(text.range);
      }
    }
  }
  editor.setDecorations(decoration, ranges);
}

function activate(context) {
  const command = vscode.workspace.getConfiguration('ovis').get('path', 'ovis');
  client = new LanguageClient(
    'ovis',
    'Ovis',
    { command, args: ['lsp'], transport: TransportKind.stdio },
    { documentSelector: [{ scheme: 'file', language: 'ovis' }] }
  );
  client.start();

  const decoration = vscode.window.createTextEditorDecorationType({
    opacity: '0.4',
    fontStyle: 'italic'
  });
  context.subscriptions.push(decoration);
  const refresh = () => vscode.window.visibleTextEditors.forEach(editor => dimSigils(editor, decoration));
  context.subscriptions.push(
    vscode.window.onDidChangeVisibleTextEditors(refresh),
    vscode.workspace.onDidChangeTextDocument(event => {
      vscode.window.visibleTextEditors
        .filter(editor => editor.document === event.document)
        .forEach(editor => dimSigils(editor, decoration));
    }),
    vscode.workspace.onDidChangeConfiguration(event => {
      if (event.affectsConfiguration('ovis.dimSigils')) {
        refresh();
      }
    })
  );
  refresh();
}

function deactivate() {
  return client ? client.stop() : undefined;
}

module.exports = { activate, deactivate };
