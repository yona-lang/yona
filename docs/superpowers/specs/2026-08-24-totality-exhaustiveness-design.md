# Effect-freedom Exhaustiveness Gate

## Goal

Extend `yonac --require-effect-free` so its accepted programs have both a
closed empty effect row and exhaustive finite-ADT `case` expressions. This is
a small totality slice; it does not attempt termination, overlap, or
non-ADT-pattern analysis.

## Scope

The existing `--Wincomplete-patterns` diagnostic remains the general warning
mode (and remains enabled by `--Wall`). In strict effect-freedom mode, the
same finite-ADT coverage analysis is mandatory and emits E0203 for every
incomplete case. Wildcards count as covering every constructor; guarded arms
do not establish coverage. Cases over primitives, sequences, records, and
open/unknown ADTs remain outside this slice.

## Architecture

Extract the finite-ADT coverage calculation from code generation into a small
shared analysis that reports the scrutinee ADT and its missing constructors.
`CodegenCase` consumes it to preserve the existing warning. The strict gate
walks the parsed module or expression before code generation and turns each
reported incomplete finite-ADT case into E0203. The analysis is syntactic
except for constructor metadata already registered in `Codegen`.

## Errors and default behavior

The strict diagnostic explains that `--require-effect-free` also requires a
finite-ADT match to cover all constructors and lists the missing names.
Programs not passing `--require-effect-free` retain current warning behavior;
there is no new default error and no change to `.yonai` format.

## Tests and documentation

Add expression and module CLI tests for strict rejection, strict success via
wildcard/complete constructors, and guarded-arm rejection. Keep direct
coverage unit tests for the warning path. Update the CLI reference, E0203
documentation, type-system status, roadmap, changelog, and matching static
site pages. The todo list moves finished #8/#10/#11 details out of the active
priority section while retaining #5 and its termination follow-up.
