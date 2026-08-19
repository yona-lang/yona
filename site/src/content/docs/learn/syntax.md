---
title: Syntax and evaluation
description: Yona's lexical rules, literals, and evaluation model — everything is an expression, evaluated strictly.
---

Yona is an expression language: there are no statements. A program is a
single expression, and evaluating it produces the program's result. This
page covers the lexical ground rules — how expressions begin and end, what
literals look like, and how they evaluate.

## Everything is an expression

Every construct — `if`, `case`, `let`, `do`, function bodies — is an
expression with a value. There is no `return` keyword; a function's value is
the value of its body, and a block's value is its last expression.

```yona
let status = if ready then :ok else :waiting in
let label = case status of
    :ok      -> "ready"
    :waiting -> "hold on"
end in
label   # => "ready" (when ready is true)
```

## Strict evaluation

Yona evaluates strictly: arguments are evaluated before a function is
applied, and `let` bindings are evaluated when bound, not when first used.
Order among *independent* `let` bindings is not guaranteed (independent
asynchronous bindings may even run in parallel); when side-effect order
matters, use a `do` block, whose expressions always run top to bottom.

```yona
do
    print "first"    # guaranteed to run before the next line
    print "second"
    42               # => 42 — the block's value is its last expression
end
```

## Newlines and semicolons

Newlines are significant tokens. A newline (or an equivalent `;`) terminates
an expression in the three places where consecutive expressions can appear:

- arms of a `case` expression,
- steps of a `do` block,
- function definitions in a module body.

```yona
case x of
  :ok -> handleOk x        # newline ends this arm
  :error -> handleError x
  _ -> fallback x
end

# Semicolons are interchangeable with newlines:
case x of :ok -> 1; :error -> 2; _ -> 0 end
```

Newlines are **suppressed** (treated as plain whitespace) in two situations,
which is what makes multi-line expressions natural:

1. Inside brackets — `()`, `[]`, `{}`:

```yona
let list = [
  1, 2, 3,
  4, 5, 6
] in list        # => [1, 2, 3, 4, 5, 6]
```

2. After a binary operator or a continuation token (`->`, `=`, `,`), so a
   line ending in an operator continues on the next line:

```yona
let total = price +
  tax +
  shipping in total
```

Implementation note. The lexer tracks bracket depth and the previous token
to decide whether a newline is a delimiter or whitespace; inside a
`case`/`do` block nested in brackets, newlines still reach the parser as
clause separators. This is what allows juxtaposition application (`f x y`)
without ambiguity at expression boundaries.

## Comments

Line comments start with `#`. Doc comments start with `##` and are attached
to the following definition (the stdlib's API reference is generated from
them). Block comments use `/* … */` and nest.

```yona
# a line comment

## Doubles a number. (doc comment — extracted into API docs)
double x = x * 2

/* block comment
   /* nested block comments are fine */
   still inside the outer comment */
double 21   # => 42
```

Never write `--` for a comment — `--` is the remove operator token, not a
comment introducer, and will produce a parse error.

## Literals

### Integers

`Int` is a 64-bit signed integer. Underscores may separate digits for
readability.

```yona
42
-17
1_000_000   # => 1000000
```

### Floats

`Float` is a 64-bit IEEE double. Scientific notation is supported.

```yona
3.14
-0.5
1.23e-4   # => 0.000123
```

### Strings

Strings are written in double quotes and support the usual escapes
(`\"`, `\\`, `\n`, `\t`, …).

```yona
"Hello, World!"
"Escaped \"quotes\" and \n newlines"
```

Strings interpolate expressions in braces: `{name}` for a plain variable,
`{(expr)}` — with parentheses — for anything containing operators or
application. Non-string values are converted automatically.

```yona
let name = "World" in "Hello {name}!"   # => "Hello World!"
let x = 6 in "result is {(x * 7)}"      # => "result is 42"
```

### Characters and booleans

```yona
'a'
'\n'
true
false
```

### Unit

`()` is the unit value — the empty tuple, used where there is nothing
meaningful to return.

```yona
()   # => ()
```

### Symbols

Symbols are interned constants written as `:snake_case`. Two occurrences of
the same symbol are always the same value.

```yona
:ok
:error
:not_found
```

Implementation note. Symbols are interned to 64-bit integer IDs at compile
time, so comparing two symbols is a single integer comparison, and pattern
matching on symbols compiles to an integer switch. See
[Pattern matching](/learn/pattern-matching/).

## Conditionals

`if` is an expression and the `else` branch is mandatory — every `if` must
produce a value of either branch. Both branches must have the same type.

```yona
if x > 0 then "positive"
else if x < 0 then "negative"
else "zero"
```

Prefer `case` over long `if`/`else` chains when you are matching on the
shape of a value — see [Pattern matching](/learn/pattern-matching/).

## Operator precedence

From highest to lowest binding strength:

1. Field access (`.`)
2. Function application (juxtaposition — `f x`)
3. Power (`**`)
4. Unary (`!`, `~`, unary `-`)
5. Multiplicative (`*`, `/`, `%`)
6. Additive (`+`, `-`)
7. Shift (`<<`, `>>`, `>>>`)
8. Join (`++`)
9. Cons (`::`, `:>`)
10. Comparison (`<`, `>`, `<=`, `>=`)
11. Equality (`==`, `!=`)
12. Bitwise AND (`&`)
13. Bitwise XOR (`^`)
14. Bitwise OR (`|`)
15. Membership (`in`)
16. Logical AND (`&&`)
17. Logical OR (`||`)
18. Pipe (`|>`, `<|`)

Function application binds tighter than every binary operator, so
`f x + g y` parses as `(f x) + (g y)`:

```yona
let f x = x * 10, g y = y + 1 in
f 2 + g 3   # => 24, i.e. (f 2) + (g 3)
```

The full grammar and operator semantics are in the
[language specification](/reference/specification/).

## Where to next

- [Functions](/learn/functions/) — definitions, lambdas, application, pipes.
- [Pattern matching](/learn/pattern-matching/) — `case` and every pattern form.
- [Types and data](/learn/types/) — inference, ADTs, records, traits.
- [Collections](/learn/collections/) — sequences, dictionaries, sets, generators.
