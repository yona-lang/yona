# Trait-Aware LSP and Zed Integration Design

**Status:** Approved design, pending implementation.

## Goal

Make Yona's foundational trait system fully visible through the editor-neutral
`yls` language server, preserve VS Code as a thin `yls --stdio` client, and
ship a first-class Zed extension that uses the same server and a dedicated
Tree-sitter grammar.

## Scope

The work covers the trait and conversion features introduced by the
foundational-traits program:

- declarations of traits, methods, superclasses, and instances;
- complete multi-parameter instance heads and constraints;
- `Eq`, `Ord`, `Hash`, `Show`, collection/algebra traits, `From`, `TryFrom`,
  `Parse`, `Send`, and `Shareable`;
- the `Ordering`, `ConvertError`, and `ParseError` ADTs; and
- diagnostics for absent or ambiguous trait instances.

It does not add a formatter, a second language server, editor-specific type
resolution, or a Yona rewrite of `yls`.

## Architecture

`yls` remains the sole semantic authority. Its analysis facade builds a typed
symbol index from source, imported `.yonai` contracts, and the Prelude. A
symbol records its declaration category, full display type/signature, source
range, declaration location, and semantic relationships such as a method's
owning trait or an instance's trait head.

All editor features consume that model:

| Feature | Trait-aware result |
|---|---|
| Hover | declaration category, complete constrained signature, trait head, and superclass requirements |
| Signature help | instantiated or declared method signature, including target/source witness order |
| Navigation | trait, method, instance, and imported-interface declaration locations |
| References and rename | semantic occurrences of declarations, methods, and instance trait names without textual false positives |
| Completion and symbols | traits, methods, instances, and public imported contracts with useful detail/kind metadata |
| Semantic tokens | dedicated trait/type/method declaration and use classifications where LSP supports them |
| Diagnostics/actions | existing compiler diagnostic plus safe explain-instance action that opens the relevant error explanation; no speculative instance-generation edits |

The server keeps full-document reparsing. If a document is incomplete, it uses
the existing recovered prefix only for semantic queries while publishing the
original parse diagnostics. All ranges remain UTF-16 and end-exclusive.

## Editor Clients

`editors/vscode` stays a transport/configuration host. It must not interpret
Yona traits or construct diagnostics. Its tests assert capability negotiation,
server discovery, grammar synchronization, and forwarding of the new semantic
features.

`yona-lang/tree-sitter-yona` is the standalone parser and query repository
required by Zed. `yona-lang/zed-yona` is the standalone Zed extension package.
It declares Yona file types, pins the grammar repository revision, launches
`yls --stdio`, exposes a configurable server path, and supplies deterministic
discovery: configured Zed `lsp.yls.binary.path`, then `PATH`. Its manifest and
CI checks validate the package structure without requiring a running GUI.

## Testing

The C++ LSP suite owns semantic protocol regressions:

- local source and imported `.yonai` trait declarations;
- constrained and multi-parameter signatures;
- `Ordering` / conversion ADTs and `Send` / `Shareable` marker diagnostics;
- hover, completion, definitions, references, rename, tokens, and signature
  help at UTF-16 positions;
- incomplete buffers and full-document replacement via `didChange`; and
- JSON-RPC responses for every new request path.

The VS Code test suite remains responsible for extension compilation, grammar
sync, and server configuration. Zed receives manifest/grammar/discovery smoke
checks from repository CI. Both editors are validated against the same
`yls --stdio` executable rather than duplicated mock servers.

## Documentation and editor behavior

The editor guide documents both clients, their installation and configuration,
and the exact supported semantic features. The CLI reference describes
editor-neutral `yls` usage. `docs/todo-list.md`, the implementation plan,
`CHANGELOG.md`, generated API documentation where relevant, and the public
site change in the same implementation series.

Existing LSP clients remain compatible: new capabilities are additive, and
unsupported client requests continue to receive the established empty/null
response.
