# Std.Parallel

## Functions

### pmap

`pmap f xs = [| f x for x = xs ]`

Parallel map — applies f to each element concurrently.
All invocations of f run in parallel. If any fails, the
rest are cancelled and the error is propagated.

Example:
import pmap from Std\Parallel in
pmap (\x -> x * 2) [1, 2, 3]   -- [2, 4, 6]

### pfor

`pfor f xs`

Parallel for-each — applies f to each element concurrently
for side effects. Returns the number of elements processed.
