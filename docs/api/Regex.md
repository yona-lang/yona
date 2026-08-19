# Std.Regex

Regex — PCRE2-backed regular expressions.

```
let re = compile "[a-z]+" in
matches re "hello 123"
```

## Functions

### `compile : String -> Int`

### `matches : Int -> String -> Bool`

### `find : Int -> String -> Seq`

### `findAll : Int -> String -> Seq`

### `replace : Int -> String -> String -> String`

### `replaceAll : Int -> String -> String -> String`

### `split : Int -> String -> Seq`
