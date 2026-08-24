# Opaque exported types implementation plan

**Goal:** implement GitHub #6 with `export type T opaque`.

1. [x] Extend `ModuleDecl` and module parsing to record opaque type exports; reject
   malformed/duplicate export declarations.
2. [x] Emit `ADT T ... opaque` without `CTOR` records for opaque exports, and load
   that interface without registering constructors.
3. [x] Ensure parser/typechecker/codegen imports only see constructors serialized
   by the interface. Add producer/consumer tests for smart constructors,
   forbidden construction/pattern matching, and transparent compatibility.
4. [x] Update syntax/specification/type-system docs, roadmap, and changelog; run
   the focused test suite and module compilation fixtures.

**Acceptance:** a client can use `Token` through exported functions but cannot
spell or pattern-match `Token`'s constructor; a transparent `export type`
continues to expose constructors.
