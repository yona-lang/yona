# Std.Utf16

UTF-8 byte offsets ↔ LSP UTF-16 positions.

Matches the C++ `yls` mapper (`include/lsp/Utf16.h`): 0-based line,
UTF-16 code-unit column, CRLF as one line break, non-BMP scalars as
two units. The same functions are the documented C ABI in
`include/yona/runtime/utf16.h`.

## Functions

### `offsetToLine : String -> Int -> Int`

Line of the UTF-16 position for a UTF-8 byte offset.

### `offsetToCharacter : String -> Int -> Int`

Character (UTF-16 column) of the UTF-16 position for a UTF-8 byte offset.

### `positionToOffset : String -> Int -> Int -> Int`

UTF-8 byte offset for an LSP (line, character) position.

```
offsetToLine "ab\ncd" 4          # => 1
offsetToCharacter "ab\ncd" 4     # => 1
positionToOffset "ab\ncd" 1 1    # => 4
```
