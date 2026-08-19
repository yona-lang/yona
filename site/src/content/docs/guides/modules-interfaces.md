---
title: Modules and interfaces
description: How Yona modules compile to native object files with C-ABI exports, what .yonai interface files carry, and how cross-module generics, search paths, and the C FFI work.
---

Modules are Yona's compilation units. Each module compiles to a **native object
file** (`.o`) with C-ABI exports plus a **text interface file** (`.yonai`) that
carries the type-level metadata other modules need to call it safely. This page
covers the compilation model in depth; for a first introduction to writing and
importing modules, see [Modules](/learn/modules/).

## Declarations and exports, briefly

A module is a top-level declaration — not an expression. It names itself with a
backslash-separated fully qualified name, lists its exports, and ends at
end-of-file (no `end` keyword):

```yona
module Acme\Geometry

export area, scale
export type Shape

type Shape = Circle Float | Rect Float Float

area shape = case shape of
    Circle r -> 3.141592653589793 * r * r
    Rect w h -> w * h
end

scale factor xs = [factor * x for x = xs]

# Private helper — not exported, invisible to importers
half x = x / 2
```

Export forms:

- `export f, g` — export functions by name.
- `export type Name` — export a type and **all** of its constructors.
- `export f, g from Other\Module` — re-export: importers see `f` and `g` as if
  this module defined them. Re-exports compile to thin forwarding wrappers, so
  both object files must be present when linking from foreign code.

Importers have three styles — selective, wildcard, and fully qualified calls:

```yona
import area from Acme\Geometry in area (Rect 3.0 4.0)   # selective
# => 12.0

import Acme\Geometry in area (Circle 1.0)               # wildcard

Acme\Geometry::scale 2 [1, 2, 3]                        # FQN, no import needed
# => [2, 4, 6]
```

## The compilation model

Compiling a file that contains a `module` declaration produces two artifacts:

```bash
yonac -o Geometry.o Geometry.yona
# Produces: Geometry.o (native object) + Geometry.yonai (interface)
```

Exported functions receive C-ABI symbols using a fixed mangling scheme —
`yona_` + the FQN with `\` replaced by `_` + `__` + the function name:

| Yona name | Linker symbol |
|-----------|---------------|
| `Acme\Geometry::area` | `yona_Acme_Geometry__area` |
| `Std\List::map` | `yona_Std_List__map` |

Because the exports are plain C symbols, a Yona module can be linked into C,
Rust, or Go programs with the system linker.

Implementation note. When `yonac` compiles a *program* (an expression file)
that imports Yona modules, it resolves the imports through the `.yonai`
interfaces and compiles the needed function bodies from the source text
embedded in those interfaces directly into the program's own LLVM module. The
final link line is the program object, the Yona runtime, and the Prelude
object. The module `.o` exists for the foreign-linking case — calling Yona
from C or another language's build system — not because every Yona-to-Yona
call goes through it.

## Interface files (`.yonai`)

The interface file is a line-oriented text format. It carries everything the
compiler needs to type-check and compile calls into the module:

```
ADT Shape 2 2
CTOR Circle 0 1 fields _0:FLOAT
CTOR Rect 1 2 fields _0:FLOAT _1:FLOAT
FN yona_Acme_Geometry__area 1 ADT -> FLOAT
FN yona_Std_Channel__channel 1 INT -> TUPLE LINEAR LINEAR
FN yona_Std_GPU__raiseGpu 1 ADT -> UNIT effects Gpu.deviceLost,Gpu.fail,Gpu.oom
AFN yona_Pkg_Mod__slow 1 INT -> INT
TRAIT Show a 1
  METHOD show
INSTANCE Show Int
  IMPL show yona_Prelude__Show_Int__show
GENFN_BEGIN yona_Acme_Geometry__scale scale
scale factor xs = [factor * x for x = xs]
GENFN_END
```

Line kinds and what they carry:

- **`ADT` / `CTOR`** — exported type shapes: variant count, arities, field
  types, and whether the type is recursive (recursive ADTs are heap-allocated).
- **`FN`** — an exported function: mangled symbol, parameter count, parameter
  and return types. Extra annotations overlay richer type information on the
  base signature:
  - `LINEAR` in a type position marks a linearity overlay — the value must be
    consumed exactly once (channel endpoints, file handles).
  - `effects Label,...` records a closed effect row so a caller's
    `handle` type-checks across the module boundary. `effects | hof`
    marks an open rest whose first parameter is a function
    (`apply f x = f x`); the importer reconstructs that shape so the
    argument's effects still propagate.
  - `borrow MASK` is a bitmask of parameters the compiler proved are read-only
    and non-escaping; importers skip the caller-side reference-count increment
    for those positions.
- **`AFN`** — an async function (`extern async` or async export): calls return
  a `Promise` and run on the thread pool, transparently awaited at use sites.
- **`TRAIT` / `INSTANCE` / `IMPL`** — trait definitions and instance method
  symbols for cross-module trait dispatch.
- **`GENFN_BEGIN` … `GENFN_END`** — the exported function's *source text*,
  used for cross-module monomorphization (next section).

Implementation note. Borrow inference is conservative across module
boundaries: a forwarded argument only stays borrowed when the callee is known
to borrow that parameter, and functions that can `raise` keep owned unwind
cleanup. An omitted `borrow` mask means the normal callee-owns convention.

## Cross-module generics (GENFN)

Yona compiles functions by **monomorphization**: a function body is compiled
at the call site, where the concrete argument types are known. A precompiled
module has already fixed one signature per export — so what happens when your
call site uses different types?

The `.yonai` embeds each exported function's source text in a
`GENFN_BEGIN`/`GENFN_END` block. When the call-site argument types differ from
the precompiled signature, the compiler re-parses that source and compiles a
type-specialized copy **locally, in the caller**. You get the same
monomorphized code quality as if the function had been defined in your own
file:

```yona
# Acme\Numeric defines and exports:   double x = x + x
# Precompiled once with the inferred signature Int -> Int.

import double from Acme\Numeric in double 21
# => 42        # matches the precompiled signature: direct extern call

import double from Acme\Numeric in double 1.25
# => 2.5       # Float call site: GENFN source re-compiled for Float
```

Trait instance methods are compiled with external linkage precisely so that
re-parsed GENFN bodies can call them through normal trait dispatch.

**Current limitation.** A re-parsed GENFN body can only reference names that
are visible to the importer: other exports, Prelude names, and stdlib imports.
An exported function that calls a *private* module helper currently fails at
the import site with `E0104 undefined function`. Until that is fixed, export
every function that your exports depend on.

## Module search paths

When resolving `import Acme\Geometry`, the compiler looks for
`Acme/Geometry.yonai` (falling back to `Acme/Geometry.yona` source for
pure-Yona modules) in this order:

1. Each `-I` path, in the order given on the command line.
2. Each directory in `YONA_PATH` (`:`-separated on Unix, `;`-separated on Windows).
3. The directory containing the input file.
4. The current working directory.
5. `lib/` and `share/yona/lib/` under each discovered **sysroot**.

A sysroot is a Yona distribution root. Sysroots are discovered from the
`--sysroot` flag, the `YONA_HOME` environment variable, and the directory
containing the `yonac` executable itself (which is how packaged installs find
their bundled stdlib without any configuration):

```bash
yonac -I ./vendor -I ./build/modules -o app main.yona   # explicit paths
YONA_HOME=/opt/yona yonac -o app main.yona              # explicit sysroot
```

See [the CLI reference](/reference/cli/) for the complete flag list.

## C FFI: `extern` declarations

Modules bind to C functions with `extern` declarations. Three forms:

```yona
extern sqrt : Float -> Float                       # bare: Yona name == C symbol
extern getEnv : String -> String = "getenv"        # aliased: rename the C symbol
extern async slowCompute : Int -> Int = "my_slow"  # thread-pool async → Promise
```

- **Bare form** — for C functions whose names already fit Yona (`sqrt`,
  `puts`). The identifier you write is the symbol the linker resolves.
- **Aliased form** — the string is the literal linker symbol, which may contain
  characters Yona identifiers cannot (`@`, versioned symbols, C++ mangling).
  This is also how the stdlib quarantines runtime mangling: one `extern
  raw_send : … = "yona_Std_Channel__send"` at the top of the file, clean names
  everywhere else.
- **Async form** — the call is dispatched to the thread pool and returns a
  `Promise`, transparently awaited at the use site. Combinable with aliasing.

Type mapping across the boundary:

| Yona type | C type |
|-----------|--------|
| `Int` | `int64_t` (LLVM `i64`) |
| `Float` | `double` |
| `Bool` | `bool` (LLVM `i1`) |
| `String` | `char *` (NUL-terminated) |
| `()` | `void` |

This is the pattern the standard library uses throughout — `Std\Channel`,
`Std\GPU`, and friends declare aliased externs at the top of the file and
export clean Yona wrappers around them:

```yona
module Std\Channel

extern raw_new  : Int -> Channel       = "yona_Std_Channel__channel"
extern raw_send : Channel -> Int -> () = "yona_Std_Channel__send"

channel n = let r = raw_new n in (Linear (Sender r), Linear (Receiver r))
```

Implementation note. `extern` never reads C headers and never invokes a C
compiler — it declares an LLVM extern reference with exactly the signature you
wrote. A mistyped C symbol surfaces as a linker error pointing at the bad
reference; a mistyped Yona-side name fails at parse time.

**Current limitation.** The stdlib's extern wrappers work at import time
because their C symbols live in the Yona runtime, which `yonac` always links.
A *user* module whose exports depend on its own externs cannot currently be
imported into a `yonac`-compiled program: the GENFN re-parse doesn't see the
extern declarations, and `yonac` does not yet add user module objects to its
link line. Extern-backed user modules are usable today as the program's own
module or when you drive the final link yourself (from C or a foreign build
system, linking the module `.o` plus its C dependencies).

## Trait instances across modules

Trait instance methods compile with **external linkage** and appear in the
`.yonai` as `INSTANCE`/`IMPL` entries alongside plain `FN` metadata. This means
an importing module — including GENFN bodies re-monomorphized in the importer —
dispatches to the defining module's instance methods through ordinary symbol
resolution. Defining `Show` for your ADT in one module makes `show` work on
that ADT everywhere the module is imported.

## Worked example: a two-module program

`Geometry.yona`:

```yona
module Geometry

export area, scale

area w h = w * h

scale factor xs = [factor * x for x = xs]
```

`main.yona`:

```yona
import area, scale from Geometry, foldl from Std\List in
let a = area 6 7,
    doubled = scale 2 [10, 20, 30]
in a + foldl (\acc x -> acc + x) 0 doubled
```

Compile and run:

```bash
yonac -o Geometry.o Geometry.yona   # emits Geometry.o + Geometry.yonai
yonac -I . -o demo main.yona        # resolves the import via Geometry.yonai
./demo
# => 162
```

The two `let` bindings are independent, so they evaluate concurrently — module
boundaries do not change Yona's transparent-async semantics.

## Limitations

- **No circular dependencies.** Modules compile in dependency order.
- **No package manager yet.** Paths and compilation order are managed by hand
  or by your build system.
- **Private-helper GENFN gap.** Exported functions that reference unexported
  module names fail at the import site (`E0104`); export the helpers they use.
- **Extern-backed user modules.** Exports that depend on the module's own
  `extern` declarations cannot yet be imported into a `yonac`-compiled
  program (see the FFI section above); link such modules from a foreign build
  system instead.
- **One module per file.** A source file contains either a module declaration
  or a program expression, not both.
