# Std.Json

Json -- JSON serialization helpers.

Provides functions to convert Yona values to JSON string fragments
and to parse JSON primitives. Useful for building JSON output or
parsing simple JSON values.

## Functions

### stringify

`stringify : Int -> String`

Convert an integer to its JSON string representation.

```yona
import stringify from Std\Json in
stringify 42   # => "42"
```

### stringifyString

`stringifyString : String -> String`

Convert a string to a JSON-quoted string with proper escaping.

```yona
import stringifyString from Std\Json in
stringifyString "hello \"world\""   # => "\"hello \\\"world\\\"\""
```

### stringifyBool

`stringifyBool : Bool -> String`

Convert a boolean to `"true"` or `"false"`.

```yona
import stringifyBool from Std\Json in
stringifyBool true   # => "true"
```

### stringifyFloat

`stringifyFloat : Float -> String`

Convert a float to its JSON string representation.

```yona
import stringifyFloat from Std\Json in
stringifyFloat 3.14   # => "3.14"
```

### null

`null : String`

Returns the JSON null literal string `"null"`.

```yona
import null from Std\Json in
null   # => "null"
```

### parseInt

`parseInt : String -> Int`

Parse a JSON integer string to an Int.

```yona
import parseInt from Std\Json in
parseInt "42"   # => 42
```

### parseFloat

`parseFloat : String -> Float`

Parse a JSON float string to a Float.

```yona
import parseFloat from Std\Json in
parseFloat "3.14"   # => 3.14
```
