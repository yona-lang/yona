# Std.Regex

Regex — PCRE2-backed regular expressions.

```
let re = compile "[a-z]+" in
matches re "hello 123"
```

## Functions

### compile

`compile : String -> Int`

### matches

`matches : Int -> String -> Bool`

### find

`find : Int -> String -> Seq`

### findAll

`findAll : Int -> String -> Seq`

### replace

`replace : Int -> String -> String -> String`

### replaceAll

`replaceAll : Int -> String -> String -> String`

### split

`split : Int -> String -> Seq`
