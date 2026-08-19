# Std.Json

Json -- JSON serialization helpers.

Provides functions to convert Yona values to JSON string fragments
and to parse JSON primitives. Useful for building JSON output or
parsing simple JSON values.

## Functions

### stringify

`stringify n`

Convert an integer to its JSON string representation.

```yona
import stringify from Std\Json in
stringify 42   # => "42"
```

### stringifyString

`stringifyString str`

Convert a string to a JSON-quoted string with proper escaping.

```yona
import stringifyString from Std\Json in
stringifyString "hello \"world\""   # => "\"hello \\\"world\\\"\""
```

### stringifyBool

`stringifyBool b`

Convert a boolean to `"true"` or `"false"`.

```yona
import stringifyBool from Std\Json in
stringifyBool true   # => "true"
```

### stringifyFloat

`stringifyFloat x`

Convert a float to its JSON string representation.

```yona
import stringifyFloat from Std\Json in
stringifyFloat 3.14   # => "3.14"
```

### null

`null`

Returns the JSON null literal string `"null"`.

```yona
import null from Std\Json in
null   # => "null"
```

### parseInt

`parseInt str`

Parse a JSON integer string to an Int.

```yona
import parseInt from Std\Json in
parseInt "42"   # => 42
```

### parseFloat

`parseFloat str`

Parse a JSON float string to a Float.

```yona
import parseFloat from Std\Json in
parseFloat "3.14"   # => 3.14
```
