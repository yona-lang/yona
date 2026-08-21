---
title: Agent guide
description: How AI coding agents should consume these docs and work with the Yona toolchain.
---

Yona is designed to be a good target for machine-written code: the grammar
is small, the type system is strict, and the compiler's diagnostics are
stable and self-explaining. This page is the entry point for coding agents
and the humans configuring them.

## Machine-readable documentation

- **<a href="/llms.txt" data-astro-reload>/llms.txt</a>** — an index of
  every documentation page with one-line descriptions. Fetch this first.
- **<a href="/llms-full.txt" data-astro-reload>/llms-full.txt</a>** — the
  entire documentation corpus concatenated as plain markdown.
- **<a href="/llms-small.txt" data-astro-reload>/llms-small.txt</a>** — an
  abridged corpus for small context windows.

## The feedback loop

Yona's compiler is built for error-driven repair:

1. **Compile:** `yonac program.yona`. **Run a one-liner:** `yona -e '<expr>'`.
2. **Read the diagnostic code.** Errors carry stable codes (for example
   `E0202` — unhandled effect at a call site).
3. **Ask the compiler to explain:** `yonac --explain E0202` prints the full
   explanation with examples. No web search required.
4. **Fix and recompile.** Warnings become errors under `--Werror` for
   stricter loops.

Useful introspection flags:

| Flag | Output |
|------|--------|
| `--emit-ir` | LLVM IR instead of an executable |
| `--emit-obj` | object file only |
| `--emit-typed-core` | resolved names, types, effects, linearity, and spans (no LLVM) |
| `--emit-accelerator-report` | JSON report of GPU-lowered and explicit accelerator sites |
| `--explain E0xxx` | full explanation of a diagnostic |
| `-I path` | additional `.yonai` interface search paths |

## Contracts an agent can rely on

- **Everything is an expression.** A program is one expression; there are no
  statements. Generation can proceed compositionally.
- **Types are inferred.** Do not emit annotations unless a signature is the
  point; the checker infers principal types.
- **Effects are visible.** A function's arrow carries the effects it may
  perform (`Int -> !{State.get} Int`). If generated code performs an effect
  with no covering `handle`, compilation fails with `E0202` — treat that as
  a contract violation, not a runtime surprise.
- **Resources are linear.** Values such as file handles and channel
  endpoints must be consumed exactly once; prefer `with` blocks. Dropping or
  duplicating one is a compile error, not a leak.
- **Exhaustiveness is checked.** `case` over an ADT should cover every
  constructor; the compiler warns otherwise.

## Style rules for generated code

Follow the [style guide](/learn/style/); the high-signal rules:

- Never nest `let`; use one multi-binding `let x = 1, y = 2 in …`.
- `let` binds values (independent RHSs may run in parallel); `do`
  sequences effects top to bottom. Combining them is valid when you need
  both — `let a = readFile x, b = readFile y in do … end`. Do not use
  `let _ = effect` to sequence, wrap a single expression in `do`, or pad
  a body with a dummy trailing `0`.
- Use comma-separated imports: `import a from X, b from Y in …`.
- Use `with` for resources, not manual open/close.
- Comments are `#` (line) and `/* */` (block) — **not** `--`.
- Prefer prelude combinators (`identity`, `const`, `flip`, `compose`) and
  `Std\…` modules over reimplementation. `foldl`, `map`, and `filter` are
  **not** prelude — `import foldl from Std\List` first.

## Syntax highlighting and grammars

The TextMate grammar used by this site is
[`site/grammars/yona.tmLanguage.json`](https://github.com/yona-lang/yona/blob/master/site/grammars/yona.tmLanguage.json).
The VS Code extension copies the same file into `editors/vscode/syntaxes/`.
See [Editor and language server](/guides/editor/). A future Yona `yls`
can use `Std\IO.readExact`, `Std\Json`, and `Std\Utf16`; the shipped
server is still the C++ `yls --stdio` binary.
