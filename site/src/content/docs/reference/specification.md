---
title: Language specification
description: >-
  The normative description of Yona's lexical structure, expressions,
  patterns, types, and modules, with implementation notes from the reference
  compiler.
---

This document specifies the Yona language as implemented by the reference
compiler `yonac`. Normative rules are stated in prose; *implementation notes*
describe how `yonac` realizes them and are informative, not binding on other
implementations.

A Yona **program is a single expression**. Compiling a source file that
contains an expression produces an executable whose exit code is the
expression's value (for integer results). A source file may instead contain a
**module declaration**, which compiles to a linkable object file plus an
interface file (§7).

## 1. Lexical structure

### 1.1 Source text

Source files are UTF-8. Identifiers are ASCII: functions and variables match
`[a-z_][A-Za-z0-9_]*` (camelCase by convention), type and constructor names
and module segments match `[A-Z][A-Za-z0-9_]*` (PascalCase).

### 1.2 Comments

```yona
# a line comment runs to end of line
## a doc comment — extracted by the API documentation generator

/* a block comment
   /* block comments nest */
   and may span lines */
```

`#` introduces a line comment. `##` at the start of a line is a documentation
comment, attached to the following declaration by documentation tooling; to
the compiler it is an ordinary comment. `/* … */` comments nest and may
contain newlines.

### 1.3 Newlines

Newlines are significant tokens. A newline (or a `;`, which is equivalent)
terminates an expression in the three positions where expression sequences
occur: **case arms**, **`do`-block steps**, and **module-level function
bodies**.

A newline is *suppressed* — treated as ordinary whitespace — in exactly these
situations:

1. **Inside brackets** `()`, `[]`, `{}`. Bracketed expressions may span any
   number of lines. Exception: when a `case`, `do`, `with`, or `handle` block
   is open *inside* the brackets, newlines again act as clause separators, so
   the block's arms still terminate correctly.
2. **After a binary operator or continuation token** (`+`, `*`, `->`, `=`,
   `,`, `|>`, …). This permits natural line continuation:

```yona
let total = price +
            tax
in total
```

This rule is what allows juxtaposition application (§3.6) to coexist with
expression sequences: `f x y` never runs onto the next line accidentally,
because the newline ends it unless an operator invites continuation.

### 1.4 Keywords

```
let in do end case of if then else with as
module import export from type trait instance
try catch raise extern async daemon
perform handle resume effect for
```

### 1.5 Literals

| Form | Examples | Notes |
|------|----------|-------|
| Integer | `42`, `-17`, `1_000_000` | 64-bit signed; `_` separators permitted between digits |
| Float | `3.14`, `-0.5`, `1.23e-4` | IEEE 754 double |
| String | `"hello"`, `"a\nb"` | UTF-8; escapes `\"` `\\` `\n` `\r` `\t` `\0`; interpolation §3.2 |
| Character | `'a'`, `'\n'` | single Unicode scalar |
| Boolean | `true`, `false` | |
| Unit | `()` | the empty tuple; the type and value of "nothing" |
| Symbol | `:ok`, `:not_found` | interned atoms, snake_case by convention |

*Implementation note.* Symbols are interned to 64-bit integer identifiers at
compile time; symbol comparison is a single integer comparison, and matching
on symbols compiles to an integer switch.

## 2. Values and their syntax

### 2.1 Collections

```yona
[1, 2, 3]                      # sequence (persistent list)
[]                             # empty sequence
(1, "two", true)               # tuple — fixed arity, heterogeneous
(42,)                          # one-element tuple
{1, 2, 3}                      # set
{"name": "Ada", "age": 36}     # dictionary
{}                             # empty dictionary
```

Sequences, sets, and dictionaries are **persistent**: every operation returns
a new value sharing structure with the old one. Tuples are fixed-arity
product values.

*Implementation note.* Small sequences are flat arrays; large ones are
radix-balanced tries. Dictionaries and sets are hash array mapped tries
(HAMTs). All share structure on update. See
[Persistent data structures](/guides/persistent-data-structures/).

### 2.2 Generators (comprehensions)

```yona
[x * 2 for x = xs]                     # sequence generator
[x for x = xs, if x > 3]               # with guard
{x * 2 for x = xs}                     # set generator
{k : v * 10 for k = ks}                # dictionary generator
[| f x for x = xs ]                    # parallel generator (§6.3)
```

The general form is `[expr for pattern = source]` with an optional
`, if guard`. The `source` is any sequence-valued expression.

*Implementation note.* Generators compile to counted loops, not closures.
With a guard, a two-pass strategy first counts matches, then fills the
result without reallocation. Chained collection pipelines are stream-fused
into a single loop when the compiler can prove it safe.

## 3. Expressions

### 3.1 `let`

```yona
let x = 42 in x + 1                       # single binding
let x = 10, y = 20 in x + y               # multiple bindings
let add x y = x + y in add 3 4            # function-definition binding
let (a, b) = (1, 2) in a + b              # pattern binding
let _ = println "side effect" in 42       # discard binding
```

`let bindings in body` introduces bindings scoped to `body`. Bindings are
separated by commas. A binding's left side is a pattern; a name followed by
parameter patterns is sugar for binding a lambda. A type annotation may
precede a function binding on its own line:

```yona
let add : Int -> Int -> Int
    add x y = x + y
in add 3 4
```

**Evaluation order.** Bindings that depend on earlier bindings observe their
values. Bindings that are *independent* of one another have **no defined
sequential order** and may be evaluated concurrently (§6.2). Code whose side
effects require an order must use `do`.

### 3.2 Strings and interpolation

Within a string literal, `{name}` interpolates a variable and `{(expr)}`
interpolates a parenthesized expression; non-string values are converted to
their textual form:

```yona
let x = 6 in "the answer is {(x * 7)}"    # "the answer is 42"
```

### 3.3 `do`

```yona
do
    fd = tcpConnect "localhost" 8080     # binding step
    send fd "hello"                      # expression step
    response = recv fd 4096
    response                             # value of the block
end
```

`do … end` evaluates its steps **strictly in textual order**. A step of the
form `name = expr` binds `name` for subsequent steps. The block's value is
its last expression. `do` is the sequencing primitive; use it whenever side
effects must happen in order.

### 3.4 `if`

```yona
if x > 0 then "positive"
else if x < 0 then "negative"
else "zero"
```

`if` is an expression; both branches are required and must have the same
type.

### 3.5 Functions and lambdas

```yona
add(x, y) -> x + y             # definition, parenthesized parameters
factorial(0) -> 1              # clauses selected by pattern
factorial(n) -> n * factorial(n - 1)

abs x if x >= 0 = x            # equation form with guard
abs x if x < 0  = -x

scale : Float -> Float -> Float    # optional annotation
scale factor x = factor * x

\x -> x * 2                    # lambda
\(x, y) -> x + y               # lambda with tuple pattern
\-> expensive ()               # zero-argument lambda (thunk)
```

A function of several clauses is matched top to bottom; the first clause
whose patterns (and guard, if present) match is selected. Functions are
first-class values; partial application is automatic:

```yona
let add5 = add 5 in add5 10    # => 15
```

**Zero-arity functions auto-evaluate** when referenced by name (Yona is
strict). To pass one as a value, wrap it in a thunk: `\-> f`.

### 3.6 Application

Application is by **juxtaposition** — `f x y` — or parenthesized —
`f(x, y)`. Juxtaposition binds tighter than every binary operator:
`f x + g y` parses as `(f x) + (g y)`.

Pipes reverse application order for pipeline style:

```yona
value |> stage1 |> stage2      # stage2 (stage1 value)
stage2 <| stage1 <| value      # the same, right-to-left
```

*Implementation note.* `yonac` compiles functions by **deferred
monomorphization**: a definition is stored as a typed AST and compiled at
each call site where concrete argument types are known. Closures capture
free variables in a heap environment; a closure value is
`{fn_ptr, ret_tag, arity, captures…}`.

### 3.7 `case`

```yona
case value of
    0 -> "zero"
    n if n > 0 -> "positive"
    _ -> "negative"
end
```

`case scrutinee of clauses end` evaluates the scrutinee once, then tests
clauses top to bottom (§4 defines patterns). The first matching clause's
body is the expression's value. All clause bodies must have the same type.
If no clause matches at runtime, the program aborts with a match error;
the compiler warns when it can prove a constructor uncovered.

### 3.8 `with` (resources)

```yona
with handle = tcpConnect "localhost" 8080 in
    send handle "hello"
# handle is closed when the body completes
```

`with name = resource in body` evaluates `resource`, binds it to `name`,
evaluates `body`, and then releases the resource by calling the `Closeable`
trait's `close` method — resolved statically for the resource's type. Using
a value whose type does not implement `Closeable` is a compile-time error.

*Current limitation.* Release is guaranteed when `body` completes normally.
If an exception propagates out of `body`, `close` is **not** currently
invoked on the unwind path.

### 3.9 Exceptions

```yona
type Error = NotFound String | IOError String

raise (NotFound "config.toml")

try
    riskyOperation ()
catch
    NotFound path -> "missing: " ++ path
    IOError msg   -> "io: " ++ msg
    _             -> "unknown failure"
end
```

Exception values are ordinary ADT values. `raise` throws; `try … catch …
end` matches the raised value against clauses like a `case`. An unmatched
exception propagates; an uncaught exception terminates the program with a
stack trace.

### 3.10 Algebraic effects <span class="yona-status yona-status--partial">Partial</span>

```yona
handle
    perform State.get ()
with
    State.get () resume -> resume 42
    return val -> val
end
```

`perform Effect.op arg` requests the operation `Effect.op` from the nearest
enclosing `handle` that covers it. A handler clause receives the operation's
argument and a `resume` continuation; `return val -> …` transforms the
handled expression's normal result.

Function types carry a **latent effect row** listing the operations the
function may perform: `Int -> !{State.get} Int`. `handle` subtracts the
operations it covers; applying a function whose row is not fully covered at
the top level is error **E0202**, reported at the introducing `perform` with
a note at the call site. Higher-order functions carry open rows (`!{|r}`)
that unify with their argument's row.

*Current limitations.* Handlers are shallow, in-scope dispatch: `resume` is
an identity continuation, not a captured delimited continuation. `effect`
declarations do not parse yet; operations are identified by their
`Effect.op` label at `perform` sites. See
[Effects](/learn/effects/) for the practical guide.

### 3.11 `extern` (C FFI)

```yona
extern sqrt : Float -> Float in
extern pow : Float -> Float -> Float in
sqrt (pow 2.0 10.0)            # => 32.0

extern async readFile : String -> String in
readFile "data.txt"            # non-blocking; auto-awaited at use
```

`extern name : Type in body` declares an external C symbol with a Yona type;
the linker resolves it. Type mapping: `Int` ↔ `i64`, `Float` ↔ `double`,
`Bool` ↔ `i1`, `String` ↔ `char*`. Curried annotation `A -> B -> C` denotes
a two-argument C function returning `C`. The `async` modifier submits the
call to the runtime's thread pool and yields a promise, awaited
transparently at use sites (§6.2).

## 4. Patterns

| Pattern | Example | Matches |
|---------|---------|---------|
| Literal | `42`, `"hi"`, `:ok`, `true` | that exact value |
| Variable | `x` | anything; binds `x` |
| Wildcard | `_` | anything; binds nothing |
| Tuple | `(a, _, c)` | tuples of that arity |
| Sequence | `[]`, `[x]`, `[a, b]` | sequences of that exact length |
| Head–tail | `[h \| t]`, `[a, b \| rest]` | non-empty sequences; `t`/`rest` bind the remainder |
| Constructor | `Some x`, `Rect w h` | values built by that constructor |
| Record | `Person{name: n}` | matches named fields; others ignored |
| Dictionary | `{"key": v}` | dictionaries containing the key |
| As-binding | `[h \| t] as whole` | matches the inner pattern and binds the whole value |
| Or-pattern | `:a \| :b -> …` | either alternative; both must bind the same names |
| Guard | `n if n > 0 -> …` | pattern matches *and* guard is true |

Patterns appear in `case` clauses, function parameters, `let` bindings, and
`catch` clauses. Matching is left-to-right, top-to-bottom, with no
backtracking within a clause.

## 5. Operators

Precedence, highest to lowest; all binary operators are left-associative
except `**`, `::`, and the arrows:

| Level | Operators | Meaning |
|-------|-----------|---------|
| 1 | `.` | field access |
| 2 | juxtaposition | function application |
| 3 | `**` | power |
| 4 | `!` `~` unary `-` | logical not, bitwise not, negation |
| 5 | `*` `/` `%` | multiplicative |
| 6 | `+` `-` | additive |
| 7 | `<<` `>>` `>>>` | shifts |
| 8 | `++` | concatenation (sequences, strings) |
| 9 | `::` | cons (prepend) |
| 10 | `<` `>` `<=` `>=` | comparison |
| 11 | `==` `!=` | equality |
| 12 | `&` | bitwise and |
| 13 | `^` | bitwise xor |
| 14 | `\|` | bitwise or |
| 15 | `&&` | logical and (short-circuit) |
| 16 | `\|\|` | logical or (short-circuit) |
| 17 | `\|>` `<\|` | pipes |

Sequence-specific operators: `x :: xs` prepends and `xs ++ ys`
concatenates. The lexer reserves `:>` (append), `--` (remove), and `in`
(membership) as operator tokens, but the current compiler does not accept
them in expressions — use `xs ++ [x]`, `Std\List.filter`, and
`Std\List.contains` (or `Std\Set.contains`) instead.

## 6. Evaluation model

### 6.1 Strictness

Yona is strictly evaluated: arguments are evaluated before application, and
bindings before their bodies — with the single systematic exception of
asynchronous values (§6.2). There is no lazy evaluation; laziness is
expressed explicitly with thunks (`\-> e`) or `Iterator`/`Std\Stream`
pipelines.

### 6.2 Transparent asynchrony

Functions that perform I/O (and `extern async` functions) return a
**promise** internally. The type system tracks promises invisibly: when a
promise appears where its underlying value is required — as an operator
operand, function argument, or condition — the compiler inserts an await
coercion. Users never write `async` or `await`, and no function is "colored".

Because `let` bindings without mutual dependencies have no defined order,
independent asynchronous bindings are **submitted before any is awaited**:

```yona
let
    a = readFile "foo.txt",    # submitted
    b = readFile "bar.txt"     # submitted
in a ++ b                      # both awaited here; elapsed ≈ max, not sum
```

*Implementation note.* On Linux, file and network I/O submit to io_uring;
CPU-bound async work runs on a work-stealing thread pool. Buffers passed to
in-flight kernel operations are pinned. See
[Concurrency in depth](/guides/concurrency/).

### 6.3 Parallel generators

`[| f x for x = xs ]` evaluates `f` over the elements concurrently on the
thread pool and preserves order in the result.

### 6.4 Memory

Values are managed by **atomic reference counting** with recursive
destructors; there is no tracing garbage collector and no stop-the-world
pause. The compiler applies Perceus-style ownership transfer (callee-owns
calling convention), uniqueness-based in-place mutation for uniquely owned
values, and escape analysis that arena-allocates values proven not to
escape. See [Memory and linearity](/guides/memory/).

## 7. Types

### 7.1 Inference

The type system is Hindley–Milner: every expression has a principal type,
inferred without annotations. Optional annotations (`name : Type` preceding
a definition) are checked, not trusted. Polymorphic functions are compiled
by monomorphization — one native instantiation per concrete type used.

### 7.2 Algebraic data types

```yona
type Option a = Some a | None
type Result a e = Ok a | Err e
type Tree a = Leaf | Node (Tree a) a (Tree a)
type Lazy a = Cons a (() -> Lazy a) | Empty       # function-typed field
type Person = Person { name : String, age : Int } # named fields
```

Constructors are first-class functions. Named-field types support dot
access, record patterns, and functional update:

```yona
let p = Person { name = "Ada", age = 36 } in
(p.name, p { age = 37 })
```

*Implementation note.* Non-recursive ADTs compile to flat
`{tag, payload}` structs; recursive ADTs and ADTs with function-typed
fields are heap-allocated.

### 7.3 Traits <span class="yona-status yona-status--stable">Stable</span>

```yona
trait Eq a
    eq  : a -> a -> Bool
    neq : a -> a -> Bool
    neq x y = if eq x y then false else true     # default method
end

instance Show a => Show (Option a)
    show opt = case opt of
        Some x -> "Some(" ++ show x ++ ")"
        None   -> "None"
    end
end

trait Eq a => Ord a                               # superclass constraint
    compare : a -> a -> Int
end
```

Traits are type classes resolved **statically**: each call site compiles the
concrete instance directly (monomorphization), with no runtime dispatch
cost. Instances are always public; `export trait Name` exports a
declaration. See [Traits](/guides/traits/).

### 7.4 Effect rows <span class="yona-status yona-status--partial">Partial</span>

Function arrows carry the set of effect operations the function may perform:
`a -> !{State.get} Int`. Rows are inferred, unioned at application,
subtracted by `handle`, propagated through `.yonai` interfaces, and kept
open (`!{|r}`) on higher-order parameters. §3.10 lists current limitations.

### 7.5 Linear types <span class="yona-status yona-status--partial">Partial</span>

`Linear a` marks values that must be consumed **exactly once**: file
handles, sockets, process handles, channel endpoints. The linearity checker
rejects duplication and silent dropping; `with` is the idiomatic consumer.
`@borrow` marks parameters that use a linear value without consuming it.
See [Memory and linearity](/guides/memory/).

### 7.6 Row-polymorphic records <span class="yona-status yona-status--stable">Stable</span>

Record types unify by row: a function using `r.name` accepts any record
containing a `name` field of the right type, and the residual row is
polymorphic.

## 8. Modules

```yona
module Data\Geometry

export area, perimeter
export type Shape

type Shape = Circle Float | Rect Float Float

area s = case s of
    Circle r -> 3.141592653589793 * r * r
    Rect w h -> w * h
end

perimeter s = case s of
    Circle r -> 2.0 * 3.141592653589793 * r
    Rect w h -> 2.0 * (w + h)
end
```

A module is a **top-level declaration** — not an expression — and extends to
end of file. `export` statements name exported functions; `export type T`
exports a type with all its constructors; `export f from Other\Module`
re-exports. Module names are backslash-separated paths (`Std\List`).

Imports are expressions:

```yona
import map, filter from Std\List in …        # selective
import length as len from Std\String in …    # aliased
import Std\Math in …                         # whole module
Std\List::map (\x -> x + 1) [1, 2, 3]        # fully qualified, no import
```

*Implementation note.* A module compiles to a native object file with
C-ABI exports (mangled `yona_Pkg_Mod__func`) and a `.yonai` **interface
file** carrying types, effect rows, linearity, and — for generic functions —
the source text itself (`GENFN`), so a caller with new concrete types can
re-monomorphize the function locally. `yonac -I path` adds interface search
paths. See [Modules and interfaces](/guides/modules-interfaces/).

## 9. Conformance and diagnostics

Compiler diagnostics carry stable codes (`E0100`-style, `W…` for warnings).
`yonac --explain E0202` prints the full explanation for a code. The
[error code index](/reference/error-codes/) lists user-facing codes and
their meanings.
