# Std.Convert

Lawful total conversions, checked numeric conversions, and strict parsing.

Generic dispatch is target-witness directed: `convert "" 42`,
`tryConvert 0 1.0`, and `parse 0 "42"`. The named helpers below make common
concrete conversions concise while retaining the same trait contracts.

## Functions

### `parseInt : String -> Result (Int, ParseError)`

### `parseFloat : String -> Result (Float, ParseError)`

### `parseBool : String -> Result (Bool, ParseError)`

### `parseString : String -> Result (String, ParseError)`

### `formatInt : Int -> String`

### `formatFloat : Float -> String`

### `formatBool : Bool -> String`

### `intToFloat : Int -> Result (Float, ConvertError)`

### `floatToInt : Float -> Result (Int, ConvertError)`

### `encodeUtf8 : String -> ByteArray`

### `decodeUtf8 : ByteArray -> Result (String, ConvertError)`
