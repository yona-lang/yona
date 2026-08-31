# Type System & Codegen Architecture

## Design Principles

1. **No boxing.** Monomorphization via deferred compilation at call sites.
2. **Homogeneous collections.** `Seq<Int>`, `Dict<String, Int>`, `Set<Symbol>`.
3. **Symbols are i64 IDs.** Interned at compile time, compared with `icmp eq`.
4. **Algebraic data types.** `type Option a = Some a | None` with named constructors.
5. **Constructors are first-class functions.** Partially applicable, HOF-compatible.
6. **Interface files.** `.yonai` sidecar files carry type metadata for cross-module linking.

## Type Representations

| Type | LLVM type | CType | Storage |
|------|-----------|-------|---------|
| Int | `i64` | INT | register |
| Float | `double` | FLOAT | register |
| Bool | `i1` | BOOL | register |
| String | `ptr` | STRING | heap (char*) |
| Symbol | `i64` | SYMBOL | register (interned ID) |
| Seq\<T\> | `ptr` | SEQ | heap `[count, T, T, ...]` |
| Set\<T\> | `ptr` | SET | heap `[count, T, T, ...]` |
| Dict\<K,V\> | `ptr` | DICT | heap `[count, K, V, K, V, ...]` |
| Tuple | `{T1, T2, ...}` | TUPLE | struct (register/stack) |
| Function | `ptr` | FUNCTION | function pointer |
| ADT (non-recursive) | `{i8, i64, ...}` | ADT | struct (register/stack) |
| ADT (recursive) | `ptr` | ADT | heap via runtime |
| Promise | `ptr` | PROMISE | heap |

## Algebraic Data Types

### Syntax

```yona
type Option a = Some a | None
type Result a e = Ok a | Err e
type Color = Red | Green | Blue
type List a = Cons a (List a) | Nil
```

Module-level only. Multi-line via `|` continuation.

### Compiled Representation

**Non-recursive** (Option, Result, Color): flat LLVM struct `{i8 tag, i64 payload...}`.
- `Some 42` → `{i8 0, i64 42}` via `insertvalue`
- `None` → `{i8 1, undef}` (payload never read)
- Pattern match: `extractvalue` tag, `icmp eq`, `extractvalue` payload

**Recursive** (List, Tree): heap-allocated via C runtime.
- `Cons 1 Nil` → `rt_adt_alloc(0, 2)` + `rt_adt_set_field(node, 0, 1)` + `rt_adt_set_field(node, 1, nil_ptr)`
- Pattern match: `rt_adt_get_tag(node)`, `rt_adt_get_field(node, idx)`
- Self-referential fields stored as `i64` (pointer cast to int), recovered via `inttoptr`

### Constructors

Zero-arity (None, Nil): produce constant ADT value directly.
N-arity (Some, Cons): deferred function — compiled at call site with concrete arg types.
Constructors are first-class: `map Some [1, 2, 3]` works.

### Interface Files (.yonai)

When compiling a module, yonac emits `.yonai` alongside `.o`:

```
ADT Option 2 1
CTOR Some 0 1
CTOR None 1 0
ADT List 2 2 recursive
CTOR Cons 0 2
CTOR Nil 1 0
FN YonaStdOptionIsSome 1 ADT -> BOOL
FN YonaStdUserInspectName 1 STRING -> INT borrow 1
```

Importers read `.yonai` to get constructor info and function signatures.
The optional `borrow` bitmask records read-only heap parameters;
`borrow 1` means the first parameter is borrowed, while omitted metadata means
all parameters use the normal owned/callee-consumes convention.
Function bodies infer this metadata when a parameter is non-escaping. Native
declarations, which have no analyzable body, may state the same ABI contract
explicitly with `borrow "01..."` after the optional C symbol. The analysis stays
conservative for forwarding calls and exception paths: forwarded arguments
inherit borrow only from a known borrowed callee parameter, functions that can
directly `raise` keep owned cleanup, and anonymous borrowed temporaries are
cleaned up by the caller after the call. Thread-pool `async` and native-task
externs reject explicit masks until their task contexts pin borrowed arguments;
synchronous and io-completion externs support them.
`yonac -I lib main.yona` searches `lib/` for `.yonai` files.

## Remaining Work

- **Exhaustiveness checking**: warn when case doesn't cover all ADT constructors
- **Records**: named LLVM structs, field access, functional update
- **Type error checking**: validate types during codegen, clear error messages
- **Type classes**: dictionary passing (future)
