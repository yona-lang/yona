# Std.Iterator

Stateful pull-iterator adapters and consumers.

Iterators are single-pass values: every call to `next` advances the shared
cursor. Use `toSeq` when a persistent, reusable collection is required.

## Functions

### `fromSeq : Seq element -> Iterator element`

### `fromByteArray : ByteArray -> Iterator Int`

### `fromIntArray : IntArray -> Iterator Int`

### `fromFloatArray : FloatArray -> Iterator Float`

### `next : Iterator element -> Option element`

Advance an iterator once.

### `foldLeft : Iterator element -> (acc -> element -> acc) -> acc -> acc`

Consume an iterator from left to right.

### `toSeq : Iterator element -> Seq element`

Consume an iterator into a persistent sequence.
