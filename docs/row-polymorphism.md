# Row Polymorphism for Records

**Status (2026-08-18):** **implemented** for **record** field rows
(`MRecord` / `row_rest`). This document is **not** the effect-row spec
(GitHub [#8](https://github.com/yona-lang/yona/issues/79)).
Evidence: [type-system-status.md](type-system-status.md) §3.

## Overview

Yona supports anonymous structural records with row-polymorphic types. Records are created with `{ field = value, ... }` syntax and fields are accessed with dot notation.

```yona
let person = { name = "Alice", age = 30 } in
person.age   -- 30
```

## Syntax

### Record Literals

```yona
{ name = "Alice", age = 30 }
{ x = 10, y = 20, z = 30 }
{ value = 42 }
```

Fields are automatically sorted by name for a deterministic layout.

### Field Access

```yona
let r = { x = 10, y = 20 } in
r.x    -- 10
r.y    -- 20
```

## Runtime Representation

Records are compiled as **tuples** with a compile-time field map. There is no runtime overhead for field access — it's a single indexed memory load, identical to tuple element access.

```
{ name = "Alice", age = 30 }

-- Fields sorted: age (index 0), name (index 1)
-- Compiles to: tuple_alloc(2); tuple_set(t, 0, 30); tuple_set(t, 1, "Alice")

r.age   -- compiles to: tuple_get(r, 0)  -- direct GEP + load
r.name  -- compiles to: tuple_get(r, 1)
```

This means record field access has **zero overhead** compared to tuple indexing.

## Type System

### Record Types

The type checker infers record types as `{ field1 : Type1, field2 : Type2 }`:

```yona
let r = { x = 42, y = true } in r
-- Inferred type: { x : Int, y : Bool }
```

### Row Variables

Field access constrains a record to have at least the accessed field, via a row variable:

```yona
r.name
-- Constrains r to: { name : t | r' }
-- where t is the field type and r' captures "the rest of the fields"
```

### Row Unification

When two record types are unified:
1. Common fields: their types are unified pairwise
2. Extra fields on one side: absorbed by the other side's row variable
3. Both have row variables: unified to capture the combined remainder

This enables functions that work on any record with certain fields:

```yona
-- Works with any record that has an 'x' field
getX r = r.x

getX { x = 42 }                    -- 42
getX { x = 10, y = 20 }            -- 10
getX { x = 1, y = 2, z = 3 }      -- 1
```

## Records vs ADTs

Records and ADTs are distinct:

| Feature | Records | ADTs |
|---------|---------|------|
| Typing | Structural (anonymous) | Nominal (named constructor) |
| Creation | `{ field = val }` | `Constructor val` |
| Access | `r.field` (GEP by compile-time index) | `adt.field` (by constructor field registry) |
| Polymorphism | Row polymorphism (open fields) | Type parameter polymorphism |
| Runtime | Tuple (flat array) | Struct or heap-allocated |

Both support field access with dot notation, but they use different codegen paths.

## Limitations

- **No record update syntax yet**: `{ r | field = newval }` is planned but not implemented
- **No record pattern matching yet**: destructuring in case expressions is planned
- **String literal fields**: string literal values in records with mixed types may require care (constant vs heap-allocated strings)
- **Cross-module row polymorphism**: requires GENFN monomorphization (same as generic functions)
