# Foundational Traits and Operator Semantics

## Goal

Make Yona's common generic operations explicit, lawful, and statically
resolved. Equality and ordering operators are syntax for trait operations;
they are not LLVM-layout operations. The same foundation supplies the small
set of traits required by reusable collection, algebra, conversion, and
concurrency APIs.

## Principles

1. A source-level operation has one semantic definition. Optimized primitive
   lowering is permitted only after the compiler has proved the corresponding
   trait obligation.
2. Trait selection is coherent: a concrete `(trait, type arguments)` key has
   at most one visible instance, and missing or ambiguous instances are
   diagnosed before LLVM IR generation.
3. Operators borrow their operands. Selecting a trait implementation must not
   change Perceus ownership or introduce pointer-identity semantics.
4. Parameterized instances retain their type arguments and constraints across
   `.yonai` boundaries. `Eq (Option a)` therefore requires `Eq a`; it is not an
   unconstrained instance for every `Option`.
5. Pure adapters and default methods live in Yona. C is reserved for primitive
   representation, syscalls, external libraries, and measured hot loops.

## Core Trait Set

The Prelude exports these language-foundation traits:

- `Eq a`: `eq : a -> a -> Bool`; `neq` defaults to `not (eq a b)`.
- `Ord a`, requiring `Eq a`: `compare : a -> a -> Ordering`; relational
  default methods are derived from `compare`.
- `Hash a`, requiring `Eq a`: `hash : a -> Int`. Equal values must hash alike.
- `Show a`: `show : a -> String`.
- `Sized a`: `size : a -> Int`.
- `Iterable c a`: `iterator : c -> Iterator a`.
- `Foldable c a`: `foldl : (b -> a -> b) -> b -> c -> b` and a `foldr`
  default expressed in Yona where the representation permits it.
- `Semigroup a`: `append : a -> a -> a`.
- `Monoid a`, requiring `Semigroup a`: `empty : Unit -> a`.
- `From a b`: total conversion, `from : a -> b`.
- `TryFrom a b`: checked conversion, `tryFrom : a -> Result b ConvertError`.
- `Parse a`: `parse : String -> Result a ParseError`.
- `Closeable a`: existing deterministic resource cleanup.
- `Send a` and `Shareable a`: compiler-recognized marker traits used at task,
  channel, and parallel-comprehension boundaries.

`Array` is the canonical indexed-access trait. Its overlap with `Sized` and
`Iterable` is documented and is independent of equality correctness.

`Functor`, `Applicative`, `Monad`, and similar higher-kinded abstractions are
not introduced until Yona can quantify over type constructors. Encoding them
as unrelated per-container traits would produce a misleading API.

## Operator Desugaring

- `a == b` requires and calls `Eq a.eq a b`.
- `a != b` requires `Eq a` and negates the same `eq` result.
- `<`, `>`, `<=`, and `>=` require `Ord a` and interpret `compare`'s
  `Ordering` result.

Primitive `Int`, `Float`, `Bool`, `String`, and `Symbol` operations may lower
directly to LLVM/runtime intrinsics, but only through the same resolved
instance record. This prevents a fast path from silently accepting a type
which has no lawful instance.

`Float` equality follows IEEE ordered equality: NaN differs from every value,
including itself; `-0.0` equals `0.0`. Its hash implementation must normalize
signed zero so the `Hash` law holds.

Functions, promises, linear resources, channels, and mutable/native handles do
not receive structural `Eq`, `Ord`, or `Hash` instances. Comparing them emits a
single source-located diagnostic explaining how to derive or implement the
missing instance, or how to use an explicit comparator such as
`Std\Test.equalBy`.

## Structural Instances

The compiler's derive engine remains the canonical structural implementation
for user ADTs. `deriving Eq`, `Ord`, `Hash`, or `Show` generates constrained
instances from field types and rejects function/resource fields for operations
that have no lawful semantics.

Prelude ADTs opt in explicitly:

- `Option a`: `Eq`, `Ord`, `Hash`, `Show` when the corresponding field
  constraint holds.
- `Result a e`: the same, with constraints on both parameters.
- `Ordering`, `FileMode`, `Whence`, `Type`, and other finite value enums:
  structural instances where meaningful.
- tuples and immutable standard collections: compiler/runtime-backed lifted
  instances requiring the element/key/value traits.

Equality checks constructor identity first and then calls `Eq` recursively for
declared fields. It never compares padding, heap addresses, closure pointers,
or incidental LLVM aggregate layouts.

## Trait Resolution and Interfaces

Trait declarations retain complete method signatures, superclasses, defaults,
instance type applications, and constraints. `.yonai` serialization must round
trip that information; registering every imported method as a one-argument
`a -> b` placeholder is not acceptable.

Resolution returns a typed instance selection rather than a mangled-name
string. It records the trait, concrete type arguments, required constraints,
method signature, implementation symbol, and whether an intrinsic lowering is
available. Type checking and code generation consume the same selection model.

Duplicate visible instances are an error. Instance lookup is deterministic and
must not depend on unordered-map iteration order.

## Marker Traits

`Send` means ownership may move to another task. `Shareable` means immutable
aliases may be observed concurrently. Primitive immutable values and fully
immutable structural values derive these markers transitively. Linear
resources, mutable/native buffers, and closures with nonconforming captures do
not acquire them automatically.

Task spawn, channel send, parallel comprehensions, and parallel `let` require
the appropriate marker. Initial enforcement is static and monomorphized; the
traits have no runtime dictionary or methods.

## Diagnostics

Missing-instance diagnostics identify:

- the operator or method being used;
- the inferred concrete type;
- the missing trait and nested field constraint, when applicable;
- a declaration-site example (`deriving Eq`, an explicit `instance`, or an
  explicit comparator/conversion);
- the source range of both mismatched operands when their types differ.

Each failed obligation is emitted once. Codegen does not continue into an
invalid aggregate comparison or leave a reachable callback body as
`unreachable`.

## Laws and Tests

The test suite checks behavior and laws, not only happy-path examples:

- `Eq`: reflexivity (where the type's documented semantics permit it),
  symmetry, transitivity, and `neq == not eq`;
- `Ord`: antisymmetry, transitivity, totality, and consistency with `Eq`;
- `Hash`: equal values have equal hashes;
- `Semigroup`: associativity;
- `Monoid`: left and right identity;
- conversion round trips where specified and precise error ADTs otherwise;
- `Sized`, `Iterable`, and `Foldable` consistency on empty, singleton, nested,
  Unicode, and large values;
- `Send`/`Shareable` positive and negative compile-time boundaries;
- cross-module and generic-instance `.yonai` round trips;
- ownership regressions under callbacks and heap-valued nested ADTs.

The `Std\Test` conformance framework exposes reusable law suites so standard
library modules and user instances can test the same contracts.
