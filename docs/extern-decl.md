# `extern` — Foreign Function Interface

Yona binds to C functions through module-level `extern` declarations. Each
declaration introduces a Yona name with a typed signature; the codegen emits
an LLVM extern function reference, and the linker resolves the symbol against
whatever `.o` / shared library is on the link line.

## Syntax

```
extern NAME : TYPE                      # bare form
extern NAME : TYPE = "C_SYMBOL"         # symbol-aliased form
extern async NAME : TYPE [= "C_SYMBOL"] # thread-pool async (returns Promise)
```

In the bare form, the Yona name and the C ABI symbol are the same string.
In the symbol-aliased form, `NAME` is what call sites in Yona use, and
`"C_SYMBOL"` is the literal symbol the linker looks up.

## When to use which form

### Bare form — `extern NAME : TYPE`

Pick this when the C function's name already follows Yona naming and there
is no reason to rename it. Typical case: thin libc / libm bindings.

```yona
module Std\Math
extern sqrt  : Float -> Float
extern sin   : Float -> Float
extern cos   : Float -> Float
extern floor : Float -> Float
```

The Yona-side `sqrt` is the same identifier the C compiler sees, and there
is nothing to hide. Adding `= "sqrt"` would be noise.

### Aliased form — `extern NAME : TYPE = "C_SYMBOL"`

Pick this whenever the C symbol you actually want to call shouldn't be the
identifier users type. The classic cases:

**1. Wrapping the Yona runtime.** The C runtime exports
mangled symbols like `YonaStdChannelChannel`. You don't want every
caller in `Std/Channel.yona` to repeat that string — alias once, then use
the clean local name everywhere:

```yona
module Std\Channel
extern raw_new  : Int -> Channel        = "YonaStdChannelChannel"
extern raw_send : Channel -> Int -> ()  = "YonaStdChannelSend"
extern raw_recv : Channel -> Option     = "YonaStdChannelRecv"

channel n = let r = raw_new n in (Linear (Sender r), Linear (Receiver r))
send s v  = case s of Sender raw -> raw_send raw v end
```

The wrapper functions get the natural names; the extern declarations
quarantine the C ABI mangling at the top of the file.

**2. The extern *is* the public API.** When the wrapper would be a pure
trampoline like `compile pattern = YonaStdRegexCompile pattern`, just give
the extern the public name directly. There's nothing useful in the
trampoline:

```yona
module Std\Regex
export compile, matches, find
extern compile : String -> Int          = "YonaStdRegexCompile"
extern matches : Int -> String -> Bool  = "YonaStdRegexMatches"
extern find    : Int -> String -> Seq   = "YonaStdRegexFind"
```

This collapses two declarations per function (extern + wrapper) into one.

**3. Renaming for ergonomics.** C's snake_case → Yona's camelCase, or
shortening verbose APIs:

```yona
extern getEnv : String -> String  = "getenv"
extern strlen : String -> Int     = "strnlen_s"
```

**4. Versioned, decorated, or platform-specific symbols.** When the
linker sees `fopen64`, `xmlParseFile@@LIBXML2_2.6.0`, or a C++
mangled name:

```yona
extern openFile : String -> String -> Int = "fopen64"
```

### Async form — `extern async NAME : TYPE`

Adding `async` marks the call as thread-pool async: the codegen wraps
the call in `YonaRuntimeAsyncCall` and the result is a `Promise`, transparent
to callers via auto-await. Combinable with the alias form.

```yona
extern async slowComputation : Int -> Int = "my_lib_slow_compute"
```

## Design rationale

**Why a two-form syntax instead of always-aliased?** A surprising amount
of FFI is to libraries whose names already match Yona conventions
(`sqrt`, `sin`, `printf`, `getpid`). Forcing `= "sqrt"` everywhere would
be pure noise. The bare form is the common case; the alias form is the
escape hatch.

**Why not auto-mangle to the module's namespace?** A naïve
`extern channel : ...` could expand to `YonaStdChannelChannel`
behind the scenes, which would save keystrokes when wrapping the Yona
runtime. The cost: the source language gets coupled to a specific C ABI
mangling scheme, the rule has to be explained, and call sites that don't
follow the convention (e.g. binding to libc) need a different syntax
anyway. Explicit `= "C_SYMBOL"` is half a line longer and zero magic.

**Why string-quoted symbols rather than identifiers?** C symbols include
characters Yona identifiers can't represent: `@`, `$`, `.`, version tags,
C++ mangling. A string is the only form that handles all of them. OCaml
(`external name : type = "c_name"`) and Haskell
(`foreign import ccall "c_name" name :: Type`) take the same approach.

**Linker resolution.** The C symbol is looked up by the linker at link
time. If you mistype the symbol, you get a `linking failed` error
pointing at the bad reference. The Yona-side name is checked at parse
time, so typos in the local name fail fast.

**No header inclusion / typedefs.** Yona's `extern` just declares a
signature — it never reads C headers, never tries to follow `#include`
chains, and never invokes the C compiler. The price is that you have
to spell out the Yona-side type explicitly. The benefit is that the
toolchain stays simple and the type you write in Yona is the type the
codegen emits, with no surprises.
