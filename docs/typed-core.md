# Typed-core interface

GitHub [#7](https://github.com/yona-lang/yona/issues/78). This is the
versioned in-process API that tools and non-LLVM backends consume without
parser-private headers or LLVM types.

**Status (2026-08-21):** thin slice. C++ query types (`include/typed_core/Query.h`)
feed `yls`. The versioned C ABI (`include/typed_core/abi.h`), example
pretty-print backend, and `yonac --emit-typed-core` dump resolved names,
inferred types, effect rows, linearity, and source spans. A wire format is
still deferred.

## Ownership and lifetime

`yona_tc_analyze` allocates a `YonaTcModule`. Every string and child array
inside it is owned by that module. The consumer must call
`yona_tc_module_free` exactly once. Pointers are invalid after free.
`yona_tc_pretty_print` returns a separate malloc'd buffer freed with
`yona_tc_string_free`.

The producer may use LLVM internally (prelude load). The public C header
does not include LLVM or C++ types.

## Versioning

`YONA_TYPED_CORE_ABI_VERSION` is `1`. `yona_tc_abi_version()` returns the
same value. Changing struct layout or existing function signatures requires
a version bump. New functions may appear in a later version; v1 consumers
ignore them.

Positions are 0-based UTF-8 code units on the source line. Ranges are
half-open `[start, end)`. This is independent of LSP UTF-16 positions in
`Query.h`.

## Compatibility

- v1 kinds: module, function, ADT, constructor, binding, import, case,
  pattern, effect, unsupported.
- Unsupported constructs (today: `try`/`catch`, trait decls/instances) are
  emitted as `YONA_TC_KIND_UNSUPPORTED` with a reason in `detail`. They are
  not silently dropped.
- Diagnostics are attached to the module; they do not replace the tree.

## Example backend

`src/typed_core/PrettyPrint.c` includes only `typed_core/abi.h` and writes a
deterministic textual summary. `yonac --emit-typed-core` prints that summary
and exits without LLVM codegen. Combine it with `-I` / `--sysroot` /
`YONA_PATH` the same way as a normal compile.

## Related

- `include/typed_core/Query.h` — C++ hover/diagnostic/symbol types for `yls`
- `docs/superpowers/plans/2026-08-17-next-plan-of-action.md` Phase 4
- Formal dump consumer: `docs/superpowers/plans/2026-08-17-yona-rocq-formalization.md`
