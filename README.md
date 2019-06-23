# Yatta

[![Build Status](https://travis-ci.org/yatta-lang/yatta.svg?branch=master)](https://travis-ci.org/yatta-lang/yatta)
[![Gitter](https://badges.gitter.im/yattalang/community.svg)](https://gitter.im/yattalang/community?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge)

## Introduction
Yatta is a minimalistic, opiniated, (strongly) dynamically typed, strict, functional programming language, with ML-like syntax, for GraalVM polyglot virtual machine (VM).

Its main purpose is to explore the potential of a language that would combine some of the most useful features of functional programming, such as immutable data structures, powerful pattern matching, simple built-in concurrency in a coherent, easy-to-read language for Java Virtual Machine (JVM).

Yatta does not insist on purity and values readability and ease of use above theoretical guarantees of pure functional languages.

Strictly speaking, Yatta does not implement any existing formal model precisely, and it forces its concurrency model built into language itself. This is a trade-off that on one hand eliminates possibility of full user control of the underlying threading implementation, on the other hand it allows for writing boiler-plate/dependency free concurrent code with clear syntax and semantics. This approach also eliminates chaotic situations in mainstream programming languages, where concurrency was added later and/or in form of mutually incompatible approaches and libraries.

Yatta aims to solve some of the shortcomings, both practical and theoretical, of existing functional JVM languages, while exploring benefits of implementation of a functional programming language on GraalVM via Truffle framework.

* **Clojure** - doesn't provide an implicit non-blocking IO, nor built-in pattern matching
* **Scala** - very complicated type system due to object oriented paradigm (OOP) combined with functional programming, notoriously slow compilation times, lack of support for built-in language level asynchronous IO / concurrency
* **Eta / Haskell** - pure, lazy evaluation and related memory leaks, monads for side-effects
* **Erlang** - although not a JVM language, it is a language with a built in concurrency model in form of actor system. From Yatta's point of view, actor model is still a low-level approach that requires significant control of the user to model concurrent execution

## Motivation
Yatta language has two main reasons to exist. First is to provide a real-world functional programming language that is very easy to use and is not a Lisp language. Lisp languages are not only notoriously famous for the parentheses-overload syntax, but mainly for their abilities in the meta-programming field. Meta-programming, while useful at times, can be an enemy to ability to understand other people's code.
Yatta aims to be very easily readable, written, no matter by whom.

Secondly, it is to abstract users from dealing with non-blocking asynchronous computations and parallelism. While these features are commonly available in other languages nowadays, they are almost exclusively non-native solutions that come in forms of libraries or frameworks and are difficult to integrate with existing codebases. On top of that, dealing with these additional libraries requires conscious effort of the programmer to choose/learn/integrate these libraries into their mindset when writing new code.

GraalVM conveniently provided a relatively easy way to implement a new, high level, programming language without the hassle of low-level native code/bytecode generation. Truffle framework allows writing Yatta interpreter in the Java language as easily as writing an AST interpreter.
Furthermore, it provides tools for optimizing the interpreter performance based on runtime profile.

Yatta language has a well-defined list of priorities:

* good readability - simple syntax, few keywords, virtually no boilerplate
* single expression principle - program is always one expression - this enables simpler evaluation and syntax, allows writing simple scripts as well as complex applications
* few types of expressions - `module`, function(function does not need a keyword, it is defined by a name and arguments (patterns)), `case`, `if`, `let`, `do` and `try`/`catch` + `raise`
* simple module system - ability to expose functions for use in other modules, and ability to import them from other modules. Modules are first level values and can be created dynamically
* powerful and efficient built-in data structures with full support for pattern matching
* built-in runtime level non-blocking asynchronous IO
* simple runtime level concurrency, no additional types or data structures necessary
* polyglot language - interoperability with other languages via GraalVM

The motivation and priorities for Yatta language design yield a language that is different than existing functional languages, in both features and level of abstraction it provides, specifically abstraction related to asynchronous and parallel computations.

## Expressions
Program in Yatta consists always of evaluation of a single expression. In fact, any Yatta program consist of exactly one expression. Note that syntax in few cases is not final and a subject of an active development.

### Basics
Values in Yatta are represented using following syntax:

* string - in quotes: `"hello world"`
* tuple - in parenthesis: `(1, 2, 3)`
* sequence - in brackets: `[1, 2, 3]`
* symbols - preceded by a colon: `:ok`
* dictionary - in curly braces: `{:one = 1, :two = 2}`
* anonymous function(lambda): `\first second -> first + second`
* function application: function name and arguments separated by spaces: `function arg_one arg_two`
* none: `()`

### Definition of aliases in the executed expression
`let` expression allows defining aliases in the executed expressions. This expressions allows evaluating patterns as well, so it is possible to deconstruct a value from a sequence, tuple, dictionary directly, for example:

```haskell
let
    (1, second) = (1, 2)
    pattern     = expression
in
    expression
```

### Sequence of side effects
`do` expressions is used for definition of a sequence of side effecting expressions.

```haskell
do
    start_time  = Time\now
    (:ok, line) = File\read_line f
    end_time    = Time\now
    printf line
    printf (end_time - start_time)
end
```

### Pattern matching expression
`case` expression is used for pattern matching on an expression.

```haskell
case File\read_line f of
    (:ok, line)       -> line
    (:ok, :eof)       -> :eof
    err@(:error, _)   -> err
    tuple   # guard expressions below
        | tuple_size tuple == 3 -> :ok
        | true                  -> :ok
    _                 -> (:error, :unknown)
end
```

### Conditional expression
`if` is a conditional expression that takes form:

```haskell
if expression
then 
    expression
else
    expression
```

Both `then` and `else` parts must be defined.

### Module
`module` is an expression representing a set of functions. Modules must have capital name, while packages are expected to start with a lowercase letter.

```haskell
module package\DemoMmodule 
    exports function1, function2
of
function1 = :something
function2 = :something_else
```

### Import expression
Normally, it is not necessary to import modules, like in many other languages. Functions from another modules can be called without explicitly declaring them as imported. However, Yatta has a special `import` expression that allows importing functions from modules and in that way create aliases for otherwise fully qualified names.

```haskell
import
    funone as one_fun 
        from package/SimpleModule
in onefun :something
```

### Exception raising & catching expression (ticket [#4](/../../issues/4))
`raise` is an expression for raising exceptions:

```haskell
raise :badarg

# alternatively with a message

raise :badarg "Message string"
```

`try`/`catch` is an expression for catching exceptions:

```haskell
try
    expression
catch
    (:badarg, error_msg, stacktrace)  -> :error
    (:io_error, error_msg, stacktrace) -> :error
end
```

## Pattern Matching and built-in Data Structures
Yatta has a rich set of built-in types, in addition to ability to define custom data types, known as records. Standard types include:

* integer - signed 64 bit number
* float - signed 64 bit floating point number
* big integer - arbitrary-precision integers (ticket [#12](/../../issues/12))
* big decimal - arbitrary-precision signed decimal numbers (ticket [#12](/../../issues/12))
* byte
* symbol (ticket [#12](/../../issues/12))
* char - UTF-8 code point
* string - UTF-8 strings
* tuple
* sequence - constant time access to both front and rear of the sequence
* dictionary - key-value mapping
* none - no value

Records are implemented using tuples and are a local to modules. Their syntax is not defined yet, however, they conceptually allow accessing tuple elements by name, rather than index.

Pattern matching is the most important feature for control flow in Yatta. It allows simple, short way of specifying patterns for the built in types, specifically:

* Simple types - numbers, booleans, symbols
* Tuples & records
* Sequence & reverse sequence, multiple head & tails & their combinations in patterns
* Dictionaries
* `let` expression patterns
* `case` patterns
* Function & lambda patterns
* Guard expressions
* Non-linear patterns - ability to use same variable in pattern multiple times
* Strings and regular expressions
* Underscore pattern - matches any value

Pattern matching, in combination with recursion are the basis of the control flow in Yatta. Yatta supports tail-call optimization, to avoid stack overflow for tail recursive functions.

## Asynchronous non-blocking IO & Concurrency
Yatta provides fully transparent runtime system that integrates asynchronous non-blocking IO features with concurrent execution of the code. This means, there is no special syntax or special data types representing asynchronous computations. Everything related to non-blocking IO is hidden within the runtime and exposed via the standard library, and all expressions consisting of asynchronous expressions(Asynchronous expression is usually obtained from the standard library or created by function `async`) are evaluated in asynchronous, non-blocking matter.

Alternative for building asynchronous operations directly into language itself would be development of such library for existing programming language. Unfortunately, such approach has several shortcomings, mainly that such library would have to be adopted by other libraries/frameworks in order to be usable and it would still impose additional boilerplate simply because libraries cannot typically change language syntax/semantics. This is why Yatta provides these features from day one, built into language syntax and semantics and therefore it is always available to any program without any external dependencies. At the same time, putting these features directly on the language/runtime level allows for additional optimizations that could otherwise be tricky or impossible.

The example below shows a simple program that reads line from two different files and writes a combined line to the third line. The execution order is as follows:

1. Read line from file 1, at the same time, read line from file 2
2. After both lines have been read, write file to file 3 and return it as a result of the \verb|let| expression

The important point of this rather simple example is to demonstrate how easy it is to write asynchronous concurrent code in Yatta.

```haskell
let
    (:ok, line1) = File\read_line f1
    (:ok, line2) = File\read_line f2
in
    File\write_line f3 (line1 ++ line2)
```

This allows programmers to focus on expressing concurrent programs much more easily and not having to deal with the details of the actual execution order. Additionally, when code must be executed sequentially, without explicit dependencies, a special expression `do` is available.

In terms of implementation, the runtime system of Yatta can be viewed in terms of promise pipelineing or call-streams. The difference is that this pipelining and promise abstraction as such is completely transparent to the programmer and exists solely on the runtime level.

In terms of parallelization of non IO related code, Yatta will provide several standard library features, which will turn normal functions into runtime-level promises.

## Evaluation
Evaluation of an Yatta program consists of evaluating a single expression. This is important, because everything, including module definitions are simple expressions in Yatta.

Module loader then takes advantage of this principle, knowing that an imported module will be a file defining a module expression. It can simply evaluate it and retrieve the module itself.

## Syntax
Syntax is intentionally very minimalistic and inspired in languages such as SML or Haskell. There is only a handful of keywords, however, it is not as flexible in naming as Haskell for example.

Yatta programs have ambition to be easily readable and custom operators with names consisting of symbols alone are not that useful when reading programs for the first time. Therefore Yatta does not support custom operators named by symbols only.

## Error handling
Yatta is not a pure language, therefore it allows raising exceptions. Exceptions in Yatta are represented as a tuple of a symbol and a message. Message can be empty, if not provided as an argument to the keyword/function `raise`.

Yatta, as it is running on GraalVM platform needs to support catching underlying JVM exceptions. These exceptions can be caught by fully qualified name of the Java exception class.

Furthermore, Yatta will provide standard functions to extract message from the JVM exceptions, as well as a stacktrace from any exceptions.

Catching exceptions is exactly the same for underlying asynchronous code, with no additional syntax or semantics required. Yatta runtime makes sure exceptions are caught regardless of whether the function being executed is an IO/CPU runtime promise or a basic function.

This makes it easy to write asynchronous and non-blocking code with proper error handling, because to the programmer code always appears exactly the same, as if it were blocking, synchronous code in mainstream languages.

Previous example extended by error handling:

```haskell
try
    let
        (:ok, line1) = File\read_line f1
        (:ok, line2) = File\read_line f2
    in
        File\write_line f3 (line1 ++ line2)
catch
    (:match_error, _)  -> :error
    (:io_error, _)     -> :error
end
```

This example is just for demonstration of handling errors when using asynchronous IO code, standard library, including the `File` module is not defined yet.


## Installation Instructions
It is possible to run Yatta locally, whether for play purposes or development of new features.

### Build requirements - Debian
    sudo apt install build-essential zlib1g zlib1g-dev 

### Getting GraalVM
    export JAVA_HOME=$HOME/jdk PATH=$JAVA_HOME/bin:$PATH
    wget -O $HOME/jdk.tar.gz https://github.com/oracle/graal/releases/download/vm-19.0.0/graalvm-ce-linux-amd64-19.0.0.tar.gz
    mkdir $HOME/jdk && tar -xzf $HOME/jdk.tar.gz -C $HOME/jdk --strip-components=1
    gu install native-image

### Running Yatta
After cloning the repository, Yatta interpreter can be run simply by:
    ./yatta <filename.yatta>

Note: for simple testing, Yatta can run without file argument, then it will read any input provided to it. It can be ended by Ctrl-D (EOF).
