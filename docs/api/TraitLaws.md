# Std.TraitLaws

Reusable executable laws for foundational trait instances.

Every suite accepts an explicit finite sample set plus the operations under
test. This keeps the API deterministic and useful for opaque types while
still checking every relevant sample pair or triple. Failures include the
first rendered counterexample instead of only naming the violated law.

## Functions

### `eqLaws : String -> (a -> String) -> (a -> a -> Bool) -> Seq a -> Seq TestCase`

Equality laws over every supplied sample, pair, and equality chain.

### `ordLaws : String -> (a -> String) -> (a -> a -> Bool) -> (a -> a -> Ordering) -> Seq a -> Seq TestCase`

Total-order laws, including agreement with equality and reversal symmetry.

### `hashLaws : String -> (a -> String) -> (a -> a -> Bool) -> (a -> Int) -> Seq a -> Seq TestCase`

Equal values must always produce equal hashes.

### `showLaws : String -> (a -> String) -> Seq a -> Seq TestCase`

Rendering the same value is deterministic for every sample.

### `semigroupLaws : String -> (a -> String) -> (a -> a -> Bool) -> (a -> a -> a) -> Seq a -> Seq TestCase`

Associativity over every triple in the sample set.

### `monoidLaws : String -> (a -> String) -> (a -> a -> Bool) -> (a -> a -> a) -> a -> Seq a -> Seq TestCase`

Left and right identity over every sample.
