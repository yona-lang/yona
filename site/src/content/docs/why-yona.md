---
title: Why Yona
description:
  A compact native functional language with explicit effects, linear resources,
  and shared semantics.
---

Yona is for programmers who want a functional language that stays close to the
native toolchain. It has a deliberately compact source language, strong static
semantics, and no VM or tracing collector in the deployment model.

## One pipeline, shared semantics

The compiler, language server, interface reader, and C adapter use one semantic
model. Names, bindings, types, effects, ownership, and diagnostics come from the
same analysis rather than separate editor and compiler implementations.

| Stage                | Responsibility                                                                             |
| -------------------- | ------------------------------------------------------------------------------------------ |
| Syntax               | Newline-aware lexing, parsing, ASTs, and patterns                                          |
| Semantics            | Bindings, inference, traits, effects, coverage, termination, and ownership                 |
| Typed IR             | Ownership-explicit lowering boundary for the backend                                       |
| LLVM code generation | Native IR and object generation without source parsing or process execution                |
| Toolchain            | Module discovery, interface files, linker planning, and argument-vector process invocation |

That separation is practical as well as architectural: an editor should report
the same binding identity and type that production compilation uses.

## Types without annotation noise

Hindley-Milner inference gives ordinary expressions precise types without
writing annotations everywhere. Algebraic data types, traits, row-like records,
refinements, and exhaustiveness checking extend the foundation where a program
needs more structure.

```yona
type Result a e = Ok a | Err e

mapResult f result =
  case result of
    Ok value -> Ok (f value)
    Err error -> Err error
  end
```

Function types also record effects. A value that performs I/O, raises, or uses a
handler boundary is different from a pure value, and the checker keeps that
distinction available to the programmer.

## Ownership is explicit at the right boundary

The runtime uses atomic reference counting and ownership transfer. Linear
resources, channel endpoints, tasks, and C handles use explicit retain/release
contracts across their boundaries. Persistent collections retain structural
sharing; values proven unique can use in-place updates.

The goal is not manual memory management in application code. It is a runtime
whose lifetime rules can be reasoned about with source-level constructs such as
`with`, rather than a finalizer eventually running at an unknown point.

## Direct native integration

Yona lowers to LLVM and can produce object files for separate compilation.
Deterministic `.yonai` interface files carry the type information needed across
modules. C interoperability is explicit through `extern` declarations and a
single canonical C adapter.

The installed CMake package treats `yonac` as a build tool: it supplies the
compiler executable, runtime archive, standard-library sysroot, and helpers for
Yona executables and modules. See [CMake integration](/reference/cmake/).

Continue with the [quick start](/learn/quick-start/), explore the
[type-system guide](/guides/type-system/), or read the
[language specification](/reference/specification/).
