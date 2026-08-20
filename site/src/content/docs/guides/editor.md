---
title: Editor and language server
description: Syntax highlighting and semantic features for Yona in VS Code and other LSP clients.
---

Yona ships a TextMate grammar and a language server named **`yls`**.

## VS Code / Cursor

The extension lives in the compiler repository at `editors/vscode`.

1. Install the Yona toolchain so `yls` and `yonac` are on `PATH` (or set
   `YONA_HOME`).
2. Open the `editors/vscode` folder in VS Code and run `npm install` then
   `npm run compile`, or install a packaged `.vsix` when one is published.
3. Open a `.yona` or `.yonai` file.

The extension starts `yls --stdio`. If the server is missing, highlighting
still works and a warning is shown.

### Settings

| Setting | Meaning |
|---------|---------|
| `yona.languageServer.path` | Absolute path to `yls`. Empty: search `PATH`, then `$YONA_HOME/bin/yls`, then the directory that contains `yonac`. |
| `yona.trace.server` | `off`, `messages`, or `verbose` LSP tracing (vscode-languageclient). |

Do not hard-code Homebrew or `/usr/local` prefixes. Discovery uses the
environment and sibling binaries.

## Features

- Syntax highlighting (shared grammar with the documentation site)
- Comments, brackets, and `end`-aware indentation
- Diagnostics from parse, type, refinement, and linearity checking
- Hover types, go-to-definition, find references, rename (including function
  parameters and other pattern bindings in the same file)
- Modules that start with `#` / `##` documentation are analyzed as modules
  (same rule as `yonac`)
- Workspace folder roots are searched for modules; open buffers refresh when
  watched `.yona` / `.yonai` files change
- Completion, document/workspace symbols, semantic tokens
- Signature help, inlay hints, call hierarchy, explain-error code actions

## Other editors

Any LSP client can launch:

```bash
yls --stdio
```

Optional `-I path` adds a module search directory, matching `yonac -I`.

## Grammar source of truth

Edit `site/grammars/yona.tmLanguage.json`, then:

```bash
./scripts/sync-yona-grammar.sh
./scripts/check-yona-grammar.sh
```
