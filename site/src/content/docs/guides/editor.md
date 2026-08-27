---
title: Editor and language server
description: Syntax highlighting and semantic features for Yona in VS Code and other LSP clients.
---

Yona ships a TextMate grammar and a language server named **`yls`**.

`yls` and the VS Code and Zed integrations do not currently expose
`yonac --require-effect-free`. Run that flag from the compiler CLI when you need
strict E0203 validation for effect freedom, finite-case coverage, and structural
size-change termination. Ordinary parse, type, refinement, and linearity
diagnostics remain shared between `yonac` and the language server.

## Zed

Install the **Yona** extension from Zed's Extensions panel, then install the
Yona toolchain so `yls` is on `PATH`. The extension starts `yls --stdio` and
uses the official Tree-sitter grammar. To select a particular server binary:

```json
{ "lsp": { "yls": { "binary": { "path": "/absolute/path/to/yls" } } } }
```

The marketplace submission is tracked at
https://github.com/zed-industries/extensions/pull/7348 until Zed merges it.

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
- Hover, go-to-definition, highlight, and completion still work on a
  prefix that parsed when the buffer is incomplete (`let answer = 42 in`,
  a trailing operator, unfinished `do` / `if` / `case`). The squiggle
  stays the original parse error; recovery does not replace it.
- Hover types, go-to-definition (same file, imported names, and
  `Module.fn` calls), find references, document highlight,
  rename (imported names are renamed only in the current file)
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

A Yona-written transport slice, `yls-yona`, speaks `Content-Length`
JSON-RPC over stdio (`initialize`, `didOpen`, `shutdown`, `exit`) using
`Std\IO.readExact` / `writeBytes`, `Std\Json`, and `Std\Utf16`. Hover and
definition still need the C++ `yls` binary, which remains the editor
default. Build it with CMake target `yls-yona`; smoke:
`python3 scripts/ci/smoke-yls-yona.py out/build/x64-debug-linux/yls-yona`.
On Windows, `readExact` / `writeBytes` put CRT stdio in binary mode so
`Content-Length` `\r\n` framing stays byte-exact.

## Grammar source of truth

Edit `site/grammars/yona.tmLanguage.json`, then:

```bash
./scripts/sync-yona-grammar.sh
./scripts/check-yona-grammar.sh
```

## Packaging and Marketplace

Build a VSIX locally (no publisher token required):

```bash
cd editors/vscode
npm ci
npm test
npm run vsix
```

Then **Extensions: Install from VSIX…** and choose `yona-<version>.vsix`.
Pull requests and pushes to `master`/`main` also build that artifact
(`vscode-extension` job in CMake Multi-Platform) and do not publish it.

Release CI publishes the same VSIX to the Visual Studio Marketplace when a
`v*` tag is pushed (Release workflow jobs `vscode-vsix` then
`publish-marketplace`). `vsce publish --packagePath` reads `VSCE_PAT` from
the environment. Open VSX (`publish-openvsx`) runs
`ovsx publish --packagePath` for publisher `yona-lang` with repository
secret `OVSX_PAT`. The job is still skipped if that secret is unset so
the workflow stays green. The `yona-lang` namespace is claimed on
[Open VSX](https://open-vsx.org/). Tag version must match
`editors/vscode/package.json`.

Manual publish (tokens in the environment, never committed):

```bash
npx vsce publish --packagePath yona-<version>.vsix
npx ovsx publish --packagePath yona-<version>.vsix
```

See `editors/vscode/README.md`.
