# Std.Encoding

Encoding -- string encoding and decoding utilities.

Provides Base64, hex, URL percent-encoding, and HTML entity escaping.

## Functions

### `base64Encode : String -> String`

Encode a string to Base64.

```yona
import base64Encode from Std\Encoding in
base64Encode "hello"   # => "aGVsbG8="
```

### `base64Decode : String -> String`

Decode a Base64-encoded string back to its original form.

```yona
import base64Decode from Std\Encoding in
base64Decode "aGVsbG8="   # => "hello"
```

### `hexEncode : String -> String`

Encode each byte of a string as two hex characters.

```yona
import hexEncode from Std\Encoding in
hexEncode "AB"   # => "4142"
```

### `hexDecode : String -> String`

Decode a hex-encoded string back to bytes.

```yona
import hexDecode from Std\Encoding in
hexDecode "4142"   # => "AB"
```

### `urlEncode : String -> String`

Percent-encode a string for use in URLs.

```yona
import urlEncode from Std\Encoding in
urlEncode "hello world"   # => "hello%20world"
```

### `urlDecode : String -> String`

Decode a percent-encoded URL string.

```yona
import urlDecode from Std\Encoding in
urlDecode "hello%20world"   # => "hello world"
```

### `htmlEscape : String -> String`

Escape HTML special characters (`<`, `>`, `&`, `"`, `'`) to their entity equivalents.

```yona
import htmlEscape from Std\Encoding in
htmlEscape "<b>hi</b>"   # => "&lt;b&gt;hi&lt;/b&gt;"
```
