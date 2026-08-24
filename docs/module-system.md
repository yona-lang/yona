# Yona Module System

## Overview

Modules are top-level compilation units in Yona. They compile to native object files (`.o`) with C-ABI exports, linked together via standard system linkers. Modules are **not** first-class values — they cannot be passed as arguments, returned from functions, or stored in data structures.

Cross-module type safety is ensured via interface files (`.yonai`) that describe exported function signatures and ADT definitions.

## Module Definition

```yona
module Package\ModuleName

export function1, function2
export type MyType

type MyType a = Constructor1 a | Constructor2

function1 x = x + 1

function2 a b = a * b

# Private helper (not exported)
helper x = x * 2
```

### Structure

- **Module name**: FQN with backslash-separated packages (`Std\List`, `Data\Collections\Map`)
- **Exports**: `export` statements listing public functions; `export type Name` exports a type and all its constructors; `export type Name opaque` exports only its nominal type, so clients use public smart constructors and observers
- **Body**: ADT type declarations and function definitions
- **Terminator**: EOF (no `end` keyword needed)

### Name Mangling

Exported functions use C-ABI name mangling for cross-language linking:

| Yona | Mangled symbol |
|------|---------------|
| `Std\List::map` | `yona_Std_List__map` |
| `Test\Math::add` | `yona_Test_Math__add` |

This enables linking Yona modules with C, Rust, and Go code.

## Re-exports

A module can re-export functions from other modules. Re-exported symbols appear as if they were defined in the re-exporting module — importers only need to depend on the re-exporting module.

```yona
module Std\Prelude

export add, mul from Std\Arith
export map, filter from Std\List
export double

double x = add x x   # can use re-exported functions in own definitions
```

Syntax: `export name1, name2 from Module\Name` for re-exports. Each `export` statement handles one group of exports or re-exports. The `from` keyword distinguishes re-exports from direct exports.

Re-exported functions are compiled as thin forwarding wrappers that call through to the source module. Both the re-exporting module's object file AND the source module's object file must be linked.

## Import Expressions

Imports are expressions — they bring names into scope for a body expression.

### Selective Import

```yona
import map, filter from Std\List in
  map (\x -> x + 1) (filter (\x -> x > 0) xs)
```

### Import with Aliases

```yona
import map as listMap from Std\List in
  listMap (\x -> x * 2) [1, 2, 3]
```

### Wildcard Import

Imports all exported names from a module:

```yona
import Std\List in
  map (\x -> x + 1) [1, 2, 3]
```

### FQN Calls (No Import Needed)

Call a module function directly using `Module::function` syntax:

```yona
Std\List::map (\x -> x + 1) [1, 2, 3]
```

This auto-loads the module interface without an explicit import.

### Multiple Import Clauses

```yona
import
  map from Std\List,
  trim from Std\String
in
  map trim ["  hello ", " world  "]
```

## Compilation Pipeline

### Module Compilation

```
Source (.yona)
    |
    v
Parser.parse_module() --> ModuleDecl AST
    |
    v
Codegen.compile_module()
    |-- Register ADT constructors
    |-- Compile exported functions (with inferred types)
    |-- Non-exported functions: deferred (compile at call site)
    |
    +-- emit_object_file() --> .o (native object, C-ABI exports)
    +-- emit_interface_file() --> .yonai (type metadata)
```

### Expression Compilation

```
Source (.yona)
    |
    v
Parser.parse_input() --> Expression AST
    |
    v
Codegen.compile() --> wraps in main(), links with runtime
    |
    v
Executable
```

### CLI

```bash
# Compile a module (produces .o + .yonai)
yonac -o MyModule.o MyModule.yona

# Compile an expression (produces executable)
yonac -o program MyProgram.yona

# Specify module search paths
yonac -I ./lib -I /usr/local/lib/yona -o program main.yona

# Print LLVM IR
yonac --emit-ir MyModule.yona

# Compile with DWARF debug info (for GDB/LLDB)
yonac -g -o program main.yona

# Warning flags
yonac --Wall -o program main.yona        # enable common warnings
yonac --Wextra --Werror -o program main.yona  # all warnings, treat as errors
```

## Interface Files (.yonai)

Text-based format describing module exports:

```
ADT TypeName variant_count max_arity [recursive]
CTOR ConstructorName tag arity [fields name:TYPE ...]
FN yona_Pkg_Mod__func param_count TYPE1 TYPE2 -> RETURN_TYPE [borrow MASK]
AFN yona_Pkg_Mod__func param_count TYPE1 -> RETURN_TYPE   (async, runs in thread pool)
TRAIT TraitName type_param method_count
  METHOD method_name
INSTANCE TraitName TypeName
  IMPL method_name mangled_name
GENFN_BEGIN mangled_name local_name
  ... function source text ...
GENFN_END
```

Types: `INT`, `FLOAT`, `BOOL`, `STRING`, `SEQ`, `TUPLE`, `SET`, `DICT`, `ADT`, `FUNCTION`, `SYMBOL`

`FN` entries can include an optional `borrow` bitmask. Each bit maps to a
parameter position; `1` means the compiler inferred that the heap parameter is
read-only and non-escaping, so importers can avoid the caller-side defensive
`rc_inc` and the callee can skip owned cleanup for that parameter. Omitted
borrow metadata is equivalent to all zeros, i.e. the normal callee-owns
convention.

Borrow inference is intentionally conservative across module boundaries:
arguments forwarded to another function only remain borrowed when that callee
is known to borrow the corresponding parameter, functions that can directly
`raise` keep owned unwind cleanup, and anonymous heap temporaries passed to a
borrowed parameter are released by the caller after the call.

### Cross-module Generics (GENFN)

Exported functions include their source text in `.yonai` files via `GENFN_BEGIN`/`GENFN_END` blocks. When an importing module calls a function with argument types that differ from the pre-compiled signature, the source is re-parsed and compiled locally with the actual call-site types (on-demand monomorphization).

This enables generic functions to work correctly across module boundaries — the importing module gets a type-specialized copy rather than being forced to use the module's single pre-compiled version.

Trait instance methods are compiled with external linkage so that re-parsed GENFN bodies can call them via normal trait dispatch. The `.yonai` file includes `FN` metadata for trait methods alongside `INSTANCE`/`IMPL` entries.

### Module Search

When resolving `import Std\List`, the compiler searches:
1. Paths from `-I` CLI flags
2. Directory containing the input file
3. Current working directory

Looks for `Std/List.yonai` in each search path.

## Standard Library Modules

| Module | Functions | Description |
|--------|-----------|-------------|
| `Std\Option` | 7 | `Some`, `None`, `map`, `flatMap`, `unwrapOr`, etc. |
| `Std\Result` | 8 | `Ok`, `Err`, `map`, `flatMap`, `unwrapOr`, etc. |
| `Std\List` | 17 | `map`, `filter`, `foldl`, `take`, `zip`, `flatten`, etc. |
| `Std\Tuple` | 4 | `first`, `second`, `swap`, `fromList` |
| `Std\Range` | 6 | `range`, `map`, `toList`, `contains`, etc. |
| `Std\Test` | 3 | `assertEqual`, `assertNotEqual`, `assertTrue` |
| `Std\Math` | 7 | `abs`, `max`, `min`, `factorial`, `sqrt`, `sin`, `cos` |
| `Std\String` | 6 | `length`, `toUpperCase`, `toLowerCase`, `trim`, `indexOf`, `contains` |

## C FFI

Import C functions using `extern` declarations:

```yona
module MyModule

export wrapper

extern puts : String -> Int
extern usleep : Int -> Int

wrapper msg = puts msg
```

Async C functions (run in thread pool):

```yona
extern async slowCompute : Int -> Int
```

## Current Limitations

- **No circular dependencies**: Modules must be compiled in dependency order
- **No package manager**: Manual compilation and path management
- **Cross-module generics**: GENFN re-parse only triggers when call-site arg types differ from the pre-compiled signature; functions referencing module-private names (closures over local variables) fall back to the extern path
