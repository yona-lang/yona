# Prelude Module

## Overview

The Prelude is a Yona module (`lib/Prelude.yona`) that is automatically loaded for all programs. Its types and functions are available without explicit imports.

## Types

```yona
type Linear a = Linear a          -- Resource wrapper (linearity checked)
type Option a = Some a | None     -- Optional value
type Result a e = Ok a | Err e    -- Error handling
type Iterator a = Iterator (() -> Option a)  -- Streaming iterator
```

These constructors are always in scope:
- `Some`, `None` — wrap/unwrap optional values
- `Ok`, `Err` — success/failure
- `Linear` — resource lifecycle tracking
- `Iterator` — streaming data source

## Functions

```yona
identity x = x             -- returns its argument
const x _ = x               -- ignores second argument
flip f a b = f b a           -- swaps argument order
compose f g x = f (g x)     -- function composition
```

Collection folds are intentionally not Prelude functions. Use the polymorphic
`foldLeft` / `foldRight` trait methods, or import the specialized helpers from
`Std\List` and `Std\Iterator`.

## Foundational traits

The Prelude is also the single source of truth for Yona's foundational static
contracts:

```yona
trait Eq a
trait Eq a => Ord a
trait Eq a => Hash a
trait Show a
trait Array array element
trait Closeable a
trait Sized a
trait Iterable collection element
trait Foldable collection element
trait Semigroup a
trait Semigroup a => Monoid a
trait From target source
trait TryFrom target source
trait Parse target
trait Send a
trait Send a => Shareable a
```

`Eq`, `Ord`, `Hash`, and `Show` have primitive and lawful lifted immutable
instances. `Array`, `Sized`, `Iterable`, and `Foldable` cover the finite
sequence/string/native-array and appropriate Option/Result/Set/Dict/Iterator
families. `Semigroup` and `Monoid` cover String, Seq, Set, and right-biased
Dict combination. Conversion implementations live in `Std\Convert`.

`Send` and `Shareable` are method-free marker traits checked and erased at
concurrency boundaries. They lift through safe immutable aggregates. Native
arrays implement `Send` (unique ownership may move) but not `Shareable`;
thread-safe `Sender`/`Receiver` channel endpoints implement both. Linear
resources and promises remain outside both contracts.

Equality and relational operators select these contracts statically:
`==`/`!=` use `Eq`; `<`/`<=`/`>`/`>=` use `Ord.compare`, whose result is the
`Ordering` ADT (`Less | Equal | Greater`). See the trait guide and
`Std\TraitLaws` for the laws and reusable executable checks.

## How It Works

### Unified Loading

`load_prelude(parser, type_checker)` is the single entry point. It reads
`Prelude.yonai` and automatically populates all subsystems:

1. **Codegen**: `register_all_imports("Prelude")` — functions available by local name
2. **Parser**: `register_constructor()` for each ADT — enables pattern matching
3. **Type checker**: `register_adt()` + `register_trait_method()` — type inference
4. **Linker**: `Prelude.o` linked with executables

No manual registration in `cli/main.cpp`, `Parser.cpp`, or anywhere else.

### Coexistence with Std Modules

`Std\Option` and `Std\Result` provide utility functions (`map`, `flatMap`, `unwrapOr`). The Prelude provides the types; Std modules provide the functions.

## Updating the Prelude

**For Yona functions/types:**
1. Edit `lib/Prelude.yona`
2. Recompile: `yonac lib/Prelude.yona && mv Prelude.yonai lib/`
3. Rebuild compiler. Done.

**For C-backed functions:**
1. Add implementation in `src/compiled_runtime.c`
2. Declare the `extern` and its exact type in `lib/Prelude.yona`
3. Regenerate `lib/Prelude.yonai` and `lib/Prelude.o`
4. Rebuild. Done.

No other files need changes — `load_prelude()` reads .yonai automatically.
