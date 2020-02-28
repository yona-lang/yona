# Yatta

[![Build Status](https://travis-ci.org/yatta-lang/yatta.svg?branch=master)](https://travis-ci.org/yatta-lang/yatta)
[![Gitter](https://badges.gitter.im/yattalang/community.svg)](https://gitter.im/yattalang/community?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge)

Yatta is a minimalistic, opiniated, (strongly) dynamically typed, strict, functional programming language, with ML-like syntax, for GraalVM polyglot virtual machine (VM). Yatta puts a strong focus on readability of the code.

Yatta abstract users from dealing with non-blocking asynchronous computations and parallelism. While these features are commonly available in other languages nowadays, they are almost exclusively non-native solutions that come in forms of libraries or frameworks and are difficult to integrate with existing codebases. On top of that, dealing with these additional libraries requires conscious effort of the programmer to choose/learn/integrate these libraries into their mindset when writing new code.

## Goals & Priorities

* good readability - simple syntax, few keywords, virtually no boilerplate
* few types of expressions - `module`, function(function does not need a keyword, it is defined by a name and arguments (patterns)), `case`, `if`, `let`, `do` and `try`/`catch` + `raise`
* simple module system - ability to expose functions for use in other modules, and ability to import them from other modules. Modules are first level values and can be created dynamically
* single expression principle - program is always one expression - this enables simpler evaluation and syntax, allows writing simple scripts as well as complex applications
* powerful and efficient built-in data structures with full support for pattern matching, including Sequence, Dictionary and Set
* custom data types representable as records
* built-in runtime level non-blocking asynchronous IO
* simple runtime level concurrency, no additional types or data structures necessary
* advanced concurrency provided by built-in Software Transactional Memory (STM) module
* polyglot language - interoperability with other languages via GraalVM

## Installation Instructions
It is possible to run Yatta locally, whether for play purposes or development of new features.

### Build requirements - Debian
    sudo apt install build-essential zlib1g zlib1g-dev 

### Getting GraalVM
    export JAVA_HOME=$HOME/jdk PATH=$JAVA_HOME/bin:$PATH
    wget -O $HOME/jdk.tar.gz https://github.com/graalvm/graalvm-ce-builds/releases/download/vm-19.3.1/graalvm-ce-java11-linux-amd64-19.3.1.tar.gz
    mkdir $HOME/jdk && tar -xzf $HOME/jdk.tar.gz -C $HOME/jdk --strip-components=1
    $HOME/jdk/bin/gu install native-image
    mvn install -DskipTests=true -Dmaven.javadoc.skip=true -B -V

### Running Yatta
After cloning the repository, Yatta interpreter can be run simply by:
    ./yatta <filename.yatta>

Note: for simple testing, Yatta can run without file argument, then it will read any input provided to it. It can be ended by Ctrl-D (EOF).

## Contributing
Here is a list of some ideas on what to do:

- standard library modules
- IDEs / editor integration

## Data Types
Yatta is dynamically typed language, meaning that types are checked during runtime. Values in Yatta are represented using following data types:

* boolean - `true` or `false`
* integer - 64 bit number
* double - double-precision 64-bit IEEE 754 floating point, written either using a floating point number or by an integer with an `f` suffix
* byte - single byte, written as an integer literal with a `b` suffix
* identifier: small letter followed by any number of letters or numbers or underscores
* character - a single UTF-8 character in single quotes: `'c'`
* string - UTF-8 characters in quotes: `"hello world"`. String is technically a sequence of characters (highly optimized internally, but for a programmer it looks and feels as a sequence).
* symbols - preceded by a colon: `:error`
* tuple - in parenthesis: `(1, 2, 3)`
* sequence - biderctional list like structure with constant time random access in brackets: `[1, 2, 3]`
* dictionary - in curly braces: `{:one = 1, :two = 2}`
* set - in curly braces: `{1, 2, 3}`
* anonymous function(lambda): `\first_arg second_arg -> first_arg + second_arg`
* unit: `()`
* native object: underlying Java object that is used by some stdlib functions, such as File descriptor

## Execution Model and asynchronous non-blocking IO & Concurrency
Yatta provides fully transparent runtime system that integrates asynchronous non-blocking IO features with concurrent execution of the code. This means, there is no special syntax or special data types representing asynchronous computations. Everything related to non-blocking IO is hidden within the runtime and exposed via the standard library, and all expressions consisting of asynchronous expressions(Asynchronous expression is usually obtained from the standard library or created by function `async`) are evaluated in asynchronous, non-blocking matter.

This approach has provides several benefits to languages that provide these features via libaries, mainly that such library would have to be adopted by other libraries/frameworks in order to be usable and it would still impose additional boilerplate simply because libraries cannot typically change language syntax/semantics. This is why Yatta provides these features from day one, built into language syntax and semantics and therefore it is always available to any program without any external dependencies. At the same time, putting these features directly on the language/runtime level allows for additional optimizations that could otherwise be tricky or impossible.

The example below shows a simple program that reads line from two different files and writes a combined line to the third line. The execution order is as follows:

1. Read line from file 1, at the same time, read line from file 2
2. After both lines have been read, write file to file 3 and return it as a result of the \verb|let| expression

The important point of this rather simple example is to demonstrate how easy it is to write asynchronous concurrent code in Yatta.

```haskell
let
    (:ok, line1) = File::read_line f1
    (:ok, line2) = File::read_line f2
in
    File::write_line f3 (line1 ++ line2)
```

This allows programmers to focus on expressing concurrent programs much more easily and not having to deal with the details of the actual execution order. Additionally, when code must be executed sequentially, without explicit dependencies, a special expression `do` is available.

In terms of implementation, the runtime system of Yatta can be viewed in terms of promise pipelineing or call-streams. The difference is that this pipelining and promise abstraction as such is completely transparent to the programmer and exists solely on the runtime level.

## Evaluation
Evaluation of an Yatta program consists of evaluating a single expression. This is important, because everything, including module definitions are simple expressions in Yatta.

Module loader then takes advantage of this principle, knowing that an imported module will be a file defining a module expression. It can simply evaluate it and retrieve the module itself.

### Binary Operators
These are all the operations supposorted by Yatta's binary operators. Combinations of other data types than listed here will result in a `TypeError`.

| Operator | Left operand | Right operand | Description |
| -------- | ------------ | ------------- | ----------- |
| `&` | integer | integer | bitwise and |
| `&` | set | set | set intersection |
| `&` | dict | dict | dict intersection |
| `|` | integer | integer | bitwise or |
| `|` | set | set | set union |
| `|` | dict | dict | dict union |
| `^` | integer | integer | bitwise xor |
| `^` | set | set | set symetric difference |
| `^` | dict | dict | dict symetric difference |
| `--` | set | any | remove element from a set |
| `--` | dict | any | remove key from a dict |
| `/` | integer | integer | division |
| `/` | double | double | division |
| `==` | integer | integer | equality |
| `==` | double | double | equality |
| `==` | byte | byte | equality |
| `==` | function | function | referential equality |
| `==` | `()` | `()` | equality - alwyays `true` |
| `==` | tuple | tuple | equality |
| `==` | module | module | equality |
| `==` | sequence | sequence | equality |
| `==` | dict | dict | equality |
| `==` | set | set | equality |
| `==` | native | native | equality |
| `>` | integer | integer | greater than |
| `>` | double | double | greater than |
| `>` | byte | byte | greater than |
| `>` | function | function | always `false` |
| `>` | `()` | `()` | greater than - alwyays `false` |
| `>` | dict | dict | left is a strict superset of the right |
| `>` | set | set | left is a strict superset of the right |
| `>=` | integer | integer | greater than or equals |
| `>=` | double | double | greater than or equals |
| `>=` | byte | byte | greater than or equals |
| `>=` | function | function | referential equality |
| `>=` | `()` | `()` | always `true` |
| `>=` | dict | dict | left is a superset of the right |
| `>=` | set | set | left is a superset of the right |
| `!=` | integer | integer | non-equality |
| `!=` | double | double | non-equality |
| `!=` | byte | byte | non-equality |
| `!=` | function | function | referential non-equality |
| `!=` | `()` | `()` | non-equality - alwyays `false` |
| `!=` | tuple | tuple | non-equality |
| `!=` | module | module | non-equality |
| `!=` | sequence | sequence | non-equality |
| `!=` | dict | dict | non-equality |
| `!=` | set | set | non-equality |
| `!=` | native | native | non-equality |
| `<` | integer | integer | lower than |
| `<` | double | double | lower than |
| `<` | byte | byte | lower than |
| `<` | function | function | always `false` |
| `<` | `()` | `()` | lower than - alwyays `false` |
| `<` | dict | dict | left is a strict subset of the right |
| `<` | set | set | left is a strict subset of the right |
| `<=` | integer | integer | lower than or equals |
| `<=` | double | double | lower than or equals |
| `<=` | byte | byte | lower than or equals |
| `<=` | function | function | referential equality |
| `<=` | `()` | `()` | always `true` |
| `<=` | dict | dict | left is a subset of the right |
| `<=` | set | set | left is a subset of the right |
| `in` | any | set | left is a member of the right |
| `in` | any | dict | left is a member of the right |
| `in` | any | seq | left is a member of the right |
| `++` | seq | seq | concatenation |
| `<<` | integer | integer | signed left shift |
| `>>` | integer | integer | signed right shift |
| `>>>` | integer | integer | zero fill right shift |
| `&&` | boolean | boolean | logical and |
| `\|\|` | boolean | boolean | logical or |
| `-` | integer | integer | arithmetic subtraction |
| `-` | float | float | arithmetic subtraction |
| `-` | dict | any | TBD: review |
| `%` | integer | integer | arithmetic modulo |
| `%` | float | float | arithmetic modulo |
| `*` | integer | integer | arithmetic multiplication |
| `*` | float | float | arithmetic multiplication |
| `-` | integer | integer | arithmetic subtraction |
| `-` | float | float | arithmetic subtraction |
| `-` | set | any | remove element from the left |
| `-` | dict | tuple | remove element from the left |
| `+` | integer | integer | arithmetic addition |
| `+` | float | float | arithmetic addition |
| `+` | set | any | add element to the left |
| `+` | dict | tuple | add tuple `(key, value)` to the left |
| `-\|` | any | seq | add element to the beginning of a sequence |
| `\|-` | seq | any | add element to the end of a sequence |


## Syntax
Program in Yatta consists always of evaluation of a single expression. In fact, any Yatta program consist of exactly one expression.

Syntax is intentionally very minimalistic and inspired in languages such as SML or Haskell. There is only a handful of keywords, however, it is not as flexible in naming as Haskell for example.

Yatta programs have ambition to be easily readable and custom operators with names consisting of symbols alone are not that useful when reading programs for the first time. Therefore Yatta does not support custom operators named by symbols only.

Yatta does not have indentation specific parsing rules, it does require new line at certain locations, which are noted in each individual expression descriptions.

Comments are denoted by `#` character and everything that follows this character until the end of line `\n` or `\r\n` is considered a comment and ignored.

Source code of Yatta program must be a valid UTF-8 text file.

### **Functions**: definition and application
Functions in Yatta are defined in a very short and concise form. They make take arguments, which function can pattern match on, and one function can be defined using multiple such arguments. Function names must start with a lowercase letter, followed by any letters, numbers or underscores.

A simple function to calculate factorial can be written for example this way:

```haskell
factorial 1 = 1
factorial n = n * factorial (n - 1)
```

Each function case must be on a new line. A more complex conditions in patterns can be specified in this way:

```haskell
factorial 1 = 1
factorial n 
  | n > 1 = n * factorial (n - 1)
```
Which means that there is an additional condition for the `n` value to be greater than 0. There may be multiple guards for each pattern and they must be each be on a new line. Guard starts with a `|` character, follows an expression that must evaluate to a boolean and finally an `=` and the expression to evaluate if the pattern matches and the guard is `true`.

Note that function arguments may actually be full patterns, not just names of arguments. Patterns are described in the section named *Pattern Matching*.

Yatta additionally supports non-linear patterns, meaning that if a pattern contains the same name multiple times, than this name is required to match the same value so that the pattern would match. This can be handy when checking for one value to be present in multiple places/arguments without having to explicitly write a guard that would ensure the equality.

#### **Anonymous functions**: aka lambdas
Since functions are first-class citizens in Yatta, it is necessary to provide means of passing functions as arguments, and also to define them without giving them a name. Following syntax is used in this case:

```bash
\argument_one argument_two -> argument_one argument_two  # lambda function for summing its arguments
```

Lambda function with no arguments is simply: `\-> :something`.

#### **Function application**: calling functions
Calling function simply means writing a name of the function and then specifying its arguments. If fewer arguments are provided than function expects, this is considered a [curried](https://en.wikipedia.org/wiki/Currying) call, and the result of such function is a partially applied function, that can be called with remaining arguments at a later point.

So for example calling a `factorial` would look simply like this:

```haskell
factorial 5
```

Since Yatta is strictly evaluated language, meaning that arguments are evaluated before calling the function, as opposed to lazy language, where arguments are only evaluated when actually used by the called function, there is one situation to be careful about and that is passing lambda functions of zero arguments as arguments to functions. This would be evaluated before actually calling the function and if there are no side-effects happening in the lambda, it might be perfectly fine. However, if the lambda must be passed as a lambda, it needs to be wrapped into another lambda at the call-site, such as for example:

```bash
let
    lambda = \-> println :hello
in
    do_something_with \-> lambda  # instead of do_something_with lambda
```

Then `do_something_with` function will obtain its argument that is a function and not a result of `println :hello` function (that would be `:hello` btw).

#### Pipes and operator precedence
Since Yatta is an ML-style language, and unlike many C-like languages it does not use parentheses to denote a function application, it can become unclear which expressions are arguments of which function. Take for example the following example:

```haskell
Seq::take 5 Seq::random 10
```

Is this a function call to the `Seq::take` function (that is a function `take` in the module `Seq` as explained later) with 3 arguments (5, `Seq::random` and 10) or is it `Seq::random 10` supposed to be computed first and then its result used as a second argument to `Seq::take`?

If the latter, then it can be written this way:

```haskell
Seq::take 5 (Seq::random 10)
```

Since any expression can be put into parentheses and be given precendece in evaluation. Another way to write this would be using *pipes*:

```haskell
Seq::take 5 <| Seq::random 10
```

or

```haskell
Seq::random 10 |> Seq::take 5
```

These are all equivalent expression and it is up to the programmers preference to decide which one to use. One nice feature that pipes have is that they can be used on multiple lines, such as:

```haskell
Seq::random 10
|> Seq::take 5
|> println
```

### **`let` expression**: defining local aliases / pattern matching in the scope of evaluated expression
`let` expression allows defining aliases in the scope of the executed expressions. This expressions allows evaluating patterns as well, so it is possible to deconstruct a value from a sequence, tuple, set, dictionary or a record directly, for example:

```haskell
let
    (1, second) = (1, 2)
    pattern     = expression1
in
    expression2
```

As shown in this example, `let` expression consists of two parts. First is used for definition of aliases and patterns using and the second one which contains the expression that is evaluated with these aliases on the stack. The result of this `let` expression is the result of the `expression2` expression. The `let` expression allows defining patterns which are on the left side of the first section. If a pattern is not matched, the whole expression throws a `:nomatch` exception. One alias line can use names defined in previous lines.
Every alias/pattern must be defined on a new line.

Note that the order of execution of the alias/pattern lines is not strictly sequential. They may in fact be executed in any order (though it is guaranteed that names used previously will always be available).


### **`do` expression**: sequencing side effects
`do` expressions is used for definition of a sequence of side effecting expressions. This expression is pretty similar to the `let` expression in the sense that it allows defining aliases and patterns, however, it doesn't have a separate expression that would be used as a result of this expression. Instead the result of the last line is used as the result.

```haskell
do
    start_time  = Time::now
    (:ok, line) = File::read_line f
    end_time    = Time::now
    printf line
    printf (end_time - start_time)
end
```

Note that unlike with `let` expression, the order of executed expressions is guaranteed to be exactly the same as the order in which they are written. This is the main usecase of the `do` expression, to allow strict ordering of execution. It does not matter whether an expression returns a run-time level promise (such as one can be created by a IO call or by using the `async` function), the order is still maintained.

### **`case` expression**: pattern matching
`case` expression is used for pattern matching on an expression. Each line of this expression contains a pattern followed by an arrow `->` and an expression that is evaulated in case of a successful pattern match. Patterns are tried in the order in which they are specified. The default case can be denoted by an underscore `_` pattern that always evaluates as true.

```haskell
case File::read_line f of
    (:ok, line)       -> line
    (:ok, :eof)       -> :eof
    err@(:error, _)   -> err
    tuple   # guard expressions below
        | tuple_size tuple == 3 -> :ok
        | true                  -> :ok
    _                 -> (:error, :unknown)
end
```

Same as with function definition patterns, patterns in the `case` expression can contain guard expressions, as is the case in the *tuple* pattern in the example above. Pattern with a guard expression will match only a guard evaluated to `true` can be found. Guards are tried in the order they are specified in.

### **`if` expression**: conditions
`if` is a conditional expression that takes form:

```haskell
if expression
then
    expression
else
    expression
```

Both `then` and `else` parts must be defined. The expression after `if` must evaluate to a boolean value. Empty sequence, dictionary or similar will not work! New lines are allowed, but not required.

### **module expression**
`module` is an expression representing a set of records (optional) and functions. A module must export at least one function, others may be private - usable only from functions defined within the same module. Records are always visible only within the same module and may not be exported. A module may be defined as a file - then the file must take name of the module + `.yatta` suffix.

```haskell
module package\DemoMmodule 
    exports function1, function2
as

record DataStructure = (field_one, field_two)

function1 = :something

function2 = :something_else
```

Calling functions from a module is denoted by a double colon:
```
package\DemoModule::function1
````

Modules must have capitalized name, while packages are expected to start with a lowercase letter.

Module may also be defined dynamically, for example assigned to a name in a `let` expression, for example:

```haskell
let some_module = module TestModule exports test_function as
    test_function = 5
in
    some_module::test_function
```

In this case the name of the module does not matter, as the module is assigned to the `test_module` value.

#### Packages
Packages are logical units for organizing modules. Modules stored in packages must follow a folder structure exactly the same as the package path.

#### Records
New data structures in Yatta can be implemented as simply as by using tuples. However, as tuples grow in number of elements, it may become useful to name those elements rather than always matching on a particular n-th element. To do so, Yatta provides `records`.

Records are essentially named tuples with names for each element and can be used to refer to a particular element by that name. Records exist within the scope of a module and cannot be imported or exported to other modules. Modules are meant to provide interface via functions alone.

Record is defined by its name and a list of field names. An example of a record definition:

```haskell
record Car = (brand, model, engine_type)
```

To initialize a new record instance, following syntax should be used:

```haskell
let
    car = Car(brand="Audi", model="A4", engine_type="TDI")
in
    ...
```

Note that not all fields are required when initializing record. Unitialized fields are then initalized with value of `()` (unit). At least one field must be provided during initialization, though, its value may be unit as well.

Updating an existing record instance (actually creating a new one with some fields changed) can be performed using this syntax:

```bash
let
    new_car = car(model="A6")  # this is same as Car(brand="Audi", model="A4", engine_type="TDI")
in
    ...
```

To access a field from a record instance a dot syntax is used:

```bash
println new_car.model  # will print A6
```

Pattern matching on records is very easy as well:

```bash
order_car Car(brand="Audi", model=model) = order_audi_model model
order_car Car(brand="Lexus", model=model) = order_lexus_model model
order_car any_car@Car = order_elsewhere any_car  # the whole record_instance is available under name any_car
```

### **`import` expression**: importing functions from other modules
Normally, it is not necessary to import modules, as it is often the case in many other languages. Functions from another modules can be called without explicitly declaring them as imported. However, Yatta has a special `import` expression (and as such it returns the value of the expression followed the `in` keyword) that allows importing functions from modules and in that way create aliases for otherwise fully qualified names.

```bash
import
    funone as one_fun, otherfun from package\SimpleModule
    funtwo from other\package\NotSimpleModule
in
    onefun :something  # expression that is the return value of the whole import expression
```

Note that importing functions from multiple modules is possibly, they just have to be put on new lines. Functions can be renamed using the `as` keyword.

### **`raise`, `try`/`catch` expressions**: raising and catching exceptions
Yatta is not a pure language, therefore it allows raising exceptions. Exceptions in Yatta are represented as a tuple of a symbol and a message. Message can be empty, if not provided as an argument to the keyword/function `raise`.

Exceptions in Yatta consist of three components. Type of exception is of type of symbol. The second component is a string description of the exception - an error message. Last component is the stacktrace which is appended by the runtime automatically.

Raising an exception can be accomplished by the `raise` expression:

```bash
raise :badarg "Error message"  # where :badarg is a symbol denoting type of exception
```

Cachting exceptions is done via the `try`/`catch`. Catching an exception is essentially pattern matching on an exception triple that consists of all three exception components.

```
try
    expression
catch
    (:badarg, error_msg, stacktrace)  -> :error
    (:io_error, error_msg, stacktrace) -> :error
end
```

### **Loops**: recursion and generators
Yatta is a functional language with immutable data types and no variables. This means that imperative constructs for loops, such as `while` or `for` cannot be used.
Instead, iteration is normally achieved via recursion. Yatta is able to optimize tail-recursive function calls, so they would not stack overflow.

A typical Python solution using mutation might look like this:

```python
def factorial(n):
    i = n
    while i > 1:
        i -= 1
        n *= i
    return n
```

However, this solution requires mutable variables, which are not present in Yatta. So, an example of a recursive function to calculate factorial in Yatta would be:

```haskell
factorial 1 = 1
factorial n = n * factorial (n - 1)
```

Note that this function is actually not tail-recursive. This is because `factorial (n - 1)` is evaluated before `n * (factorial (n - 1))`, so the multiplication is the last expression here, and thus there is a potential for stack overflow. That might often not be an issue, just something to consider when writing recursive functions.

It would actually not be verydifficult to rewrite this function to be tail-recursive, such as:

```haskell
factTR 0 a = a
factTR n a = factTR (n - 1) (n * a)

factorial n = factTR n 1
```

In this case there is a helper function that is tail-recursive, since the last epxression in that function is the call to itself.

#### Generators
Another way to iterate over a collection (sequence, set or dictionary) are generator expressions. They allow transforming an existing collection into another one (of possibly different type, such as sequence to dictionary). Generator consists of three or four components:

The syntax for a generator generating a sequence from a set:
```haskell
[x * 2 | x <- {1, 2, 3}]  # the source collection is a set of 1, 2, 3, so the result is [2, 4, 6]
[x * 2 | x <- {1, 2, 3} if x % 2 == 0]  # generator with a condition, so the result is [4]
```

The syntax for a generator generating a set from a sequence:
```haskell
[x * 2 | x <- [1, 2, 3]]  # the source collection is a set of 1, 2, 3, so the result is {2, 4, 6}
[x * 2 | x <- [1, 2, 3] if x % 2 == 0]  # generator with a condition, so the result is {4}
```

The syntax for a generator generating a dictionary:
```haskell
{key = val * 2 | key = val <- {:a = 1, :b = 2, :c = 3}}  # the source collection is a set of 1, 2, 3, so the result is {:a = 2, :b = 4, :c = 6}
{key = val * 2 | key = val <- {:a = 1, :b = 2, :c = 3} if val % 2 == 0}  # generator with a condition, so the result is {:b = 4}
```

Generators are an easy an conveniet way to transform built-in collections. They are, however, themselves implemented using reusable [Transducers](language/lib-yatta/Transducers.yatta) module, for example a set generator without using the above mentioned syntax "sugar" could look like:

```haskell
Transducers::filter \val -> val < 0 (\-> 0, \acc val -> acc + val, \acc -> acc * 2)
|> Set::reduce [1, 2, 3]
```

The description of Transducer functions can be found in the module itself. Transducers may be combined in order to create more complex transformations. Also, custom collection may implement their version of a `reduce` function that accepts a transducer and reduces the collection as desired.

## Strings
Strings in Yatta are technically sequences of UTF-8 characters. Character in Yatta can be written between apostrophes. Working with strings is then no different than working with any other sequence. String literals can be multi-line as well. There is no special syntax for multi-line strings and a single pair of quotes is used to denote all string literals.

### String Interpolation
Yatta supports string interpolation for convenience of formatting strings. The syntax is as follows:

```
"this string contains an interpolated {variable}"
```

In this string the `{variable}` part is replaced with whatever contents of the `variable`.

It is also possible to use string interpolation with alignment option, which can be used for formatting tabular outputs:

```
"{column1,10}|{column2, 10}"
```

The alignment number will make the value align to the right and filled with spaces to the left, if positive. If the number is negative, the opposite applies and the text is aligned to the left with spaces on the right. The number can be either a literal value or any expression that return an integer.

## Pattern Matching
Pattern matching is the most important feature for control flow in Yatta. It allows simple, short way of specifying patterns for the built in types, specifically:

Pattern matching, in combination with recursion are the basis of the control flow in Yatta.

### Scalar values - integer, float, byte, character, boolean, symbol
Pattern matching for scalar values is super simple. The pattern looks is the value itself, for example:

```haskell
case expr of
    5 -> do_something
    6 -> do_something_else
end
```

### Tuples
Pattern matching on tuples:

```haskell
case expr of
    (:ok, value)      -> do_something value  # value contains the second element of the tuple
    (:error, message) -> println message
end
```

### Records
Pattern matching on records is described in the section about Records.

### Underscore pattern
The underscore pattern `_` will match any value.

### Sequence & reverse sequence, multiple head & tails & their combinations in patterns
Sequence is a biderctional structure and can be easily pattern matched from either left or right side. Yatta allows pattern matching on more than a single element as well:

#### Matching sequence on the beginning
```haskell
case [1] of
    1 -| [] -> 2
    []      -> 3
    _       -> 9
end
```
This code will result in `2`.

Yatta allows matching on more than just one element in the beginning:
```haskell
case [1, 2, 3, 4] of
    1 -| 2 -| []   -> 2
    1 -| 2 -| tail -> tail
    []             -> 3
    _              -> 9
end
```
Will produce `[3, 4]`.

#### Matching sequence on the end
```haskell
case [1] of
    [] |- 1 -> 2
    []      -> 3
    _       -> 9
end
```
This code will result in `2`.

#### Obtaining a remainer of a sequnce
```haskell
case [1, 2, 3] of
    1 -| []   -> 2
    1 -| tail -> tail
    []        -> 3
    _ -> 9
end
```
This code will result in `[2, 3]`. This could also be done from the other end of the sequence, using `|-` operator instead (and reversing head/tails sections).

#### Matching a sequence elements
```haskell
case arg of
    []             -> 3
    [1, second, 3] -> second
    _               -> 9
end
```
Will produce `2`.

### Matching strings
Since strings are sequences of characters, they can be pattern matched on as such. Alternatively string literals may be used as well, or any combination of the two. For example:

```haskell
case "hello there" of
    'h' -| 'e' -| _ |- 'r' |- 'e' -> 0
    _                             -> 1
end
```
Which will produce a `0`.

Another example:
```haskell
case ['a', 'b', 'c'] of
    h -| "bc" -> h
    _         -> 1
end
```
Which produces an `'a'`.

### Dictionary patterns
Dictionaries can be matched on both keys and values. Example:
```haskell
case {"a" = 1, "b" = 2} of
    {"b" = 3}           -> 3
    {"a" = 1, "b" = bb} -> bb
    _                   -> 9
end
```
This will result in `2`.

### "As" patterns
Sometimes it can be useful to name a collection (sequence, set or dictionary) or a record in a pattern for later use. For example when matching on a record type, but ignoring all the fields. This can be done using `@` syntax in Yatta:

```haskell
let mod = module TestMod exports testfun as
    
    record TestRecord=(argone, argtwo)
    
    testfun = case TestRecord(argone = 1, argtwo = 2) of
        2            -> 0
        x@TestRecord -> x.argone
        _            -> 2
    end
in mod::testfun
```
This slightly with an inlined module definition more complex example will produce `1`. Remember that records exist on a module level only.

### `let` expression patterns
`let` expression allows using patterns on the left side of its assignments. In that way it is not necessary to use `case` expression explicitly.

### * `do` expression patterns
`do` expression similarly to `let` expressions allows defining aliases, and they can be patterns on the left side. Unlike `let` expression, `do` expression does not require value assignment, but still allows it. For example:

```ruby
do
    (one, _) = (1, :unused)
    println one
    two = 2
    one + two
end
```
Which will result in `3` after `1` is printed.

### `case` patterns
This is THE pattern matching expression. It maches a value against a list of patterns and executes the expression of the matching pattern.

### Function & lambda patterns
Functions and lambdas may be defined using patterns as explained in the section about functions. The only limitation of the lambda definitions, is that they may only contain one pattern. This is not used much for control flow, but still useful for deconstructing some data structures. If lambda needs to pattern match multiple patterns, either define it as a named function, or use `case` expression.

### Guard expressions
Patterns may be enhanced further with guard expressions. Guard expressions are initiated by a `|` character followed by an expression that must evaluate into a boolean.
Each pattern may have multiple guard expressions, each on a new line.

For example, a function calculating body mass index could be defined like this:

```haskell
bmiTell bmi
    | bmi <= 18.5 = "You're underweight, you emo, you!"
    | bmi <= 25.0 = "You're supposedly normal. Pffft, I bet you're ugly!"
    | bmi <= 30.0 = "You're fat! Lose some weight, fatty!"
    | true        = "You're a whale, congratulations!"
```
As can be seen, guards allow checking additional conditions that can't be captured by pattern itself, such as checking whether a number is in a range.

### Non-linear patterns - ability to use same variable in pattern multiple times
Non-linear patterns are those that contain the same name in multiple places, for example:

```haskell
nonLinearHeadTailsTest head -| _    head -| _    = head
nonLinearHeadTailsTest headOne -| _ headTwo -| _ = headOne + headTwo
```
For example, calling this function:
```haskell
nonLinearHeadTailsTest [2, 0] [3, 0]
```
Will yield `5`, because the first element in first and second argument must be the same.
