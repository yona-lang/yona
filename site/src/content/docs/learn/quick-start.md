---
title: Quick start
description: Your first Yona programs — expressions, files, executables, and the REPL.
---

This page takes you from nothing to a compiled, running Yona program.
[Install Yona](/install/) first; you need `yonac` (the compiler) and `yona`
(the REPL) on `PATH`.

## Evaluate an expression

`yonac -e` compiles and runs a single expression:

```bash
yonac -e 'let fib n = if n <= 1 then n else fib (n-1) + fib (n-2) in fib 10'
# => 55
```

Everything in Yona is an expression — a program is *one* expression, and its
value is the program's result. `let` introduces bindings; `fib` here is a
recursive function bound with function-definition syntax.

## Your first file

Create `hello.yona`:

```yona
# hello.yona — a program is a single expression.
import println from Std\IO in
do
    println "Hello from Yona"
    0
end
```

What this does, precisely:

- `import println from Std\IO in …` brings one function from the standard
  library's `Std\IO` module into scope for the expression that follows.
- `do … end` evaluates its expressions **in order** — this matters for side
  effects like printing. Its last expression (`0`) is the block's value, which
  becomes the program's exit code.

Compile and run:

```bash
yonac hello.yona -o hello    # default output name is a.out (a.exe on Windows)
./hello
# Hello from Yona
```

`yonac` compiles ahead of time: the output is a self-contained native
executable, not a script. Without `-o`, expression programs compile to
`a.out` (`a.exe` on Windows); use `--emit-ir` if you want to inspect the
generated LLVM IR instead.

## Something real: parallel I/O without async

Save as `sizes.yona`:

```yona
# Reads two files concurrently, then reports their combined length.
import readFile from Std\File,
       println from Std\IO,
       length from Std\String in
let
    a = readFile "hello.yona",   # both reads are submitted
    b = readFile "sizes.yona"    # before either result is needed
in do
    println "combined bytes: {(length a + length b)}"
    0
end
```

Three things to notice:

1. **No async/await.** `readFile` performs non-blocking I/O. Because the two
   bindings do not depend on each other, the compiler runs them in parallel;
   the values are awaited automatically at first use (`length a`). This is
   Yona's *transparent async* — the founding idea of the language. Details in
   [Concurrency](/learn/concurrency/).
2. **Multi-binding `let`.** One `let` introduces many bindings, separated by
   commas. Nesting `let` inside `let` is legal but unidiomatic — see
   [Style](/learn/style/).
3. **String interpolation.** `"{name}"` interpolates a variable;
   `"{(expr)}"` interpolates an expression (the parentheses are required for
   anything containing operators).

## Pattern matching in ten lines

```yona
type Shape = Circle Float | Rect Float Float

area shape = case shape of
    Circle r -> 3.141592653589793 * r * r
    Rect w h -> w * h
end

area (Rect 3.0 4.0)   # => 12.0
```

`type` declares an **algebraic data type** with two constructors. `case`
matches on the constructors and binds their fields. The compiler knows every
constructor of `Shape`, so it can warn when a `case` does not cover all of
them. More in [Pattern matching](/learn/pattern-matching/) and
[Types and data](/learn/types/).

## The REPL

```bash
yona
```

`yona` compiles and runs each entered expression natively — it is the same
pipeline as `yonac`, not an interpreter. Useful for exploring the standard
library:

```yona
import map from Std\List in map (\x -> x * x) [1, 2, 3]
# => [1, 4, 9]
```

## Prelude: what needs no import

These are available in every program without any `import`: the types
`Option a` (`Some`/`None`), `Result a e` (`Ok`/`Err`), `Linear a`,
`Iterator a`, and the functions `identity`, `const`, `flip`, `compose`.
Everything else — including `foldl`, `map`, and `filter` from
[Std\List](/stdlib/list/) — lives in `Std\…` modules; see the
[standard library](/stdlib/) and the [prelude reference](/reference/prelude/).

## Where to go next

- [Syntax and evaluation](/learn/syntax/) — the exact rules for newlines,
  literals, and expressions.
- [Concurrency](/learn/concurrency/) — `let` vs `do` vs `with`, and what the
  runtime actually does.
- [Why Yona 2.0](/why-yona-2/) — if you knew the GraalVM-era language.
