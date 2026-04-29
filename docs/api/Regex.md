# Std.Regex

Regex — PCRE2-backed regular expressions.

```
let re = compile "[a-z]+" in
matches re "hello 123"
```

## Functions

### `extern`

```yona
extern compile    : String -> Int                       = "yona_regex_compile"
```

### `extern`

```yona
extern matches    : Int -> String -> Bool               = "yona_regex_matches"
```

### `extern`

```yona
extern find       : Int -> String -> Seq                = "yona_regex_find"
```

### `extern`

```yona
extern findAll    : Int -> String -> Seq                = "yona_regex_findAll"
```

### `extern`

```yona
extern replace    : Int -> String -> String -> String   = "yona_regex_replace"
```

### `extern`

```yona
extern replaceAll : Int -> String -> String -> String   = "yona_regex_replaceAll"
```

### `extern`

```yona
extern split      : Int -> String -> Seq                = "yona_regex_split"
```

