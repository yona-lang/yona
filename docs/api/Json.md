# Std.Json

Recursive JSON values — object/array parse and stringify.

`parse` returns `Result Json String`. `stringify` emits compact JSON.
`get` / `asString` / `asInt` work from expression programs (imported
constructors can be pattern-matched). Scalar helpers
(`stringifyString`, `parseInt`, …) stay available for fragments.
`yls-yona` uses `parse` / `stringify` / `get` for JSON-RPC bodies.

Parse and stringify run in the C runtime (recursive ADT walk).

## Types

### Json

`type Json =`

JSON value: null, bool, int, float, string, array, or object.
Objects preserve member order as a sequence of `(key, value)` pairs.

## Functions

### `parse : String -> Result`

Parse one JSON value. Trailing non-whitespace is an error.

```
parse "{\"id\":1}"   # => Ok (JsonObject [("id", JsonInt 1)])
```

### `stringify : Json -> String`

Compact JSON text for `j`.

```
stringify (JsonInt 1)   # => "1"
```

### `get : a -> b -> c`

Look up `key` in a JSON object. `None` if `j` is not an object or the key is missing.

### `asString : a -> b`

Unwrap a JSON string.

### `asInt : a -> b`

Unwrap a JSON integer.

### `stringifyString : String -> String`

### `stringifyBool : Bool -> String`

### `stringifyFloat : Float -> String`

### `null : String`

### `parseInt : String -> Int`

### `parseFloat : String -> Float`
