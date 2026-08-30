# Std.String

String -- string manipulation and conversion.

Provides utilities for searching, transforming, splitting, and
converting strings. Iterator-returning functions (`split`, `lines`,
`chars`) use O(1) memory per element.

## Functions

### `length : String -> Int`

Returns the length of the string in bytes.

```yona
import length from Std\String in
length "hello"   # => 5
```

### `isEmpty : String -> Bool`

Returns `true` if the string has zero length.

```yona
import isEmpty from Std\String in
isEmpty ""      # => true
isEmpty "hi"    # => false
```

### `toUpperCase : String -> String`

Convert all characters to uppercase.

```yona
import toUpperCase from Std\String in
toUpperCase "hello"   # => "HELLO"
```

### `toLowerCase : String -> String`

Convert all characters to lowercase.

```yona
import toLowerCase from Std\String in
toLowerCase "HELLO"   # => "hello"
```

### `trim : String -> String`

Remove leading and trailing whitespace.

```yona
import trim from Std\String in
trim "  hello  "   # => "hello"
```

### `indexOf : String -> String -> Int`

Return the index of the first occurrence of `needle`, or -1 if not found.

```yona
import indexOf from Std\String in
indexOf "hello world" "world"   # => 6
```

### `contains : String -> String -> Bool`

Returns `true` if `str` contains `needle`.

```yona
import contains from Std\String in
contains "hello world" "world"   # => true
```

### `startsWith : String -> String -> Bool`

Returns `true` if `str` starts with `prefix` (`startsWith prefix str`).

```yona
import startsWith from Std\String in
startsWith "hel" "hello"   # => true
```

### `endsWith : String -> String -> Bool`

Returns `true` if `str` ends with `suffix` (`endsWith suffix str`).

```yona
import endsWith from Std\String in
endsWith ".txt" "hello.txt"   # => true
```

### `substring : String -> Int -> Int -> String`

Extract a substring from index `start` (inclusive) to `end` (exclusive).

```yona
import substring from Std\String in
substring "hello" 1 4   # => "ell"
```

### `replace : String -> String -> String -> String`

Replace all occurrences of `old` with `new`.

```yona
import replace from Std\String in
replace "aabaa" "a" "x"   # => "xxbxx"
```

### `split : String -> String -> [String]`

Split a string by a delimiter. Returns an `Iterator String`.

```yona
import split from Std\String in
let iter = split "a,b,c" "," in
# consume with iterator protocol
```

### `join : String -> [a] -> String`

Join a sequence of strings with a separator.

```yona
import join from Std\String in
join ", " ["a", "b", "c"]   # => "a, b, c"
```

### `charAt : String -> Int -> Int`

Returns the character code (Int) at the given index.

```yona
import charAt from Std\String in
charAt "ABC" 0   # => 65
```

### `padLeft : Int -> String -> String -> String`

Pad `str` on the left with `pad` until it reaches `width`.

```yona
import padLeft from Std\String in
padLeft 5 "0" "42"   # => "00042"
```

### `padRight : Int -> String -> String -> String`

Pad `str` on the right with `pad` until it reaches `width`.

```yona
import padRight from Std\String in
padRight 5 "." "hi"   # => "hi..."
```

### `reverse : String -> String`

Reverse a string.

```yona
import reverse from Std\String in
reverse "hello"   # => "olleh"
```

### `repeat : Int -> String -> String`

Repeat a string `n` times.

```yona
import repeat from Std\String in
repeat 3 "ab"   # => "ababab"
```

### `take : Int -> String -> String`

Take the first `n` characters of a string.

```yona
import take from Std\String in
take 3 "hello"   # => "hel"
```

### `drop : Int -> String -> String`

Drop the first `n` characters of a string.

```yona
import drop from Std\String in
drop 3 "hello"   # => "lo"
```

### `count : String -> String -> Int`

Count the number of non-overlapping occurrences of `needle` in `str`.

```yona
import count from Std\String in
count "ababa" "ab"   # => 2
```

### `lines : String -> Iterator a`

Split a string into lines. Returns an `Iterator String`.

```yona
import lines from Std\String in
let iter = lines "a\nb\nc" in
# consume with iterator protocol
```

### `unlines : [a] -> String`

Join a sequence of strings with newline separators.

```yona
import unlines from Std\String in
unlines ["a", "b", "c"]   # => "a\nb\nc"
```

### `chars : String -> Iterator a`

Returns an `Iterator` over individual characters of the string.

```yona
import chars from Std\String in
let iter = chars "hello" in
# consume with iterator protocol
```

### `fromChars : [a] -> String`

Build a string from a sequence of character codes.

```yona
import fromChars from Std\String in
fromChars [72, 105]   # => "Hi"
```

### `toInt : String -> Int`

Parse a string as an integer.

```yona
import toInt from Std\String in
toInt "42"   # => 42
```

### `toFloat : String -> Float`

Parse a string as a float.

```yona
import toFloat from Std\String in
toFloat "3.14"   # => 3.14
```
