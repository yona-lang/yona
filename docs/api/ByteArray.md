# Std.ByteArray

Contiguous unboxed byte array. Provides allocation, indexing, slicing,
bulk operations (foldl, map), and conversion between byte arrays,
strings, and sequences. Used for binary I/O, network protocols,
and interop with C libraries. Implements the `Array` trait.

## Functions

### alloc

`alloc size`

Allocate a zero-filled byte buffer of `size` bytes.

```yona
import alloc from Std\ByteArray in
let buf = alloc 1024 in
length buf   # => 1024
```

### length

`length bytes`

Returns the number of bytes in the buffer.

```yona
import length from Std\ByteArray in
length (fromString "hello")   # => 5
```

### get

`get bytes index`

Returns the byte value (0-255) at the given index.

```yona
import get, fromString from Std\ByteArray in
let buf = fromString "ABC" in
get buf 0   # => 65
```

### set

`set bytes index value`

Sets the byte at `index` to `value` (0-255). Mutates the buffer in place.

```yona
import alloc, set, get from Std\ByteArray in
let buf = alloc 4 in
do
  set buf 0 42
  get buf 0   # => 42
end
```

### concat

`concat a b`

Concatenate two byte buffers into a new buffer.

```yona
import concat, fromString from Std\ByteArray in
let buf = concat (fromString "hello ") (fromString "world") in
toString buf   # => "hello world"
```

### slice

`slice bytes start end`

Extract a sub-buffer from index `start` (inclusive) to `end` (exclusive).

```yona
import slice, fromString, toString from Std\ByteArray in
toString (slice (fromString "hello") 1 4)   # => "ell"
```

### fromString

`fromString str`

Convert a UTF-8 string to a byte buffer.

```yona
import fromString from Std\ByteArray in
let buf = fromString "hi" in
length buf   # => 2
```

### toString

`toString bytes`

Convert a byte buffer back to a UTF-8 string.

```yona
import fromString, toString from Std\ByteArray in
toString (fromString "hello")   # => "hello"
```

### fromSeq

`fromSeq seq`

Create a byte buffer from a sequence of integers (0-255).

```yona
import fromSeq, get from Std\ByteArray in
let buf = fromSeq [72, 105] in
get buf 0   # => 72
```

### toSeq

`toSeq bytes`

Convert a byte buffer to a sequence of integers.

```yona
import fromString, toSeq from Std\ByteArray in
toSeq (fromString "Hi")   # => [72, 105]
```

### head

`head : ByteArray -> Int`

First byte value. O(1).

### tail

`tail : ByteArray -> ByteArray`

All bytes except the first. Returns a new array.

### join

`join : ByteArray -> ByteArray -> ByteArray`

Concatenate two byte arrays (alias for `concat`).

### foldl

`foldl : (Int -> Int -> Int) -> Int -> ByteArray -> Int`

Left fold over all bytes. Single-pass, cache-friendly.

```yona
import fromString, foldl from Std\ByteArray in
foldl (\acc b -> acc + b) 0 (fromString "ABC")   -- 65+66+67 = 198
```

### map

`map : (Int -> Int) -> ByteArray -> ByteArray`

Apply a function to each byte, returning a new array.
