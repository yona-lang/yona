---
title: Why Yona 2.0
description: >-
  Why Yona left GraalVM for LLVM, what the rewrite kept, what it added, and
  what it deliberately left behind.
---

Yona 2.0 is a ground-up reimplementation of the Yona language: a native,
ahead-of-time compiler built on LLVM, with a static type system. It replaces
the original GraalVM-hosted interpreter. This chapter explains the decision
honestly — what the first design got right, why its foundation stopped
fitting, and what changed.

## What Yona 1.x was

Yona began in 2018 as a dynamically typed, strict functional language for the
GraalVM: minimal ML-like syntax, few expression forms, and one founding idea —
**transparent concurrency**. Programs never mentioned promises or callbacks;
the runtime analyzed `let` expressions, batched independent bindings, and ran
them in parallel over non-blocking I/O. Persistent sequences, dictionaries,
and sets were built in, with full pattern-matching support.

Hosting on GraalVM/Truffle was a sound bet at the time. A small team got a
world-class JIT, garbage collection, and polyglot interoperability with Java
and JavaScript without writing a compiler backend from scratch. Yona 0.8.x
shipped, worked, and found its voice. The
[GraalVM implementation](https://github.com/yona-lang/yona) remains available
in that form, and its original documentation is preserved at
[yona-lang.github.io](https://yona-lang.github.io/).

## Why GraalVM stopped fitting

Five pressures accumulated, and each pointed away from the JVM.

**API instability.** Truffle and the Graal compiler interfaces moved fast and
broke often. For a large language team that churn is absorbable; for a small
language it converted every GraalVM upgrade into a rewrite tax, paid out of
the budget that should have gone to the language itself. Yona's interfaces to
its own users were stable; its foundation was not.

**The JVM as product surface.** Installing Yona 1.x meant installing GraalVM,
adding a component JAR with `gu`, and accepting JVM startup time and memory
floors. A language whose pitch is simplicity cannot require a virtual machine
distribution as a prerequisite. Yona 2.0 installs with
`dnf install yona`, `apt install yona`, `brew install akovari/tap/yona`, or a
Windows MSI — and compiles programs to self-contained native executables.

**The dynamic ceiling.** Yona 1.x was proudly dynamic, and honest about the
consequences: ADTs were conventions over tuples and symbols, there was no
exhaustiveness checking, and what other languages solve with type classes had
to be solved "by convention of sorts". That ceiling was fine for scripts and
increasingly wrong for the systems Yona wanted to serve — and it made
machine-generated code impossible to verify beyond "it parses".

**The performance model.** A tracing JIT accelerates hot interpreter loops;
it does not give you native binaries, predictable ahead-of-time performance,
deterministic memory behavior, or a path to lowering array pipelines onto a
GPU. LLVM gives all four.

**Polyglot cost versus value.** GraalVM's headline feature — calling Java and
JavaScript from Yona — was rarely the reason anyone chose the language. It was
paid for continuously and used occasionally. Yona 2.0 replaces it with a
plain C FFI (`extern` declarations), which is smaller, stable, and sufficient.

## What 2.0 keeps

The rewrite preserved everything that made Yona feel like Yona:

- **The syntax.** Juxtaposition application, few expression forms
  (`let`, `do`, `case`, `if`, `with`, `try`/`catch` + `raise`, `import`,
  `module`), significant newlines, no boilerplate.
- **Transparent concurrency.** Independent `let` bindings still parallelize
  automatically; `do` still sequences; `with` still scopes resources. The
  machinery underneath is now io_uring and a work-stealing thread pool
  instead of Truffle promises — the programming model is unchanged.
- **Persistent data structures.** Sequences, dictionaries, and sets with
  structural sharing, now implemented as radix-balanced tries and HAMTs in
  native code.
- **Pattern matching everywhere**, including head-tail decomposition,
  or-patterns, guards, and `as` bindings.

## What 2.0 adds

**A native pipeline.** Source → typed AST → LLVM IR → machine code. Common
benchmarks land within 1–2× of C; collection pipelines are stream-fused into
single loops. See [Performance](/guides/performance/) for methodology and
numbers.

**A static type system.** Hindley–Milner inference means programs are fully
typed with almost no annotations. On top of inference: algebraic data types
with exhaustive matching, traits (type classes) resolved by monomorphization,
record-row polymorphism, and **effect rows** — a function's arrow carries the
effects it may perform (`Int -> !{State.get} Int`), checked at call sites.
Linear types track resources such as file handles, sockets, and channel
endpoints, so leaking one is a compile error. Some of this is complete, some
is honestly partial; every feature page carries a status badge, and
[The type system](/guides/type-system/) states precisely what is checked
today.

**Memory management that matches the runtime.** Atomic reference counting
with Perceus-style ownership transfer, uniqueness-based in-place updates, and
escape analysis for arena allocation — no garbage collector, no pauses, no
JVM heap.

**Distribution.** Copr, PPA, AUR, Homebrew, Windows MSI. One binary compiler
(`yonac`), one REPL (`yona`), no VM.

**Accelerators.** `Std\GPU` executes columnar map/filter/reduce pipelines on
Vulkan compute queues, and the compiler can lower ordinary `IntArray` /
`FloatArray` pipelines to it transparently. This was structurally impossible
on the old stack. See [Accelerators](/guides/accelerators/).

## What was left behind

Honesty requires the other list. Yona 2.0 does **not** carry over:

- **GraalVM polyglot interop.** Calling Java or JavaScript is gone; the FFI
  is C (`extern` declarations).
- **Software transactional memory.** STM was a 1.x flagship module. It is on
  the 2.0 backlog, not in the language today.
- **First-class module values.** In 1.x, modules were runtime values you
  could create dynamically. In 2.0, modules are compile-time units with
  `.yonai` interface files that enable separate compilation and cross-module
  generics. This is a real semantic break, traded for static checking and
  native linking.

## Who Yona 2.0 is for

Yona 2.0 is for people who want a small functional language that compiles,
types, and runs like systems software: no async/await ceremony, no VM, no
garbage collector — and for a world in which much code is written by
machines, a compiler strict enough to keep that code honest.

Continue with the [quick start](/learn/quick-start/), or read how the
[concurrency model](/learn/concurrency/) works.
