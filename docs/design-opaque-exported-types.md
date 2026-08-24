# Opaque exported types

**Status:** implemented design for GitHub #6.

## Syntax

```yona
module Data\Token

export type Token opaque
export make, value

type Token = Token Int
make n = Token n
value (Token n) = n
```

`export type T` remains transparent and exports `T` with its constructors.
`export type T opaque` exports only the nominal type name. The declaration must
refer to an ADT declared in the same module; duplicate or contradictory exports
are a parse error.

## Visibility

Inside the defining module, constructors remain available for construction and
pattern matching. An importer may pass, store, return, and annotate `T`, but
cannot construct or match its hidden constructors. Smart constructors and
observers are ordinary exported functions.

An opaque type may appear in public function signatures and trait instances.
The trait declaration or methods themselves are unaffected; clients can use a
public trait operation only when its normal constraints are satisfied.

## Interfaces and compatibility

`.yonai` writes `ADT T ... opaque` and omits the following `CTOR` records for
that type. Older interfaces lacking `opaque` retain transparent behavior.
Importers therefore cannot register hidden constructor names in their parser,
type checker, or code generator. Changing a transparent export to opaque is a
source-breaking API change for clients that construct or pattern-match it.

## Non-goals

This feature does not add existential types, representation-polymorphic
optimization barriers, private functions, or visibility modifiers for record
fields. Runtime values keep their existing ADT representation.
