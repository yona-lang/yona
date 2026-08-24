---
title: Modules
description: Declaring modules, exporting functions and types, importing as an expression, and fully qualified calls.
---

A Yona module is a named collection of functions and types that compiles to
a native object file. Modules are **top-level declarations**, not
expressions — they cannot be passed around, returned, or stored in data
structures. One file, one module.

## Declaring a module

A module file starts with `module Pkg\Name` and **ends at end-of-file** —
there is no closing `end` keyword. The name is a fully qualified name (FQN)
with backslash-separated package segments:

```yona
module Data\Geometry

export area, perimeter
export type Shape

type Shape = Circle Float | Rect Float Float

area shape = case shape of
    Circle r -> 3.14159265 * r * r
    Rect w h -> w * h
end

perimeter shape = case shape of
    Circle r -> 2.0 * 3.14159265 * r
    Rect w h -> 2.0 * (w + h)
end

# Private helper — not exported, invisible to importers
square x = x * x
```

Everything after the exports is ordinary Yona: type declarations and
function definitions. Anything not listed in an `export` statement is
private to the module.

## Exports

`export` is a standalone statement and may appear any number of times, each
handling one group of names:

```yona
export area, perimeter        # functions
export type Shape             # a type AND all of its constructors
export type Token opaque      # a type without exposing constructors
export scale from Data\Xform  # re-export from another module
```

The three forms, precisely:

- **Functions:** `export f, g` makes `f` and `g` callable by importers.
- **Types:** `export type Shape` exports the type together with all its
  constructors (`Circle`, `Rect`), so importers can construct values and
  pattern-match on them.
- **Opaque types:** `export type Token opaque` exports only the nominal type.
  Importers can pass it to public functions but cannot name or match its
  constructors; expose smart constructors and observers instead.
- **Re-exports:** `export f, g from Other\Mod` republishes names defined in
  another module as if they were defined here. Importers depend only on the
  re-exporting module. The re-exporting module may also use those names in
  its own definitions:

```yona
module Std\Convenience

export add, mul from Std\Arith
export double

double x = add x x    # re-exported names are usable locally
```

## Imports are expressions

`import … in body` brings names into scope **for the body expression only**
— it is scoped like `let`, not a file-level statement. This means imports
can appear anywhere an expression can, and their scope is exactly as large
as you make it.

### Selective import

```yona
import area from Data\Geometry in
area (Circle 1.0)
# => 3.14159265
```

### Aliased import

Rename on import with `as` — useful for avoiding clashes or shortening
names:

```yona
import area as shapeArea from Data\Geometry in
shapeArea (Rect 2.0 3.0)
# => 6.0
```

### Whole-module import

Importing just the module name brings **all** of its exports into scope:

```yona
import Data\Geometry in
area (Circle 1.0) + perimeter (Rect 2.0 3.0)
```

Prefer selective imports in anything but throwaway code; they document
where each name comes from.

### Multi-module imports

One `import` can pull from several modules, comma-separated. This is the
idiomatic form — do not nest `import` expressions:

```yona
import
  area from Data\Geometry,
  println from Std\IO
in
do
    println "computing"
    area (Circle 1.0)
end
```

## Fully qualified calls

`Pkg\Mod::func` calls an exported function directly, **with no import at
all**. The compiler auto-loads the module's interface:

```yona
Std\List::map (\x -> x + 1) [1, 2, 3]
# => [2, 3, 4]
```

FQN calls suit one-off uses; switch to an import when a module is used more
than a couple of times in the same expression.

## How `yonac` compiles a module

Compiling a module file produces two artifacts:

```bash
yonac -o Geometry.o Data/Geometry.yona
# produces Geometry.o (native object) + Data/Geometry.yonai (interface)
```

- **The object file (`.o`)** contains native code with C-ABI exports.
  Names are mangled predictably — `Data\Geometry::area` becomes
  `yona_Data_Geometry__area` — so Yona modules link with C (and anything
  with a C FFI) through the ordinary system linker.
- **The interface file (`.yonai`)** is a text file describing the exported
  functions' signatures, ADT definitions, traits, and inferred effect rows.
  It is what makes cross-module calls *type-checked*: when you import a
  module, the compiler reads its `.yonai`, not its source. Exported
  functions also embed their source text in the interface, so generic
  functions can be re-specialized at call sites whose types differ from the
  pre-compiled signature (cross-module monomorphization).

When resolving `import Data\Geometry`, the compiler looks for
`Data/Geometry.yonai` in the `-I` search paths, then next to the input
file, then in the current directory:

```bash
yonac -I ./lib -I "$YONA_HOME/lib" -o program main.yona
```

Modules must be compiled in dependency order — circular module dependencies
are not supported.

For the interface file format, borrow inference metadata, and the details of
cross-module generics, see
[Modules and interfaces](/guides/modules-interfaces/).

## A complete two-file example

`Data/Counter.yona`:

```yona
module Data\Counter

export type Counter
export make, bump, value

type Counter = Counter Int

make = Counter 0
bump c = case c of Counter n -> Counter (n + 1) end
value c = case c of Counter n -> n end
```

`main.yona`:

```yona
import make, bump, value from Data\Counter in
value (bump (bump make))
# => 2
```

Build and run:

```bash
yonac -o Counter.o Data/Counter.yona
yonac -I . -o main main.yona
./main   # exit code 2
```

The exported `Counter` constructor is available to `main.yona` because
`export type Counter` exports the type with its constructors; `bump`'s
pattern match on `Counter n` in an importing module would work the same
way.
