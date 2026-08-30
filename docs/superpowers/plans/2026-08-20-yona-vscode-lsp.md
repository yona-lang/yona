# Yona VS Code Extension and Language Server Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> `superpowers:subagent-driven-development` or `superpowers:executing-plans`
> to implement this plan task-by-task. Track progress with the checkboxes.

**Status:** Implemented (2026-08-20). Review fixes landed the same day:
`looks_like_module` matches `yonac` `#`/`##` skip; AST walk indexes
pattern bindings; JSON `\u` + surrogates; workspace roots +
`didChangeWatchedFiles`; signature-help look-behind; Windows binary stdio;
Codegen reused across `didChange`. Follow-up: LSP ranges are end-exclusive
(`Range::contains` / `overlaps`); `yonac` and `yls` share
`include/yona/Syntax/ModuleSource.h`. Cross-file definition for imports and FQN calls,
document highlight, and import-rename staying in the current buffer
(2026-08-21). Incremental / partial AST recovery on parse failure
(2026-08-21): hover, definition, highlight, and completion walk a recovered
prefix (` 0`, ` in 0`, ` end`, ` then 0 else 0`, truncate last token/line)
while published diagnostics stay the original parse errors. `IfExpr`
construction is null-safe so incomplete `if` no longer SIGSEGV. `try`/`catch`
consumes closing `end` (2026-08-21). Typed-core C ABI + `yonac --emit-typed-core`
  (2026-08-21). Local VSIX packaging (`npm run vsix`, CI artifact, no tokens)
  (2026-08-21). Marketplace CI publish on `v*` tags via `VSCE_PAT`
  (2026-08-21). Open VSX publish is fully wired (`OVSX_PAT` is set;
  `publish-openvsx` still skips if the secret is unset). A Yona-written
  editor-default server remains later work. `yls-yona` now uses
  `Std\Json.get` / `asString` / `asInt` (2026-08-21). GENFN remonomorphization
  isolates importer names so `import length from Std\String` cannot shadow
  `Std\Json.getPair`'s Prelude Array `length` (2026-08-21). Isolation
  copies the GENFN mangled name before clearing that map so sibling
  functions (`Std\Stream.range` from `naturals`) stay visible (2026-08-21).

**Goal:** Ship a production-quality VS Code/Cursor extension and C++ `yls`
language server covering Yona syntax and semantics, while evolving the
compiler's reusable analysis APIs toward typed-core issue
[#7](https://github.com/yona-lang/yona/issues/78).

**Architecture:** Keep the extension and server in the Yona monorepo. The
TypeScript extension is a thin `vscode-languageclient` host. The C++23 server
speaks LSP/JSON-RPC over stdio and reuses the existing lexer, parser,
typechecker, diagnostics, module interfaces, and source spans. Semantic query
types must not expose LLVM. A server written in Yona is deferred until full
JSON, raw stdio framing, UTF-16 conversion, and a typed-core C ABI exist.

**Tech stack:** C++23, CMake/Ninja, doctest, JSON-RPC 2.0/LSP 3.17,
TypeScript, current VS Code Extension API, `vscode-languageclient`, npm,
TextMate grammar.

## Global constraints

- Follow TDD for behavioral changes: failing test, minimal implementation,
  passing test, refactor.
- Keep one canonical TextMate grammar and verify generated/copied editor
  grammar equality in tests or CI.
- Analyze unsaved buffers in memory; never require writing them to disk.
- Convert UTF-8 byte offsets to LSP UTF-16 positions correctly.
- Resolve modules using the same input-directory, `-I`, `YONA_PATH`, sysroot,
  and packaged-stdlib rules as `yonac`.
- Keep LSP analysis independent of LLVM values and headers at its public
  boundary.
- Prefer full-document reparsing initially; incremental parsing is optional
  only after profiling.
- Do not add file-watching syscalls here; consume
  `workspace/didChangeWatchedFiles`.
- Update `docs/todo-list.md`, this plan, `CHANGELOG.md`, internal docs, and
  `site/src/content/docs/` in the same change.
- Run the repository-owned local quality command, the complete CMake build/test
  suite, and extension tests/lints before completion.

## Target repository layout

```text
editors/vscode/
  package.json
  package-lock.json
  language-configuration.json
  syntaxes/yona.tmLanguage.json
  src/extension.ts
  test/
  tsconfig.json

include/yona/Lsp/
  Analysis.h
  JsonRpc.h
  Protocol.h
  SemanticIndex.h
  Server.h
  Session.h
  Utf16.h

src/Lsp/
  Analysis.cpp
  JsonRpc.cpp
  SemanticIndex.cpp
  Server.cpp
  Session.cpp
  Utf16.cpp

cli/Yls.cpp
test/Lsp/LspTest.cpp
test/Lsp/
site/grammars/yona.tmLanguage.json
```

The exact split may be refined during implementation, but each file must keep
one responsibility: transport, protocol types, document state, compiler
analysis, semantic indexing, or request dispatch.

## Phase 0: Restore a trustworthy baseline

- [x] Reproduce and root-cause the `with fd = 0 in 42` parser SIGSEGV.
- [x] `with` parser crash fix (`parse_expr_until_in` + null checks).
- [x] Fix scoped `in` terminators without regressing membership or set removal
  (landed on master `a9b57ba`; not an `yls` change).
- [x] Fix zero-arity `perform Op ()` argument counting (landed on master).
- [x] Fix recursive-function effect-row rest inference (landed on master).
- [x] Fix GENFN calls to same-module plain-FN siblings (landed on master).
- [x] Fix `try`/`catch` closing-`end` consumption and reject trailing tokens
  (landed 2026-08-21).
- [x] Full `ctest` suite passes with the shared semantic model.

## Phase 1: Syntax extension

### Deliverables

- [x] Scaffold `editors/vscode/` with compile, lint, unit-test, and package
  scripts.
- [x] Register language id `yona`, `.yona` and `.yonai`, and settings.
- [x] Add comments, brackets, auto-closing pairs, surrounding pairs, folding,
  and newline/`end` indentation rules.
- [x] Audit the canonical TextMate grammar against `Lexer.cpp` keywords and
  the public specification.
- [x] Cover comments, interpolation, literals, symbols, module FQNs, imports,
  ADTs, records, traits, instances, deriving, effects, effect rows, `@borrow`,
  extern modifiers, operators, and parallel comprehensions.
- [x] Remove unsupported `daemon` syntax while retaining `as`.
- [x] Add grammar token presence tests in the extension unit suite.
- [x] Add `scripts/sync-yona-grammar.sh` and `scripts/check-yona-grammar.sh`.

## Phase 2: LSP transport and document analysis

### Compiler/tooling prerequisites

- [x] Add a frontend analysis facade that initializes Prelude, module search
  paths, parser, `TypeChecker`, `RefinementChecker`, and `LinearityChecker`.
- [x] `yls` calls `check_module` and the two overlays `yonac` skips.
- [x] Structured diagnostics via `publishDiagnostics` (stdout is JSON-RPC only).
- [x] Incremental / partial AST on expression parse failure (2026-08-21):
  keep original parse diagnostics; recover a walkable AST via suffixes and
  truncation so hover/definition/highlight/completion work on the prefix.

### Server deliverables

- [x] Add `yls` CMake target and install it beside `yonac`.
- [x] Implement bounded `Content-Length` framing and JSON-RPC 2.0.
- [x] Implement `initialize`, `initialized`, `shutdown`, and `exit`.
- [x] Implement `didOpen` / `didChange` / `didClose` (full-document sync).
- [x] UTF-8 ↔ UTF-16 mapping tests (ASCII, non-BMP, CRLF).
- [x] Publish parse and semantic diagnostics for unsaved buffers.
- [x] In-process Server handle tests (subprocess transcript optional later).

## Phase 3: Typed semantic index and navigation

- [x] Same-file name index: kind, definition/use spans, type string (`type_of`
  - `pretty_print`). No symbol-doc comments yet.
- [x] Hover, definition, references, rename (same-file name match).
- [x] Document symbols and workspace symbols.
- [x] Cross-file import / `.yonai` use→def (imports and FQN calls).
- [x] Cross-file rename safety (imported names stay in the current file)
  and document highlight.

This semantic model is the first real consumer and implementation slice of
typed-core issue #7. Its public API must not expose parser-private ownership or
LLVM types.

## Phase 4: Complete language intelligence

- [x] Completion: keywords + collected names/types from the current buffer.
- [x] Signature help from inferred type strings.
- [x] Semantic tokens (full).
- [x] Inlay hints for inferred binding types.
- [x] Call hierarchy prepare (incoming/outgoing empty until a call graph).
- [x] Code actions: explain error code (`yona.explain`).
- [x] No ad-hoc formatter.
- [x] Unsupported requests return empty / null.

## Phase 5: Packaging, CI, and documentation

- [x] Install/package `yls` in Homebrew, DEB/RPM, AUR generator, Docker,
  Windows MSI staging, and release tarballs.
- [x] Extension setting `yona.languageServer.path`; discovery via `PATH`,
  `YONA_HOME`, sibling of `yonac`.
- [x] Dedicated multi-OS `yls` subprocess smoke in CI
  (`scripts/ci/smoke_yls.py` after the matrix build).
- [x] Grammar drift check on Linux CI.
- [x] Site Tools page, CLI/`install` docs, changelog, todo-list, design spec.
- [x] Tooling LSP item marked done; Marketplace CI publish is on
  `v*` tags (`VSCE_PAT`). Open VSX publish is fully wired (`OVSX_PAT`).
- [x] Local VSIX packaging: `package` / `vsix` scripts, README install-from-vsix,
  LICENSE, CI `vscode-extension` job (artifact only). Human still publishes.
- [x] Marketplace CI publish on Release `v*` tags (`publish-marketplace` +
  `VSCE_PAT`). Open VSX (`ovsx publish --packagePath`) is wired with
  `OVSX_PAT` and still skips if that secret is unset (2026-08-21).

## Deferred Yona-written server prerequisites

- [x] Implement the planned recursive `Std\Json.Json` ADT with object/array
  parse and stringify support. (`lib/Std/Json.yona` + C ABI
  `include/yona/Runtime/Codecs/Json.h`, 2026-08-21)
- [x] Add pipe-safe `Std\Io.readExact(fd, n)` using stream `read`, not
  seek-based `File.readBytes`/`pread`. (`YonaStdIoReadExact`, 2026-08-21)
- [x] Expose UTF-8 to UTF-16 offset conversion. (`Std\Utf16` +
  `include/yona/Runtime/Codecs/Utf16.h`, 2026-08-21)
- [x] Expose typed-core through the canonical C ABI
  (`include/yona/TypedCore/Abi.h`, 2026-08-21). A serialized wire format remains
  out of scope.
- [x] Add `Std\Process.getArgs`.

File watching can remain editor-driven. A Yona server rewrite is a separate
project and must preserve this server's protocol conformance suite.

**2026-08-21:** `yls-yona` (`tools/yls/main.yona`) is a transport-only stdio
slice (`initialize` / `didOpen` / `shutdown` / `exit`). C++ `yls` remains
the editor default. Hover/definition still need `yona_lib` typed-core.
**2026-08-21 (later):** Windows CI `smoke_yls_yona` failed because CRT
stdin/stdout stayed in text mode; `readExact`/`writeBytes` now `_setmode`
binary like C++ `yls`. Shipped in **v0.1.6** so Marketplace / Open VSX /
GitHub Release can run (v0.1.5 Release died on that smoke; tags stay put).

## Verification gate

- [x] `cmake --preset x64-debug-linux`
- [x] `cmake --build --preset build-debug-linux`
- [x] `./out/build/x64-debug-linux/tests` — 391/391 passed (2026-08-20, after
  rebase onto `a9b57ba`).
- [x] Focused `yls` unit and subprocess protocol tests pass
  (`--source-file='*LspTest.cpp'` 41/41 after parse recovery;
  `scripts/ci/smoke_yls.py`).
- [x] Extension compile, lint, and unit tests pass (`editors/vscode`).
- [x] Formatting is enforced by the repository-owned local quality command.
- [x] IDE diagnostics report no newly introduced errors.
- [x] First-party LSP, extension, and smoke-test files pass the local quality
  checks.
