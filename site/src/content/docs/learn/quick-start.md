---
title: Quick start
description: Your first Yona programs — expressions, files, executables, scripts, and the REPL.
---

This page takes you from nothing to a compiled, running Yona program.
[Install Yona](/install/) first; you need `yonac` (the compiler) and `yona`
(the runner / REPL) on `PATH`.

## Evaluate an expression

`yona -e` compiles and runs a single expression:

```bash
yona -e 'let fib n = if n <= 1 then n else fib (n-1) + fib (n-2) in fib 10'
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
println "Hello from Yona"
```

What this does, precisely:

- `import println from Std\IO in …` brings one function from the standard
  library's `Std\IO` module into scope for the expression that follows.
- That expression *is* the program. `println` writes the line; there is no
  dummy return value and no `do` wrapper around a single call.

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
in println "combined bytes: {(length a + length b)}"
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
let maybeValue = Some 42 in
case maybeValue of
    Some x -> x * 2
    None -> 0
end
# => 84
```

`case` matches on the constructors of a value and binds their fields.
`Some` and `None` are the constructors of `Option`, available in every
program without an import. The compiler knows every constructor of a type,
so it can warn when a `case` does not cover all of them. Your own algebraic
data types are declared with `type` inside a module — see
[Modules](/learn/modules/). More in [Pattern matching](/learn/pattern-matching/)
and [Types and data](/learn/types/).

## Scripts and the REPL

A file can be a script. After `chmod +x`, the kernel finds `yona` on `PATH`:

```yona
#!/usr/bin/env yona
import println from Std\IO in
println "Hello from a script"
```

`yona hello.yona` is the same path. Each invocation compiles; for a tool you
run often, `yonac -o hello hello.yona`. Piped stdin is also a program:
`echo '1 + 2' | yona`.

With no file and a TTY, `yona` starts the REPL:

```bash
yona
```

The REPL compiles and runs each entered expression natively. Useful for
exploring the standard library:

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
