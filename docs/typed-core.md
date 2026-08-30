# Typed-core interface

GitHub [#7](https://github.com/yona-lang/yona/issues/78). Typed core is the
in-process API that tools and non-LLVM backends consume without parser-private
headers or LLVM types.

**Status (2026-08-30):** `SemanticModel`
(`include/yona/Semantics/SemanticModel.h`) is the single C++ source of binding
identity, definitions, references, inferred types, effects, ownership, and
diagnostics for tools. `yls` consumes that model directly. The canonical C
adapter (`include/yona/TypedCore/Abi.h`), example pretty-print backend, and
`yonac --emit-typed-core` project the same semantic facts into a C-owned tree.
A wire format is still deferred.

## Ownership and lifetime

`YonaTypedCoreAnalyze` allocates a `YonaTypedCoreModule` and owns every string
and child array reachable from it. Pass the module to
`YonaTypedCoreDisposeModule` exactly once; every borrowed pointer becomes
invalid when it returns. `YonaTypedCorePrettyPrint` returns a separate
allocation disposed with `YonaTypedCoreDisposeString`. Both dispose functions
accept null.

Null source and allocation failure return null. Parse and semantic failures
return a module containing diagnostics. Positions are zero-based UTF-8 code
units on the source line, and ranges are half-open `[Start, End)`.

Separate analysis calls may run concurrently. A completed module is immutable
and may be read concurrently, but disposal must not overlap reads of the same
module or string.

The producer does not create an LLVM or code-generation session. Prelude and
imported contracts are read by the Semantics-owned `InterfaceCatalog`; the
public C header contains no LLVM or C++ types.

## Node model

Node kinds cover modules, functions, ADTs, constructors, bindings, imports,
case expressions, patterns, effects, and explicitly unsupported constructs.
Unsupported constructs (currently `try`/`catch`, trait declarations, and trait
instances) use `YonaTypedCoreNodeKindUnsupported` with a reason in `Detail`;
they are not silently dropped. Diagnostics are attached to the module and do
not replace its tree.

## Example backend

`src/TypedCore/PrettyPrint.c` includes only `yona/TypedCore/Abi.h` and writes a
deterministic textual summary. `yonac --emit-typed-core` prints that summary
and exits without LLVM code generation. Combine it with `-I`, `--sysroot`, and
`YONA_PATH` in the same way as a normal compile.

## Related

- `include/yona/Semantics/InterfaceCatalog.h` — canonical analysis-time
  `.yonai` catalog
- `include/yona/Semantics/SemanticModel.h` — shared C++ semantic model
- `include/yona/Lsp/Protocol.h` — editor-protocol value types
- `docs/superpowers/plans/2026-08-17-next-plan-of-action.md` Phase 4
- Formal dump consumer:
  `docs/superpowers/plans/2026-08-17-yona-rocq-formalization.md`
