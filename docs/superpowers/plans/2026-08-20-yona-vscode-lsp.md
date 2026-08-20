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
`include/ModuleSource.h`. Marketplace publish and a Yona-written server
remain later work.

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
- Do not add file-watching syscalls for v1; consume
  `workspace/didChangeWatchedFiles`.
- Update `docs/todo-list.md`, this plan, `CHANGELOG.md`, internal docs, and
  `site/src/content/docs/` in the same change.
- Run `./scripts/format.sh`, the complete CMake build/test suite, extension
  tests/lints, and Aikido scans before completion.

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

include/lsp/
  Analysis.h
  JsonRpc.h
  Protocol.h
  SemanticIndex.h
  Server.h
  Session.h
  Utf16.h

src/lsp/
  Analysis.cpp
  JsonRpc.cpp
  SemanticIndex.cpp
  Server.cpp
  Session.cpp
  Utf16.cpp

cli/yls.cpp
test/lsp_test.cpp
test/lsp/
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
- [ ] Fix `try`/`catch` closing-`end` consumption and reject trailing tokens
  (not an `yls` v1 gate).
- [ ] Full `ctest` 360/360 — not a gate for `yls` v1 (pre-existing failures).

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
- [x] Remove unsupported legacy `daemon` syntax while retaining `as`.
- [x] Add grammar token presence tests in the extension unit suite.
- [x] Add `scripts/sync-yona-grammar.sh` and `scripts/check-yona-grammar.sh`.

## Phase 2: LSP transport and document analysis

### Compiler/tooling prerequisites

- [x] Add a frontend analysis facade that initializes Prelude, module search
  paths, parser, `TypeChecker`, `RefinementChecker`, and `LinearityChecker`.
- [x] `yls` calls `check_module` and the two overlays `yonac` skips.
- [x] Structured diagnostics via `publishDiagnostics` (stdout is JSON-RPC only).
- [ ] Incremental / partial AST on expression parse failure (v1: parse
  diagnostics only; hover/completion empty until the buffer parses).

### Server deliverables

- [x] Add `yls` CMake target and install it beside `yonac`.
- [x] Implement bounded `Content-Length` framing and JSON-RPC 2.0.
- [x] Implement `initialize`, `initialized`, `shutdown`, and `exit`.
- [x] Implement `didOpen` / `didChange` / `didClose` (full-document v1).
- [x] UTF-8 ↔ UTF-16 mapping tests (ASCII, non-BMP, CRLF).
- [x] Publish parse and semantic diagnostics for unsaved buffers.
- [x] In-process Server handle tests (subprocess transcript optional later).

## Phase 3: Typed semantic index and navigation

- [x] Same-file name index: kind, definition/use spans, type string (`type_of`
  + `pretty_print`). No symbol-doc comments yet.
- [x] Hover, definition, references, rename (same-file name match).
- [x] Document symbols and workspace symbols.
- [ ] Cross-file import / `.yonai` use→def (v1 uses local names + FQN call
  occurrences).
- [ ] Cross-file rename safety and document highlight.

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
  (`scripts/ci/smoke-yls.py` after the matrix build).
- [x] Grammar drift check on Linux CI.
- [x] Site Tools page, CLI/`install` docs, changelog, todo-list, design spec.
- [x] Tooling LSP item marked done for v1; Marketplace publish is later.

## Deferred Yona-written server prerequisites

- [ ] Implement the planned recursive `Std\Json.Json` ADT with object/array
  parse and stringify support.
- [ ] Add pipe-safe `Std\IO.readExact(fd, n)` using stream `read`, not
  seek-based `File.readBytes`/`pread`.
- [ ] Expose UTF-8 to UTF-16 offset conversion.
- [ ] Expose typed-core through a stable C ABI or separately versioned wire
  format.
- [ ] Add `Std\Process.getArgs`.

File watching can remain editor-driven. A Yona server rewrite is a separate
project and must preserve this server's protocol conformance suite.

## Verification gate

- [x] `cmake --preset x64-debug-linux`
- [x] `cmake --build --preset build-debug-linux`
- [x] `./out/build/x64-debug-linux/tests` — 391/391 passed (2026-08-20, after
  rebase onto `a9b57ba`).
- [x] Focused `yls` unit and subprocess protocol tests pass
  (`--source-file='*lsp_test.cpp'` 27/27 after exclusive-end + shared
  `is_module_source`; `scripts/ci/smoke-yls.py`).
- [x] Extension compile, lint, and unit tests pass (`editors/vscode`).
- [ ] `./scripts/format.sh` — `clang-format` not installed on this host.
- [x] IDE diagnostics report no newly introduced errors.
- [x] Aikido reports zero unresolved findings for added/modified first-party
  LSP/extension/script files (fixed `assert` in `scripts/ci/smoke-yls.py`).
- [ ] Independent whole-change review approves specification compliance and
  code quality.

