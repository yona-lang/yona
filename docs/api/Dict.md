# Std.Dict

Dict — persistent dictionary backed by a Hash Array Mapped Trie (HAMT).

Provides immutable key-value mappings with O(log32 n) lookup, insert,
and update. Iterators use stack-based trie traversal with O(1) memory
per element.

## Functions

### put

`put dict key value`

Insert or update a key-value pair. Returns a new dictionary with the
mapping added. The original dictionary is unchanged.

```
let d = put {} 1 100 in
let d2 = put d 2 200 in
get d2 1 0   # => 100
```

### get

`get dict key default`

Look up the value for `key`. Returns `default` if the key is not present.

```
let d = put {} 42 999 in
get d 42 0    # => 999
get d 99 0    # => 0
```

### contains

`contains dict key`

Check whether `key` exists in the dictionary. Returns `true` or `false`.

```
let d = put {} 1 10 in
contains d 1   # => true
contains d 2   # => false
```

### size

`size dict`

Returns the number of entries in the dictionary.

```
let d = put (put {} 1 10) 2 20 in
size d   # => 2
```

### keys

`keys dict`

Eagerly collects all keys into a sequence.

```
let d = put (put {} 1 10) 2 20 in
keys d   # => [1, 2]  (order may vary)
```

### entries

`entries dict`

Returns a streaming `Iterator (Int, Int)` over `(key, value)` tuples.
Uses stack-based trie traversal — O(1) memory per element.

```
let d = put (put {} 1 10) 2 20 in
forEach (\k v -> println (show k ++ " => " ++ show v)) d
```

### keysIter

`keysIter dict`

Returns a streaming `Iterator Int` over keys. O(1) memory per element.

```
import keysIter from Std\Dict in
let d = put (put {} 1 10) 2 20 in
let iter = keysIter d in
# consume with iterator protocol
```

### values

`values dict`

Returns a streaming `Iterator Int` over values. O(1) memory per element.

```
import values from Std\Dict in
let d = put (put {} 1 10) 2 20 in
let iter = values d in
# consume with iterator protocol
```

### forEach

`forEach callback dict`

Apply `callback` to each `(key, value)` entry for side effects.
The callback receives two arguments: the key and the value.

```
let d = put (put {} 1 10) 2 20 in
forEach (\k v -> println (show k ++ ": " ++ show v)) d
```
