# Std.Set

Set — persistent set backed by a Hash Array Mapped Trie (HAMT).

Provides immutable sets with O(log32 n) insert, membership test, and
standard set operations (union, intersection, difference). Iterators
use stack-based trie traversal with O(1) memory per element.

## Functions

### `insert : Set a -> a -> Set a`

Add an element to the set. Returns a new set containing `elem`.
The original set is unchanged. Inserting a duplicate is a no-op.

```
let s = insert (insert #{} 1) 2 in
size s   # => 2
```

### `contains : Set a -> a -> Bool`

Check whether `elem` is a member of the set.

```
let s = insert #{} 42 in
contains s 42   # => true
contains s 99   # => false
```

### `size : Set a -> Int`

Returns the number of elements in the set.

```
let s = insert (insert #{} 1) 2 in
size s   # => 2
```

### `elements : Set a -> [a]`

Eagerly collects all elements into a sequence.

```
let s = insert (insert #{} 3) 1 in
elements s   # => [3, 1]  (order may vary)
```

### `union : Set a -> Set a -> Set a`

Returns a new set containing all elements from both `a` and `b`.

```
let a = insert (insert #{} 1) 2 in
let b = insert (insert #{} 2) 3 in
elements (union a b)   # => [1, 2, 3]  (order may vary)
```

### `intersection : Set a -> Set a -> Set a`

Returns a new set containing only elements present in both `a` and `b`.

```
let a = insert (insert #{} 1) 2 in
let b = insert (insert #{} 2) 3 in
elements (intersection a b)   # => [2]
```

### `difference : Set a -> Set a -> Set a`

Returns a new set containing elements in `a` that are not in `b`.

```
let a = insert (insert (insert #{} 1) 2) 3 in
let b = insert #{} 2 in
elements (difference a b)   # => [1, 3]  (order may vary)
```

### `iterator : Set a -> Iterator a`

Returns a streaming `Iterator Int` over set elements.
Uses stack-based trie traversal — O(1) memory per element.

```
import iterator from Std\Set in
let s = insert (insert #{} 1) 2 in
let iter = iterator s in
# consume with iterator protocol
```

### `forEach : (a -> b) -> Set a -> ()`

Apply `callback` to each element for side effects.

```
let s = insert (insert #{} 1) 2 in
forEach (\x -> println (show x)) s
```
