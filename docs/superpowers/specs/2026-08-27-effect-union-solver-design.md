# Effect-union solver design

## Decision

Replace the type checker's one-tail effect rows with a dedicated effect-constraint
solver. This is a soundness migration, not a compatibility workaround: a helper
which invokes callbacks with independent rows `rho` and `sigma` has effect
`rho ∪ sigma`, which cannot be represented as labels plus one tail.

The migration keeps value-type unification and effect solving separate. An arrow
references an `EffectRef`; effect expressions form a normalized, deterministic
join/mask DAG. Equality between arrow annotations is an effect-solver equality
constraint. Applying a callee adds an inclusion edge to the ambient effect
summary; it never equates independent callback rows.

## Model

`EffectSolver` owns a distinct `EffectVarId` namespace and the following nodes:

- `FlexibleMeta`: an ordinary, generalizable effect variable that may be bound
  by a true row-equality constraint.
- `DerivedLeast`: a function-body or recursive-SCC summary. It is the least
  fixed point of local labels and inclusion edges.
- `RigidOpaque`: an imported legacy/missing open row. It remains open and can
  neither be closed nor rebound by local inference.
- `Join`: the associative, commutative, idempotent union of effects.
- `Mask`: a symbolic handler subtraction edge. It removes handled labels from
  a source summary after instantiation rather than only labels known eagerly.

The normal query is:

```text
EffectNormalForm {
  known_labels: sorted, deduplicated operations;
  tails: sorted, unique (effect-variable projection, excluded-label mask);
  is_open: tails is nonempty or a reachable opaque source exists;
}
```

Every consumer—typed core, E0202, strict effect-freedom, pretty printing, and
interface emission—uses this query. There is no first-rest fallback.

## Invariants

1. Every unhandled application adds an inclusion edge to its ambient function
   summary. No source may be discarded because another row was seen first.
2. Aggregation never equates callback effect variables. Only a function-type
   equality constraint can do that.
3. Only `DerivedLeast` cells are defaulted to their least solution. A pure
   recursive SCC therefore closes; flexible and opaque inputs do not.
4. Generalization freezes every arrow root as one effect graph template, and
   instantiation clones those roots together with fresh effect IDs. Separate
   helper applications cannot share mutable effect state.
5. A handler is a symbolic mask. It can eliminate a callback effect that is
   unknown at helper definition time but becomes known at call time.
6. Imports preserve the complete normalized effect scheme. Legacy open
   metadata is a `RigidOpaque` leaf; new interfaces serialize every structural
   arrow path plus stable shared flexible/opaque projections, masks, labels,
   and closed rows. Raw solver node IDs are never part of the file format.

## Compatibility

Existing closed `.yonai` rows (`effects -` and known labels) map directly to
closed graph terms. Existing open/legacy rows map conservatively to
`RigidOpaque`. New interfaces use a versioned deterministic effect-scheme
encoding; old readers retain a conservative open summary rather than accepting
an unknown effect-free export.

Typed-core ABI v1 continues to print the normalized summary. A later ABI may
expose the symbolic graph, but no existing typed-core consumer is broken.

## Required evidence

- A two-callback helper exposes both `State.get` and `Log.log`; reordering
  declarations, calls, or tuple elements does not change the summary.
- Independent callback rows stay polymorphic across sibling instantiations.
- Pure direct/mutual recursive SCCs close; callback-first and recursive-first
  recursive HOFs remain open in strict mode.
- Imports, aliases, partial applications, handlers, nested handlers, typed
  core, E0202, `.yonai` round trips, and `--require-effect-free` agree.
- Legacy interfaces remain conservative and all normal type/CLI/codegen tests
  continue to pass.
