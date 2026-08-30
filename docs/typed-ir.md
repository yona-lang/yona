# Typed IR

`yona_typed_ir` is the ownership-explicit boundary between semantic analysis
and backend lowering. It is a compiled component, not a header-only dependency
marker.

The representation owns modules, functions, and function-local `ValueId`
values. Every value carries copied semantic type/effect facts plus both its
semantic ownership (`Unrestricted` or `Linear`) and its runtime state
(`Trivial`, `Borrowed`, `Owned`, or `Transferred`). Builders reject unknown
facts, implicit linear ownership, invalid transfers, duplicate functions, and
transferred results.

`buildEntryModule` copies the root facts of an immutable `SemanticModel` into a
single-entry module. It does not retain the AST, type checker, or diagnostics.
This gives tools and future lowering passes a stable SemanticModel -> Typed IR
boundary.

`lowerTypedIrModule` is the direct Typed IR -> LLVM entry. It never parses
source and does not include syntax APIs. The initial canonical subset emits
Unit, Bool, Int, and Float constants plus identity-style scalar parameter
returns. Unsupported instructions and non-trivial ownership fail explicitly;
they are not widened to an untyped value or routed through the AST codegen.

The next backend steps should add typed instructions and ownership operations
to this representation, then move the remaining AST lowering behind the same
entry point. Syntax and semantics must never depend on Typed IR or Codegen.
