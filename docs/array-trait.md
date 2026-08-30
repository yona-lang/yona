# Array Trait

Yona provides a unified `Array` trait in the Prelude for indexed access to
contiguous and indexed collections. Combined with the polymorphic `foldl`
function, this gives uniform access patterns across all array-like types.

## The Array Trait

```yona
trait Array a
    length : a -> Int
    get : a -> Int -> Int
end
```

The trait is in the Prelude — no imports needed. Methods dispatch at compile
time based on the receiver's type.

## Instances

The Array trait has built-in instances for:

| Type | length | get | Notes |
|------|--------|-----|-------|
| ByteArray | yes | yes (returns Int 0-255) | Contiguous `uint8_t[]` |
| IntArray | yes | yes | Contiguous `int64_t[]` |
| FloatArray | yes | yes (returns Float) | Contiguous `double[]` |
| Seq | yes | yes (O(log n) for >32 elements) | Persistent RBT |
| String | yes | yes (returns char code) | UTF-8 byte access |

## Usage

```yona
import fromSeq from Std\IntArray in
let arr = fromSeq [10, 20, 30, 40, 50] in

length arr       -- 5
get arr 2        -- 30
```

No `import length from ...` needed — `length` is a Prelude trait method.
The compiler resolves it to the right implementation based on the argument
type via trait dispatch.

## Polymorphic foldl

The Prelude's `foldl` function works on any iterable collection via runtime
type detection. It handles:

- `Seq` (boxed sequence)
- `Iterator` (lazy stream)
- `ByteArray`, `IntArray`, `FloatArray` (unboxed arrays)
- `String` (per-byte iteration)

```yona
import fromSeq from Std\IntArray in
foldl (\acc x -> acc + x) 0 (fromSeq [1, 2, 3, 4, 5])   -- 15

import keysIter from Std\Dict in
let d = {1: 10, 2: 20, 3: 30} in
foldl (\acc k -> acc + k) 0 (keysIter d)                 -- 6 (Iterator)

foldl (\acc b -> acc + b) 0 "ABC"                        -- 198 (65+66+67)
```

`foldl` is not a trait method because trait dispatch in Yona uses the first
argument's type — for `foldl f acc collection`, the first arg is the function,
not the collection. Runtime type detection via the RC type tag is used instead.

## Why Array, not Foldable?

For `length` and `get`, the receiver IS the first argument, so trait dispatch
works. For `foldl`, the collection is the third argument, which doesn't fit
trait dispatch's "first arg determines instance" model.

This design keeps:
- Compile-time safety for indexed access (`length`, `get` are trait-checked)
- Runtime polymorphism for fold (no need to know the type at the call site)

## Adding New Array Types

To add a new type as an `Array` instance:

1. Add the trait instance to `lib/Prelude.yonai`:
   ```
   INSTANCE Array MyType
     IMPL length YonaPreludeArrayMyTypeLength
     IMPL get YonaPreludeArrayMyTypeGet
   ```
2. Implement the wrapper functions in the owning runtime component, using the
   canonical typed value descriptors for managed arguments.
3. To support polymorphic `foldl`, add a case to `YonaStdListFoldl`
   that detects your type's RC tag and dispatches to the specialized fold.
