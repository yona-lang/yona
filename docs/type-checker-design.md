# Type Checker Design

**Status (2026-08-18):** HM + ADTs + traits + **record** rows are in
`MonoType`. Algebraic `perform`/`handle` typechecking exists; **effect rows
are design-only**. Linear/refinement/`@borrow` live in separate checkers or
codegen — see [type-system-status.md](type-system-status.md). Phase 7
`[done]` below means handler-scope warnings, not rows or `effect` decls.

## Overview

Yona's type checker implements Hindley-Milner type inference with extensions
for ADTs, traits, effects, and sum types. It runs as a separate pass between
parsing and codegen:

```
Source → Parser → AST → TypeChecker → TypeMap → Codegen → LLVM IR
```

## Architecture

### Core Components

| Component | File | Purpose |
|-----------|------|---------|
| `MonoType` | `InferType.h` | Type representation with variables, constructors, arrows |
| `TypeArena` | `TypeArena.h` | Arena allocator for types (no shared_ptr overhead) |
| `UnionFind` | `UnionFind.h` | Union-find with path compression for unification |
| `Unifier` | `Unification.h` | Unification algorithm with occurs check |
| `TypeEnv` | `TypeEnv.h` | Lexical scope chain for type bindings |
| `TypeChecker` | `TypeChecker.h` | Main inference pass, produces type map |

### Type Representation

```
MonoType variants:
  Var(id, level)          -- unification variable
  Con(Int|Float|Bool|...) -- built-in type
  App("Option", [a])     -- type application (ADTs, Seq, Set, Dict)
  Arrow(param, ret)       -- function type
  MTuple([types...])      -- product type
```

### Unification Algorithm

Standard HM unification with levels for efficient generalization:
1. Chase union-find links to find representatives
2. If both are same variable -> success
3. If one is variable -> occurs check, level adjustment, bind
4. If both are constructors -> check match, unify args pairwise

### Let-Polymorphism

```yona
let id = \x -> x in
(id 1, id "hello")  -- id is polymorphic: forall a. a -> a
```

1. Infer `\x -> x` at level+1 -> `a -> a`
2. Generalize: variables at level > current become quantified -> `forall a. a -> a`
3. Each use of `id` instantiates with fresh variables
4. `id 1` -> `Int -> Int`, `id "hello"` -> `String -> String`

### Effect Type Tracking

The type checker tracks algebraic effects via:
- **Effect registry**: `register_effect()` stores operation signatures (param types, return type)
- **Handler scope stack**: `handle...with` blocks push handled operations onto a stack
- **Perform checking**: `infer_perform()` verifies argument types against the operation signature and returns the declared return type
- **Handler inference**: `infer_handle()` infers handler clause bodies, binding operation arguments and `resume` with correct types
- **Unhandled effect warning**: `perform` outside any matching `handle` triggers a `-Wunhandled-effect` warning

```yona
effect State s
    get : () -> s
    put : s -> ()
end

-- Type checker verifies: get returns s, put takes s
-- resume in handlers has type: return_type -> result_type
handle
    let x = perform State.get () in   -- x : s
    perform State.put (x + 1)          -- checked: x + 1 must match s
with
    State.get () resume -> resume 0    -- resume : s -> result
    State.put s resume -> resume ()    -- s : s, resume : () -> result
    return val -> val
end
```

### Error Messages

Every error includes:
- Source location (line, column) with source context display
- Expected vs actual type
- Context string ("in function application", "in if condition", etc.)
- "Did you mean?" suggestions for undefined variables (via edit distance)

```
test.yona:15:10: error: type mismatch: expected Int but found String in operator +
   15 | 1 + "hello"
      |     ^~~~~~~

test.yona:22:5: error: undefined variable 'prnt'; did you mean 'print'?
   22 | prnt "hello"
      | ^~~~

test.yona:30:5: error: no instance for 'Num String'

test.yona:8:5: warning: effect operation 'State.get' may not be handled [-Wunhandled-effect]
```

### ADT Type Inference

ADT constructors are registered as polymorphic functions:
- `None : forall a. Option a` (arity 0)
- `Some : forall a. a -> Option a` (arity 1)
- Constructor patterns bind type variables and unify with the scrutinee type

### Trait Constraints

Trait methods have deferred constraints that are solved after inference:
- `abs : forall a. Num a => a -> a`
- Each use records a constraint `(Num, resolved_type)`
- `solve_constraints()` checks that concrete types have registered instances

## Implementation Phases

1. **Foundation**: TypeArena, UnionFind, MonoType, basic Unification [done]
2. **Environment**: TypeEnv, builtin types/operators registration [done]
3. **Core Inference**: literals, identifiers, let, functions, apply, if [done]
4. **Collections**: tuples, seqs, patterns (head-tail, tuple, constructor, or-pattern) [done]
5. **ADTs**: constructor types, constructor patterns, polymorphic schemes [done]
6. **Traits**: constrained polymorphism, deferred constraints, instance resolution [done]
7. **Effects**: perform/handle type checking, handler scope tracking, unhandled warnings [done]
8. **Codegen Integration**: wired into cli/main.cpp pipeline [done]
9. **Error Polish**: "did you mean?" suggestions, context strings, source display [done]
