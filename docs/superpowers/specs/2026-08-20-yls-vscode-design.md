# Yona VS Code + `yls` design

Date: 2026-08-20

## Decision

Ship a C++23 `yls` language server over stdio JSON-RPC and a thin VS Code
extension. Semantic queries reuse `Parser`, `TypeChecker`,
`RefinementChecker`, `LinearityChecker`, and `DiagnosticEngine`. LLVM types
do not appear in `include/yona/Lsp/`.

## Documents

Open buffers live in memory. `didChange` replaces the full text. Module
search uses `YONA_PATH`, `YONA_HOME`, the document directory, and `lib/`
the same way `yonac` does.

## Features

Diagnostics, hover, definition (imports and `Module.fn` calls resolve to
the source `.yona` / `.yonai`), references, document highlight, completion,
document/workspace symbols, semantic tokens, rename (imported names stay
in the current file), signature help, inlay hints, call hierarchy
(prepare), and explain-code-action. On parse failure, published diagnostics
stay the original parse errors; hover, definition, highlight, and
completion walk a recovered prefix when one exists.

## Grammar

`site/grammars/yona.tmLanguage.json` is canonical.
`./scripts/sync-yona-grammar.sh` copies it into `editors/vscode/syntaxes/`.
`./scripts/check-yona-grammar.sh` fails CI on drift.

## Deferred: Yona-written `yls`

Do not start until this C++ server is the editor backend and #7 has an
architecture document. Stdlib gaps:

| Gap | Why | Suggested work |
|-----|-----|----------------|
| Real JSON values | LSP messages are objects/arrays | Planned `Std\Json` ADT |
| Frame I/O | `Content-Length` on stdin | `IO.readExact` via `read()`, not `File.readBytes` (`pread` fails on pipes) |
| UTF-16 | VS Code positions | `utf8OffsetToUtf16`; `String.length` is bytes today |
| Compiler queries | Cannot reimplement HM in Yona | `extern` to typed-core C ABI, or `yonac --analyze` |
| File watch | Optional | Planned `File.watch`, or keep editor `didChangeWatchedFiles` |
| CLI args | `yls --stdio` | Planned `Process.getArgs` |
