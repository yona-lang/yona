# Typed IR Codegen Rewrite Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the direct AST-to-LLVM backend with one verified Typed
IR-first compiler and atomically cut over every compiler, runtime, interface,
and tool consumer after full behavioral and platform parity.

**Architecture:** Build the complete replacement behind test-only entry points
while the legacy backend remains frozen as a behavioral oracle. Structural
types live in `yona_model`; canonical SSA Typed IR owns functions, callables,
control outcomes, and ownership facts; bounded LLVM lowering contexts consume
only fully verified IR. First-class calls, effects, continuations, and async
work share one descriptor-driven universal ABI, while statically known calls
retain typed native signatures. The final production change switches the
whole pipeline at once and deletes every legacy ABI and fallback.

**Tech Stack:** C++23, C11 runtime ABI, LLVM 22+, CMake/Ninja, doctest,
ASan/LSan/UBSan, Fedora 44 ARM64 QEMU, Python contract tests, `.yonai` v2.

## Global Constraints

- No generated function or module may mix legacy and replacement ownership,
  callable, control-outcome, exception, effect, async, or runtime conventions.
- Keep the replacement inaccessible from `yonac`, `yona`, and `yona-repl`
  except through test helpers until the atomic cutover task.
- Do not add a legacy adapter, fallback parser, source-reparse path, or silent
  fallback from Typed IR lowering to AST-to-LLVM lowering.
- Put immutable structural types in `yona_model`; `yona_interface` must not
  include or link `yona_typed_ir`.
- Use strong IDs: `model::TypeId`, `model::EffectRowId`, and Typed IR-local
  `FunctionId`, `BlockId`, `ValueId`, `CallableDescriptorId`, and
  `CleanupRegionId` (including dedicated continuation segment/transition IDs).
- `OwnershipKind` is exactly `Trivial`, `Borrowed`, or `Owned`; transfer is an
  operation/data-flow event, never a fourth stable value state.
- Keep reference counting as the cutover baseline. Arena placement remains
  disabled until the final ownership and escape verifiers pass.
- Preserve typed direct ABIs for statically known monomorphic calls. Use the
  universal descriptor ABI only at first-class/dynamic boundaries.
- Return nonlocal control through an out-parameter `YonaControlOutcome`; do
  not rely on platform C struct-return conventions or generated-program SJLJ.
- Every task starts with a failing focused test, keeps the frozen oracle green,
  updates its affected documentation in the same commit, and ends with
  `git diff --check`.
- The authoritative final pass order is: semantic AST lowering -> local/
  imported generic preparation and specialization -> pattern canonicalization
  -> structured-concurrency planning -> generator lowering -> decision-tree/
  control-flow lowering -> async preparation -> cleanup preparation ->
  effect/continuation preparation -> closure conversion -> closed operation
  instantiation -> effect/continuation finalization ->
  tail-call lowering -> accelerator selection -> control-outcome lowering ->
  representation selection -> ownership analysis/lowering -> cleanup
  lowering -> `runLlvmReadinessVerification` (all phase/domain/SSA verifiers)
  -> LLVM. A milestone may skip a
  pass only after its verifier proves the corresponding operation family is
  absent; it may never advance unresolved operations to `LlvmReady`.
- Before every task commit, update only that task's proven checkboxes and add
  its exact verification result to this plan, then stage this plan alongside
  the implementation. Update `CHANGELOG.md`, TODOs, internal docs, and public
  docs only in tasks that actually change their described behavior; Task 17
  performs the complete shipped-documentation cutover.
- Do not stage or overwrite unrelated worktree changes. In particular, finish
  the already-started AArch64 SJLJ correction as the isolated baseline task.
- Use `Unreleased` in `CHANGELOG.md`; do not change `VERSION`.

## Locked File and Component Map

The component graph remains:

```text
yona_model
  -> yona_interface
  -> yona_semantics
  -> yona_typed_ir
  -> yona_codegen_llvm
  -> yona_toolchain
```

New responsibilities are split as follows:

- `include/yona/Model/StructuralType.h` and
  `src/Model/StructuralType.cpp`: interned semantic/ABI type and effect
  vocabulary shared by interface and Typed IR.
- `include/yona/TypedIr/{Ids,Instruction,TypedIr,Builder,Verifier,Printer,Parser}.h`
  and matching sources: canonical SSA storage and phase contracts.
- `include/yona/TypedIr/{Pattern,DecisionTree,Callable,Ownership,Control,Cleanup}.h`:
  domain records without LLVM dependencies.
- `include/yona/TypedIr/{Analysis,Passes,Verification}/` and matching sources:
  immutable analyses, transformations, and focused verifiers.
- `include/yona/Codegen/Llvm/` and `src/Codegen/Llvm/`: module-, function-,
  block-, ABI-, debug-, accelerator-, optimizer-, and object-lowering units.
- `include/yona/Toolchain/CompilerPipeline.h` and
  `src/Toolchain/CompilerPipeline.cpp`: the sole finished frontend-to-artifact
  orchestration API.
- `include/yona/Runtime/Core/{Abi,Callable,Outcome,Effect}.h` and matching C
  sources: the replacement universal runtime boundary.
- `include/yona/Interface/{Schema,Version,FormatReader,FormatWriter}.h` and
  matching sources: `.yonai` v2 framing and structural metadata only.
- `test/Support/TypedIrTestUtil.h` and
  `test/Support/TypedIrExecution.{h,cpp}`: one parse/analyze/lower/verify and
  O0-O3 link/execute/allocation-stat harness shared by every new test.

---

### Task 1: Make the legacy ARM64 oracle safe enough to retain temporarily

**Files:**

- Modify: `include/yona/Runtime/Platform/SjLj.h`
- Modify: `src/Codegen/CodegenExpr.cpp`
- Modify: `src/Runtime/Core/Exceptions.c`
- Modify: `test/CMake/native_arm64_ci_packaging_contract.py`
- Create: `test/Fixtures/LegacyOracle/Prelude.yona`
- Create: `test/Fixtures/LegacyOracle/Std/Regex.yona`
- Modify: `test/Interface/PreludeInterfaceTest.cpp`
- Modify: `test/Codegen/CodegenTest.cpp`
- Modify: `CMakeLists.txt`
- Modify: `docs/platform-architecture.md`
- Modify: `docs/superpowers/plans/2026-08-27-native-arm64-ci-packaging.md`
- Modify: `docs/todo-list.md`
- Modify: `CHANGELOG.md`

**Interfaces:**

- Consumes: existing `YONA_SJLJ_SETJMP(Buffer)` and
  `yonaSjLjLongJump(void *Buffer)` legacy-only entry points.
- Produces: a complete `YonaSjLjBufT` AAPCS64 save area whose restore sequence
  uses fixed caller-saved registers for its buffer base and branch target.
- Lifetime: this code remains exclusively for legacy oracle execution and is
  deleted in Task 17.

The legacy oracle also gets two immutable source snapshots. Copy the current
`lib/Prelude.yona` and `lib/Std/Regex.yona` byte-for-byte to
`test/Fixtures/LegacyOracle/Prelude.yona` and
`test/Fixtures/LegacyOracle/Std/Regex.yona`. Make the legacy Prelude custom
command and `PreludeInterfaceTest` consume only the first path; make the one
`CodegenTest` case that directly compiles `Std\Regex` consume only the second.
Checked-in v1 interfaces and the old production compiler continue using those
frozen inputs; replacement bootstrap always reads the live canonical sources.
The ARM64/CMake contract asserts the two target graphs and paths are disjoint,
the test paths are literal oracle paths, and no generic source-root lookup can
redirect the oracle. A SHA-256 file generated in the contract's temporary
directory pins each copy to the source bytes at this commit, and later tasks
must not update either snapshot. This lets Task 15 migrate the live Prelude
and Regex declarations without asking the old compiler to parse or lower the
replacement ABI.

- [ ] **Step 1: Tighten the static contract test around the latent restore bug**

Add these checks after loading `SjLj.h` in
`test/CMake/native_arm64_ci_packaging_contract.py`:

```python
require(
    sjlj,
    r'mov x16, %0.*?ldr d8, \[x16, #104\].*?'
    r'ldp x27, x28, \[x16, #88\].*?ldr x17, \[x16, #8\].*?br x17',
    'AArch64 longjmp must keep the restore base in caller-saved x16.',
    failures,
)
if re.search(r'yonaSjLjLongJump.*?\[%0', sjlj, re.DOTALL):
    failures.append(
        'AArch64 longjmp must not address through an unconstrained operand '
        'after restoring x19-x28.'
    )
```

- [ ] **Step 2: Run the contract and confirm the intended red failure**

Run:

```bash
python3 test/CMake/native_arm64_ci_packaging_contract.py
```

Expected: failure containing `restore base in caller-saved x16`.

- [ ] **Step 3: Restore through fixed caller-saved registers**

Use `x16` as the immutable buffer base and `x17` for saved SP/PC. The core
inline assembly in `yonaSjLjLongJump` must be:

```c
__asm__ volatile("mov x16, %0\n\t"
                 "ldr d8, [x16, #104]\n\t"
                 "ldr d9, [x16, #112]\n\t"
                 "ldr d10, [x16, #120]\n\t"
                 "ldr d11, [x16, #128]\n\t"
                 "ldr d12, [x16, #136]\n\t"
                 "ldr d13, [x16, #144]\n\t"
                 "ldr d14, [x16, #152]\n\t"
                 "ldr d15, [x16, #160]\n\t"
                 "ldp x19, x20, [x16, #24]\n\t"
                 "ldp x21, x22, [x16, #40]\n\t"
                 "ldp x23, x24, [x16, #56]\n\t"
                 "ldp x25, x26, [x16, #72]\n\t"
                 "ldp x27, x28, [x16, #88]\n\t"
                 "ldr x29, [x16]\n\t"
                 "ldr x17, [x16, #16]\n\t"
                 "mov sp, x17\n\t"
                 "ldr x17, [x16, #8]\n\t"
                 "br x17"
                 :
                 : "r"(YonaSjLjBuffer)
                 : "x16", "x17", "memory");
```

Keep the complete x19-x28/d8-d15 save logic already present in both the
runtime macro and generated legacy `try` lowering.

- [ ] **Step 4: Verify the focused native and emulated baseline**

Run:

```bash
python3 test/CMake/native_arm64_ci_packaging_contract.py
scripts/test-arm64-qemu.sh -tc='*channel*ownership*,*async*exception*'
git diff --check
```

Expected: the contract passes; the focused Fedora 44 ARM64 suite reports all
selected cases passing with balanced allocation counts.

Remove the completed AArch64 SJLJ restore entry from `docs/todo-list.md`; the
changelog and ARM64 plan retain its history until SJLJ itself is deleted.

- [ ] **Step 5: Commit only the isolated baseline files**

```bash
git add CHANGELOG.md docs/platform-architecture.md \
  docs/superpowers/plans/2026-08-27-native-arm64-ci-packaging.md \
  docs/todo-list.md \
  include/yona/Runtime/Platform/SjLj.h src/Codegen/CodegenExpr.cpp \
  src/Runtime/Core/Exceptions.c \
  test/CMake/native_arm64_ci_packaging_contract.py \
  test/Fixtures/LegacyOracle/Prelude.yona \
  test/Fixtures/LegacyOracle/Std/Regex.yona \
  test/Interface/PreludeInterfaceTest.cpp test/Codegen/CodegenTest.cpp \
  CMakeLists.txt
git add docs/superpowers/plans/2026-09-01-typed-ir-codegen-rewrite.md
git commit -m "fix: preserve aarch64 exception context"
```

### Task 2: Introduce structural types and preserve semantic identities

**Files:**

- Create: `include/yona/Model/StructuralType.h`
- Create: `src/Model/StructuralType.cpp`
- Create: `include/yona/Model/CanonicalEncoding.h`
- Create: `src/Model/CanonicalEncoding.cpp`
- Create: `include/yona/Semantics/StructuralTypeProjection.h`
- Create: `src/Semantics/StructuralTypeProjection.cpp`
- Create: `test/Model/StructuralTypeTest.cpp`
- Modify: `include/yona/Model/EffectSolver.h`
- Modify: `include/yona/Model/InferType.h`
- Modify: `include/yona/Model/TypeArena.h`
- Modify: `src/Model/TypeArena.cpp`
- Modify: `include/yona/Semantics/Unification.h`
- Modify: `src/Semantics/Unification.cpp`
- Modify: `src/Model/EffectSolver.cpp`
- Create: `test/Model/EffectSolverTest.cpp`
- Modify: `include/yona/Semantics/TypeChecker.h`
- Modify: `src/Semantics/TypeChecker.cpp`
- Modify: `test/Semantics/TypeCheckerTest.cpp`
- Modify: `include/yona/Model/ModuleIdentity.h`
- Modify: `src/Model/ModuleIdentity.cpp`
- Modify: `include/yona/Semantics/SemanticModel.h`
- Modify: `src/Semantics/SemanticModel.cpp`
- Modify: `test/Semantics/SemanticModelTest.cpp`
- Modify: `cmake/YonaComponents.cmake`
- Modify: `CMakeLists.txt`

**Interfaces:**

- Consumes: solved `compiler::typechecker::MonoTypePtr`, its frozen effect
  graph, `model::ModuleIdentity`, and semantic `OwnershipKind`.
- Produces: `model::TypeTable`, `model::TypeId`, `model::EffectRowId`,
  `model::StructuralType`, `model::EffectRow`, `model::CallingConvention`, and
  distinct `model::ParameterOwnership`/`model::ResultOwnership` contracts.
- Produces on `SemanticModel`: `types()`, the compiler-internal
  `sharedTypeArena()`, `sources()`, and `bindingFor(const ast::AstNode *)`,
  plus the canonical inferred effect operation table, without retaining
  `TypeChecker` storage.

- [ ] **Step 1: Write structural interning and lifetime tests**

Add tests with these assertions:

```cpp
TEST_CASE("Structural type interning is canonical and nominal") {
  yona::model::TypeTable Types;
  const auto Int = Types.intern(yona::model::PrimitiveType::Int);
  REQUIRE(Int.has_value());
  CHECK(Types.intern(yona::model::PrimitiveType::Int) == Int);

  const auto LocalOption = Types.intern(yona::model::NominalType{
      yona::model::NominalTypeKey{yona::model::ModuleIdentity{"Main"},
                                  "Option"},
      {*Int}});
  REQUIRE(LocalOption.has_value());
  const auto OtherOption = Types.intern(yona::model::NominalType{
      yona::model::NominalTypeKey{yona::model::ModuleIdentity{"Other"},
                                  "Option"},
      {*Int}});
  REQUIRE(OtherOption.has_value());
  CHECK(*LocalOption != *OtherOption);
  CHECK(Types.format(*LocalOption) == "Main\\Option Int");
}

TEST_CASE("Structural type record fields have deterministic identity") {
  yona::model::TypeTable Types;
  const auto Int = Types.intern(yona::model::PrimitiveType::Int);
  const auto Bool = Types.intern(yona::model::PrimitiveType::Bool);
  REQUIRE(Int.has_value());
  REQUIRE(Bool.has_value());
  const auto Left = Types.intern(
      yona::model::RecordType{{{"z", *Bool}, {"a", *Int}}});
  const auto Right = Types.intern(
      yona::model::RecordType{{{"a", *Int}, {"z", *Bool}}});
  REQUIRE(Left.has_value());
  REQUIRE(Right.has_value());
  CHECK(*Left == *Right);
}
```

Create `Semantic model structural type projection preserves nominal identity`
as the top-level semantic projection sentinel, so both Task 2 focused prefixes
are independently enumerable.

Extend `SemanticModelTest.cpp` so a model survives destruction of its checker
and still formats the root type and resolves an identifier node's binding.
Add table-driven cases proving matching perform/handler uses create one
qualified operation signature, conflicting uses are rejected with both
ranges, and transitive calls preserve `MayRaise`/`MayCancel` facts.
Add rows containing the same generic operation key applied to `Int` and to
`String`; require distinct interning/canonical bytes, exact argument-order
round trips, and rejection of wrong type/effect argument arity.
Freeze one recursive-nominal/function/effect operation graph and assert its
exact canonical bytes and fingerprint independent of insertion order. Clone
the fixture with randomized raw `TypeParameterId` and open-effect-variable IDs
and require byte-identical alpha-normalized output. Add a negative case for an
unbound/ multiply-owned binder and a golden case proving that permuted,
duplicate-free trait constraints canonicalize identically.
Add open generic operation fixtures proving alpha-renamed type/effect binder
IDs produce identical declaration bytes/fingerprints, while
`encodeClosedDescriptorGraph` rejects the same open signature. Instantiate it
at two concrete types and require distinct closed runtime bytes/fingerprints;
repeat with phantom type/effect binders absent from the substituted signature
and still require distinct closed bytes;
force equal stored hashes in both domains and require full same-domain byte
comparison to reject unequal declarations/instances.
Add anonymous Sum rows proving `Int | String`, `String | Int`, duplicate
alternatives, and nested source sums normalize to one ID and canonical byte
sequence; a singleton-after-dedup canonicalizes to its member, while an empty
sum, direct structural cycle, foreign alternative, and marker-as-runtime-
carrier are rejected transactionally.
Round-trip a Sum through inference, structural projection, TypeTable remapping,
and canonical structural encoding in this task. Task 3 adds the TIRF
printer/parser round-trip, Task 15 adds v2, and Task 16's ABI matrix executes
scalar and managed injections/type patterns with balanced owners; Task 2 must
not import those later components.
Add exact solver/projection cases for an ACI join containing two independent
tails, one tail shared by two arrow roots, a symbolic mask, and an opaque
source. Randomize join insertion order and raw variable IDs and require the
same structural rows and canonical bytes. Put `E.op<Int>` and `E.op<String>`
in one row, mask only the former, and require the latter to survive. Exercise
two initially distinct atoms whose type variables later unify, then require
an existing known set/mask/equality constraint to use their one representative
before scheme freeze. Distinguish masks by ordered effect-row arguments, by
outer quantified type/effect arguments before and after substitution, and by
phantom declared arguments as well as by `Int` versus `String`. Exercise
an open record `{a : Int | r}`, two records sharing `r`, closed-record
substitution, overlap rejection, canonical round trip, and rejection of an
open row reachable from executable IR. Finally require phantom declared type
and Flexible effect binders to survive `TypeScheme` projection even when the
body, constraints, and latent rows do not mention them. Project the same
borrowed inference Arrow under two distinct legal ownership/calling-convention
contracts and require distinct structural FunctionType IDs; also assert stable
ranged error codes for every projection rejection above.

- [ ] **Step 2: Run the tests and confirm missing structural APIs**

Run:

```bash
cmake --build --preset build-debug-linux --target tests -j2
./out/build/x64-debug-linux/tests -tc='Structural type*,Semantic model structural type*'
```

Expected: compilation fails because `StructuralType.h`, `TypeTable`, and the
new `SemanticModel` accessors do not exist.

- [ ] **Step 3: Implement the immutable structural vocabulary**

Define the core public records exactly once in `StructuralType.h`:

```cpp
namespace yona::model {

using IdDomain = std::uint64_t;
inline constexpr IdDomain InvalidIdDomain = 0;

template <class Tag> class StrongId final {
public:
  constexpr StrongId() noexcept = default;
  constexpr StrongId(IdDomain Domain, std::uint32_t Value) noexcept
      : Domain_(Domain), Value_(Value) {}
  [[nodiscard]] constexpr bool isValid() const noexcept;
  [[nodiscard]] constexpr IdDomain domain() const noexcept;
  [[nodiscard]] constexpr std::uint32_t value() const noexcept;
  friend constexpr bool operator==(StrongId, StrongId) noexcept = default;
  friend constexpr auto operator<=>(StrongId, StrongId) noexcept = default;
private:
  IdDomain Domain_ = InvalidIdDomain;
  std::uint32_t Value_ = std::numeric_limits<std::uint32_t>::max();
};

struct StrongIdHasher final {
  template <class Tag>
  std::size_t operator()(StrongId<Tag> Id) const noexcept {
    auto Seed = std::hash<IdDomain>{}(Id.domain());
    Seed ^= std::hash<std::uint32_t>{}(Id.value()) +
            static_cast<std::size_t>(0x9e3779b9u) + (Seed << 6u) +
            (Seed >> 2u);
    return Seed;
  }
};

using TypeId = StrongId<struct TypeIdTag>;
using EffectRowId = StrongId<struct EffectRowIdTag>;
using TypeParameterId = StrongId<struct TypeParameterIdTag>;
using EffectVariableId = StrongId<struct EffectVariableIdTag>;

enum class PrimitiveType : std::uint8_t {
  Unit = 0, Bool = 1, Int = 2, Float = 3, Char = 4, String = 5,
  Symbol = 6, Byte = 7
};
enum class ParameterOwnership : std::uint8_t {
  Trivial = 0, Borrow = 1, Consume = 2
};
enum class ResultOwnership : std::uint8_t {
  Trivial = 0, Owned = 1
};
enum class CallingConvention : std::uint8_t {
  DirectYona = 0, ClosureEntry = 1, Continuation = 2,
  EffectOperation = 3, AsyncAdapter = 4, NativeExtern = 5,
  ExportedC = 6
};

struct NominalTypeKey {
  ModuleIdentity Module;
  std::string Name;
  friend bool operator==(const NominalTypeKey &,
                         const NominalTypeKey &) = default;
};
struct SymbolIdentity {
  ModuleIdentity Module;
  std::string Name;
  friend bool operator==(const SymbolIdentity &,
                         const SymbolIdentity &) = default;
};
struct LocalFunctionIdentity {
  ModuleIdentity Module;
  std::vector<std::uint32_t> LexicalDeclarationPath;
  friend bool operator==(const LocalFunctionIdentity &,
                         const LocalFunctionIdentity &) = default;
};
using FunctionDeclarationIdentity =
    std::variant<SymbolIdentity, LocalFunctionIdentity>;
struct TypeParameterType { TypeParameterId Parameter; };
struct SumType { std::vector<TypeId> Alternatives; };
struct TupleType { std::vector<TypeId> Elements; };
struct SequenceType { TypeId Element; };
struct SetType { TypeId Element; };
struct DictionaryType { TypeId Key; TypeId Value; };
struct RecordField { std::string Name; TypeId Type; };
struct RecordType {
  std::vector<RecordField> Fields;
  std::optional<TypeId> RowRest;
};
struct NominalType { NominalTypeKey Key; std::vector<TypeId> Arguments; };
struct ArrayType { TypeId Element; bool Mutable; };
struct ChannelType { TypeId Element; }; // compiler-internal shared channel core
struct PromiseType {
  TypeId Element;
  ResultOwnership ResultContract;
  EffectRowId Effects;
};
struct CursorType {
  TypeId Element;
  ResultOwnership ElementContract; // Trivial or Owned
};
struct CursorStepType {
  TypeId Element;
  ResultOwnership ElementContract; // Trivial or Owned
};
struct OutcomeTaskGroupType {};
struct ResourceType {
  NominalTypeKey Key;
  std::vector<TypeId> Arguments;
};
enum class ResourceAbiKind : std::uint8_t {
  GenericResource = 0, ChannelSender = 1, ChannelReceiver = 2
};
enum class ResourceShareability : std::uint8_t {
  Linear = 0, AlwaysShareable = 1
};
struct ResourceDeclaration {
  NominalTypeKey Key;
  std::vector<TypeParameterId> TypeParameters;
  ResourceAbiKind AbiKind;
  ResourceShareability Shareability;
  std::optional<std::string> TryRetainNativeSymbol;
  std::string ReleaseNativeSymbol;
};
enum class AbiOpaqueKind : std::uint8_t {
  ExceptionValue = 0, EffectRequest = 1, ControlOutcome = 2,
  ExecutionContext = 3, CallableInvocationEnvironment = 4,
  ContinuationBoundaryContext = 5
};
struct AbiOpaqueType { AbiOpaqueKind Kind; };
enum class OwnedSlotStateKind : std::uint8_t {
  HandlerRouter = 0, TryBoundary = 1, CleanupObligation = 2
};
struct OwnedSlotField {
  TypeId Type;
  ParameterOwnership Ownership;
};
struct OwnedSlotStateType {
  OwnedSlotStateKind Kind;
  std::vector<OwnedSlotField> Fields;
  FunctionDeclarationIdentity DropIdentity;
};
struct FunctionParameter { TypeId Type; ParameterOwnership Ownership; };
struct EffectOperationKey {
  NominalTypeKey Effect;
  std::string Operation;
  friend bool operator==(const EffectOperationKey &,
                         const EffectOperationKey &) = default;
};
struct EffectOperationApplication {
  EffectOperationKey Key;
  std::vector<TypeId> TypeArguments;
  std::vector<EffectRowId> EffectArguments;
  friend bool operator==(const EffectOperationApplication &,
                         const EffectOperationApplication &) = default;
};
struct TypeConstraint {
  TypeParameterId Parameter;
  NominalTypeKey Trait;
  std::vector<TypeId> Arguments;
};
struct TraitMethodKey {
  NominalTypeKey Trait;
  std::string Method;
};
struct TraitTargetApplication {
  FunctionDeclarationIdentity Target;
  std::vector<TypeId> TypeArguments;
  std::vector<EffectRowId> EffectArguments;
};
struct DeferredTraitEvidence {
  FunctionDeclarationIdentity OwningScheme;
  TypeConstraint Constraint;
};
using TraitSelectionEvidence =
    std::variant<DeferredTraitEvidence, TraitTargetApplication>;
struct TraitResolutionRequest {
  TraitMethodKey Method;
  std::vector<TypeId> InstanceArguments;
  std::vector<TypeId> MethodTypeArguments;
  std::vector<EffectRowId> MethodEffectArguments;
  TraitSelectionEvidence Evidence;
};
enum class EffectVariableKind : std::uint8_t {
  Flexible = 0, Opaque = 1
};
struct EffectVariableDeclaration {
  EffectVariableId Variable;
  EffectVariableKind Kind;
};
struct GenericBinderEnvironment {
  std::vector<TypeParameterId> InheritedTypeParameters;
  std::vector<EffectVariableDeclaration> InheritedEffectVariables;
  std::vector<TypeParameterId> DeclaredTypeParameters;
  std::vector<EffectVariableDeclaration> DeclaredEffectVariables;
};
struct FunctionType {
  std::vector<TypeParameterId> TypeParameters;
  std::vector<EffectVariableDeclaration> EffectVariables;
  std::vector<TypeConstraint> Constraints;
  std::vector<FunctionParameter> Parameters;
  TypeId Result;
  ResultOwnership ResultContract;
  EffectRowId Effects;
  CallingConvention Convention;
  std::optional<FunctionDeclarationIdentity> SourceIdentity;
  std::optional<SymbolIdentity> ExportedSymbol;
};

struct EffectOperationSignature {
  EffectOperationKey Key;
  std::vector<TypeParameterId> TypeParameters;
  std::vector<EffectVariableDeclaration> EffectVariables;
  std::vector<TypeConstraint> Constraints;
  std::vector<FunctionParameter> Parameters;
  TypeId Result;
  ResultOwnership ResultContract;
  EffectRowId Effects;
};

using StructuralType = std::variant<PrimitiveType, TypeParameterType,
    TupleType, SequenceType, SetType, DictionaryType, RecordType, NominalType,
    ArrayType, ChannelType, PromiseType, CursorType, CursorStepType,
    OutcomeTaskGroupType, ResourceType, AbiOpaqueType, OwnedSlotStateType,
    FunctionType, SumType>;

`ResourceType::Arguments` is mandatory and declaration ordered. Its arity is
validated against the owning resource declaration just like NominalType;
binder substitution, canonical encoding, remap, cycle validation, hashing,
and runtime descriptor instantiation traverse every argument. A parameterized
resource is keyed by qualified identity plus the complete argument graph, so
`Sender Int` and `Sender String` are distinct structural types/descriptors
even under a forced fingerprint collision. A bare or wrong-arity generic
resource is never runtime-reachable.

`model::ResourceDeclaration` is the interface-independent executable policy
for a ResourceType. Every runtime-reachable resource key has exactly one row
in `typed_ir::Module::Resources`; the row's type parameters own the type's
arguments and its kind/shareability/callback identities select representation,
descriptor flags, retain, and release. A missing row, duplicate key,
conflicting imported/local row, wrong arity, callback-policy mismatch, or
unknown enum value is invalid before ownership analysis. Interface v2 maps
its schema row to and from this model record plus visibility; neither LLVM nor
Typed IR depends on the interface layer to recover policy.

`ChannelType` is the compiler-internal shared queue-core shape and is never
source-nameable or an outer v2 root. Public `Sender a` and `Receiver a` are
distinct parameterized ResourceTypes whose manifest representation selects
the CHANNEL ABI kind; qualified key plus complete arguments therefore encode
both endpoint role and payload type without an interchangeable raw handle.

struct EffectTailProjection {
  EffectVariableId Variable;
  EffectVariableKind Kind;
  std::vector<EffectOperationApplication> ExcludedOperations;
};
struct EffectRow {
  std::vector<EffectOperationApplication> Operations;
  std::vector<EffectTailProjection> Tails;
  bool MayRaise = false;
  bool MayCancel = false;
};
enum class BinderOwnerKind : std::uint8_t {
  Nominal = 0, Trait = 1, Function = 2, Instance = 3, Operation = 4,
  Resource = 5
};
struct InstanceBinderOwner {
  NominalTypeKey Trait;
  std::vector<TypeId> Arguments;
  std::vector<TypeConstraint> Constraints;
};
using CanonicalBinderOwnerIdentity =
    std::variant<NominalTypeKey, FunctionDeclarationIdentity, InstanceBinderOwner,
                 EffectOperationKey>;
struct CanonicalBinderDeclaration {
  BinderOwnerKind Kind;
  CanonicalBinderOwnerIdentity Identity;
  std::vector<TypeParameterId> TypeParameters;
  std::vector<EffectVariableDeclaration> EffectVariables;
};
struct BinderDeclarationPair {
  CanonicalBinderDeclaration Source;
  CanonicalBinderDeclaration Destination;
};
struct TypeRemap {
  std::vector<TypeId> Types;
  std::vector<EffectRowId> Effects;
  std::vector<TypeParameterId> TypeParameters;
  std::vector<EffectVariableId> EffectVariables;
};

class TypeTableAppendTransaction;
class TypeTable final {
public:
  TypeTable();
  TypeTable(TypeTable &&) noexcept = default;
  TypeTable &operator=(TypeTable &&) noexcept = default;
  TypeTable(const TypeTable &) = delete;
  TypeTable &operator=(const TypeTable &) = delete;
  [[nodiscard]] IdDomain idDomain() const noexcept;
  TypeParameterId allocateTypeParameter();
  EffectVariableId allocateEffectVariable();
  [[nodiscard]] std::expected<TypeId, std::string>
  internSum(std::span<const TypeId> Alternatives);
  [[nodiscard]] std::expected<TypeId, std::string>
  intern(StructuralType Type);
  [[nodiscard]] std::expected<EffectRowId, std::string>
  internEffect(EffectRow Row);
  [[nodiscard]] TypeTableAppendTransaction beginAppendTransaction();
  [[nodiscard]] const StructuralType &at(TypeId Id) const;
  [[nodiscard]] const EffectRow &effect(EffectRowId Id) const;
  [[nodiscard]] bool valid(TypeId Id) const noexcept;
  [[nodiscard]] bool valid(EffectRowId Id) const noexcept;
  [[nodiscard]] std::size_t typeCount() const noexcept;
  [[nodiscard]] std::size_t effectCount() const noexcept;
  [[nodiscard]] std::expected<TypeRemap, std::string>
  remapInto(TypeTable &Destination,
            std::span<const BinderDeclarationPair> BinderPairs) const;
  [[nodiscard]] std::string format(TypeId Id) const;
  [[nodiscard]] std::expected<void, std::string> validate() const;
};

class TypeTableAppendTransaction final {
public:
  TypeTableAppendTransaction(TypeTableAppendTransaction &&) noexcept;
  TypeTableAppendTransaction &
  operator=(TypeTableAppendTransaction &&) noexcept;
  TypeTableAppendTransaction(const TypeTableAppendTransaction &) = delete;
  TypeTableAppendTransaction &
  operator=(const TypeTableAppendTransaction &) = delete;
  ~TypeTableAppendTransaction(); // discards unless committed

  TypeParameterId allocateTypeParameter();
  EffectVariableId allocateEffectVariable();
  TypeId reserveType();
  EffectRowId reserveEffect();
  [[nodiscard]] std::expected<void, std::string>
  define(TypeId Reserved, StructuralType Type);
  [[nodiscard]] std::expected<void, std::string>
  defineEffect(EffectRowId Reserved, EffectRow Row);
  [[nodiscard]] std::expected<TypeId, std::string>
  internSum(std::span<const TypeId> Alternatives);
  [[nodiscard]] std::expected<TypeId, std::string>
  intern(StructuralType Type);
  [[nodiscard]] std::expected<EffectRowId, std::string>
  internEffect(EffectRow Row);
  [[nodiscard]] std::expected<void, std::string> prepareCommit();
  [[nodiscard]] std::expected<TypeId, std::string>
  resolved(TypeId Provisional) const;
  [[nodiscard]] std::expected<EffectRowId, std::string>
  resolved(EffectRowId Provisional) const;
  void commitPrepared() noexcept;
};

} // namespace yona::model
```

Each `TypeTable` allocates a nonzero process-unique, nonserialized `IdDomain`;
all four model ID families carry that domain plus their dense local ordinal.
`valid` checks both domain equality and range, so a foreign table's ordinal
zero cannot alias this table's ordinal zero. Moving a table preserves its
domain. Cloning/decoding creates a fresh domain and exhaustively remaps every
ID; canonical encoders write only canonical local indices, never the domain.
The table is append-only and stores interned records at stable addresses.
Normal frontend lowering shares one heap-allocated table between its immutable
`SemanticModel` view and destination `typed_ir::Module`; the table object is
never moved after publication. Explicit clone/decode transactions are the
only operations that create a new domain.
`TypeTableAppendTransaction` snapshots all four arena sizes plus a mutation
generation. `reserveType`/`reserveEffect` allocate transaction-local
provisional IDs in the same domain; each must be defined exactly once before
preparation, which permits a legal forward-referencing acyclic graph to be
staged without a dummy row. `intern`/`internEffect` are reserve-plus-define
conveniences. Generic `intern(StructuralType)` and `define` recognize
`SumType` and route it through the same delayed
flatten/sort/deduplicate/empty/singleton normalization as `internSum`; there is
no unchecked Sum insertion path. A singleton reserved definition resolves to
its member rather than publishing a Sum row, and an empty definition fails
the transaction. All callers handle the fallible generic result.
`prepareCommit` rejects an undefined or multiply defined slot, any directed
cycle across the joint type/effect graph, a foreign reference, or a changed
generation. It
then canonicalizes the complete private graph (including delayed Sum
flatten/sort/dedup after every alternative is defined), resolves
committed/staged duplicates, rewrites every child TypeId/EffectRowId inside
the private staged records through the result, precomputes final hash/equality
keys, reserves all stable storage and indices, and freezes a mapping available
through `resolved`. After successful preparation that lookup is total for
every pre-snapshot committed ID (identity) and every defined provisional; it
returns an error before preparation and for a foreign, undefined, or discarded
ID. Provisional IDs are never published; every coordinated module
arena rewrites its private staged references through that mapping before its
own prepare barrier. A genuinely new row may keep its provisional ordinal,
but callers never depend on that optimization.
Failure leaves the table and every coordinated arena byte-for-byte unchanged.
Only after all participants are prepared may the nonthrowing
`commitPrepared` append the deduplicated final rows and increment the
generation, followed by the other nonthrowing arena commits. Coordinated
module buffers may contain only resolved final IDs, publish only after the
table commit, and remain privately discardable if any later prepare barrier
fails. The destructor
drops private rows and mappings. Ordinary one-off alloc/intern operations use
the same mechanism, and concurrent/mixed mutation is rejected rather than
invalidating a published ID.
Tests deliberately collide ordinal-zero Type/Effect/TypeParameter/
EffectVariable IDs from two tables and require every foreign lookup,
constraint, and resolver request to fail before indexing.
`StructuralTypeTest` also reserves and defines a forward-referencing
type/effect/binder DAG in one append transaction, proves no provisional ID is
readable or publishable before preparation, rewrites all private references
through `resolved`, and proves the committed graph uses those final IDs. A
separate direct-cycle case is rejected unchanged. Separate duplicate,
stale-generation, invalid-reference, and forced reserve/hash failure cases
destroy the transaction and compare all four arena sizes, canonical bytes,
domains, generations, and existing-record addresses byte-for-byte with the
pre-transaction snapshot.

`remapInto` is declaration seeded rather than occurrence discovered. The
caller pairs each source binder owner with the already-created destination
owner; the remapper validates equal owner kind, declaration arity, and effect
variable kind, then maps every declaration position before walking a type or
row. The returned binder vectors are indexed by raw source binder ID and
include unused phantom binders. An encountered unseeded binder, duplicate or
conflicting seed, invalid destination binder, or owner mismatch fails without
changing the destination. Type/effect nodes are staged and committed only
after the complete graph validates. Interface loading and generic-fragment
import use this one result to rewrite the structural table and every nominal,
resource, function, operation, generic, trait, and instance declaration;
ResourceType arguments and Resource owner binders are traversed exactly like
their nominal counterparts. Neither path
infers a binder mapping from reachable occurrences. Tests merge two source
tables with colliding raw binder IDs and unused type/effect phantoms and prove
that both destination declarations remain distinct and alpha-equivalent.

`OwnedSlotStateType` is the canonical internal type for handler-router state,
try-boundary state, and armed cleanup obligations. It is always managed,
linear, and non-Shareable; fields are Trivial or Consume (Borrow is invalid),
and mandatory `DropIdentity` names the predeclared deterministic drop entry that clears
every still-owned slot in order. Ordinary tuple/record types can never alias or
compare equal to it. It is legal in canonical TIRF but is never an outer public
v2 root. Representation selection maps each value to `ManagedPointer`; that
pointer's structural descriptor is the exact `YonaOwnedSlotStateDescriptor`
with `YONA_ABI_OWNED_SLOT_STATE`. Verifier/descriptor tests require exact kind,
field type/ownership vector, drop identity, and full structural bytes, and a
representation-selection matrix covers HandlerRouter, TryBoundary, and
CleanupObligation.

`CursorType` and `CursorStepType` are internal managed, linear,
non-Shareable carriers. Their element type and normalized Trivial/Owned result
contract determine `CursorValueInst` statically; Borrowed is forbidden. A
dictionary cursor's element is the structural key/value tuple. They are not
source-nameable or legal public interface roots. Representation selection maps
them only to `YonaCursorRef`/`YonaCursorStepRef`, and canonical encoding follows
their element graph.

`OutcomeTaskGroupType` is the canonical internal managed carrier for one
structured-concurrency group owner. The compiler-visible owner is linear and
non-Shareable. The group owns its submitted child references; children carry
only their own atomic membership state and ordinal, never a pointer or owning
reference back to the group. Generated code may only borrow the unique group
owner for join/take and may never emit a group retain.
It is not source-nameable, comparable,
serializable as a public interface root, or usable as a Promise payload.
Representation selection maps it only to `YonaOutcomeTaskGroupRef`; ordinary
resources and opaque pointers can never compare equal to it. Task 14 requires
every group-producing instruction and group operand to use this exact type.

`PromiseType` describes the value observed after a whole-outcome await, not
the task handle's storage layout. Its Success payload is always self-contained:
only Trivial and Owned result contracts exist. A Borrowed callable/operation
result is deliberately unrepresentable until Yona has a source-visible
lifetime system; semantic projection, canonical fragment/interface decoding,
and verifier construction reject it rather than guessing an owner. The
Promise, async result record, callable adapter, task descriptor, and await
result must all agree on that exact structurally derived contract. Every submitted task uses
the async lift of its work row: all operation applications and `MayRaise` are
preserved and `MayCancel` is forced true. This lifted row, not the synchronous
callable row, is stored in its Promise and propagated at each demanded await;
an async result contract without `MayCancel` is invalid.

`SumType` is the canonical anonymous source union. The checked `internSum`
path recursively flattens nested sums, sorts alternatives by a dedicated
arena-local recursive structural-order key, and removes duplicates. That key
topologically normalizes the staged joint type/effect graph, compares complete
child structure, and treats raw binder IDs as arena-local semantic atoms; it
never compares TypeId/EffectRowId insertion ordinals. It therefore works for
open generic sums without pretending the closed-descriptor encoder can encode
them. `remapInto` re-normalizes every Sum after binder remapping, while v2 and
runtime-descriptor encoders independently order alternatives with their
binder-aware canonical bytes. One distinct alternative
canonicalizes to that alternative's existing TypeId; an empty list is an
error, so every stored Sum row has at least two children. Source order and raw
IDs are not identity. Transactional normalization waits until every reserved
alternative is defined. Sum graphs are acyclic just like
all other non-nominal structural graphs—recursive source data must use a
nominal declaration. A runtime sum value is one `YonaAbiValue` whose dynamic
descriptor is exactly one closed alternative, never the Sum marker itself.
`RuntimeTypeIsTest` selects an alternative by full descriptor equivalence and
`RuntimeTypePayloadProjection` exposes that same carrier under the selected
static type. A Borrow projection remains rooted in the actual sum owner, a
Retain projection clones through the actual descriptor, and construction uses
`InjectSumInst` to move an Owned alternative (or copy a Trivial one) into the
sum carrier without allocating or inventing a numeric tag.
Tests cover alpha-renamed open sums producing identical v2 bytes, a remap that
changes arena-local binder IDs and must resort, and both A-to-B-to-A and
Type-to-Effect-to-Type cycle rejection.

`resultOwnershipFor(const TypeTable &, TypeId)` is the single total structural
rule for every legal escaping result: no-carrier/scalar/process-lifetime
carriers are `Trivial`; every managed carrier and every Sum carrier is
`Owned`; internal borrow-only
opaque/state views are not legal results. Every stored `ResultContract` is
redundant wire/verifier evidence and must equal that function. Consequently a
handler/try/body result type cannot carry a second independent ownership
choice, and same-type/different-result-ownership records are rejected at
projection, decode, and descriptor validation.

`CanonicalEncoding.h` owns the versioned, fixed-width little-endian canonical
encoding of an isolated reachable structural type/effect/operation graph and
its FNV-1a fingerprint helpers. It canonicalizes/reindexes the reachable DAG,
excludes stored fingerprint fields, and has no interface or Typed IR
dependency. It exposes `CanonicalStructuralEncodingVersion == 2`; Task 15
statically requires that value for `YONAI 2`. Task 3 uses its isolated rooted
form for runtime descriptors/fingerprints; Task 15 uses its module-table form.
Both call the same record codecs, binder normalization, expanded-key ordering,
and discriminant constants, but their indices and complete envelopes are
deliberately scope-local and are never claimed byte-identical.

Expose the two forms as distinct APIs:

```cpp
struct CanonicalBinderRef {
  std::uint32_t BinderIndex;
  std::uint32_t Ordinal;
};
struct CanonicalTypeParameterMapping {
  TypeParameterId Source;
  CanonicalBinderRef Target;
};
struct CanonicalEffectVariableMapping {
  EffectVariableId Source;
  CanonicalBinderRef Target;
};
enum class CanonicalEncodingErrorCode : std::uint8_t {
  InvalidIdentity, InvalidBinder, UnboundParameter, MultiplyOwnedParameter,
  OpenRuntimeDescriptor, InvalidReference, DuplicateRecord,
  NonCanonicalOrder, SizeLimit
};
struct CanonicalEncodingError {
  CanonicalEncodingErrorCode Code;
  std::string Path;
  std::string Message;
};
enum class CanonicalRootKind : std::uint8_t { Type = 0, Effect = 1,
                                              Operation = 2 };
struct ClosedEffectOperation {
  EffectOperationKey Key;
  std::vector<TypeId> TypeArguments;
  std::vector<EffectRowId> EffectArguments;
  std::vector<FunctionParameter> Parameters;
  TypeId Result;
  ResultOwnership ResultContract;
  EffectRowId Effects;
};
using CanonicalRoot = std::variant<TypeId, EffectRowId,
                                   ClosedEffectOperation>;
using CanonicalBytesResult =
    std::expected<std::vector<std::byte>, CanonicalEncodingError>;
CanonicalBytesResult encodeClosedDescriptorGraph(const TypeTable &Types,
                                                  const CanonicalRoot &Root);
CanonicalBytesResult encodeOperationDeclarationGraph(
    const TypeTable &Types, const EffectOperationSignature &Signature,
    std::span<const CanonicalBinderDeclaration> Binders);

struct CanonicalInterfaceTables {
  std::vector<std::byte> TypeSectionPayload;
  std::vector<std::byte> EffectSectionPayload;
  std::vector<std::uint32_t> TypeRemap;
  std::vector<std::uint32_t> EffectRemap;
  std::vector<std::uint32_t> InstanceRemap;
  std::vector<CanonicalTypeParameterMapping> TypeParameterRemap;
  std::vector<CanonicalEffectVariableMapping> EffectVariableRemap;
};
std::expected<CanonicalInterfaceTables, CanonicalEncodingError>
encodeInterfaceTables(const TypeTable &Types,
                      std::span<const CanonicalBinderDeclaration> Binders);

using DecodedBinderOwnerIdentity =
    std::variant<NominalTypeKey, FunctionDeclarationIdentity, std::uint32_t,
                 EffectOperationKey>;
struct DecodedBinderDeclaration {
  BinderOwnerKind Kind;
  DecodedBinderOwnerIdentity Identity;
  std::vector<TypeParameterId> TypeParameters;
  std::vector<EffectVariableDeclaration> EffectVariables;
};
struct DecodedTypeParameterMapping {
  CanonicalBinderRef Source;
  TypeParameterId Target;
};
struct DecodedEffectVariableMapping {
  CanonicalBinderRef Source;
  EffectVariableId Target;
};
struct DecodedInterfaceTables {
  TypeTable Types;
  std::vector<DecodedBinderDeclaration> Binders;
  std::vector<TypeId> TypeIdsByCanonicalIndex;
  std::vector<EffectRowId> EffectIdsByCanonicalIndex;
  std::vector<DecodedTypeParameterMapping> TypeParametersByCanonicalRef;
  std::vector<DecodedEffectVariableMapping> EffectVariablesByCanonicalRef;
};
std::expected<DecodedInterfaceTables, CanonicalEncodingError>
decodeInterfaceTables(std::span<const std::byte> TypeSectionPayload,
                      std::span<const std::byte> EffectSectionPayload);
```

V2 readers decode the module-wide form and reconstruct binder-aware isolated
operation graphs with `encodeOperationDeclarationGraph`; runtime descriptor
emission builds `ClosedEffectOperation` and uses
`encodeClosedDescriptorGraph` only after specialization. Its concrete type/
effect arguments are encoded even when a binder is phantom and does not occur
in the substituted parameters/result/residual row. Adding
an unrelated module type may renumber the module table, but cannot alter the
bytes/fingerprint of either isolated root domain.
`decodeInterfaceTables` is the only binder/type/effect record decoder. It
allocates fresh in-memory raw type/effect binder IDs in canonical
`(BinderIndex, Ordinal)` order, returns both canonical-index-to-`TypeTable` ID
vectors and canonical-ref-to-raw-ID mappings, validates every reference and
record order, and rejects any payload it cannot reproduce canonically.
Instance binder identities remain canonical instance indices until the v2
reader decodes and validates the instance section. `FormatReader` uses these
returned mappings for all schema records and never decodes a structural
record itself; after instances are known it rebuilds binder declarations,
re-encodes the complete tables, and requires byte identity.
For `BinderOwnerKind::Function`, `DecodedBinderOwnerIdentity` preserves the
complete `FunctionDeclarationIdentity` discriminant and payload; both Symbol
and Local lexical-path identities round-trip, and a kind/payload mismatch is a
decode error. Add a nested-local generic binder fixture whose local ordinal
collides with a top-level symbol's table position, plus corrupt discriminant
and truncated lexical-path cases, and require byte-identical re-encoding.

The canonical encoding is normative here, not selected by its implementation.
It uses `u8`, `u32le`, and `u64le`; a string is `u64le byte_count` followed by
validated UTF-8 bytes; a vector is `u64le element_count` followed by its
elements; an option is `u8 present` (`0` or `1`) followed by the value only
when present. A `ModuleIdentity` is its single validated canonical fully
qualified string (including `\` separators). Raw in-memory type-parameter and
open-effect-variable IDs are never written or hashed. A canonical binder table
uses these records:

```text
BinderOwnerKind          := Nominal=0, Trait=1, Function=2, Instance=3,
                            Operation=4, Resource=5
BinderOwner              := Kind:u8,
                            Identity:(NominalTypeKey for 0/1,
                                      FunctionDeclarationIdentity for 2,
                                      CanonicalInstanceIndex:u32le for 3,
                                      EffectOperationKey for 4,
                                      NominalTypeKey for 5),
                            TypeParameterCount:u32le,
                            EffectVariableKinds:vector<u8>
TypeParameterRef         := BinderIndex:u32le, Ordinal:u32le
EffectVariableRef        := BinderIndex:u32le, Ordinal:u32le
```

Nominal/trait/instance/operation/resource binders come from their schema
declaration; nominal, trait, instance, and resource owners have a zero
effect-variable count by construction, while an operation may declare
Flexible effect variables. A resource binder identity is its exact
`ResourceDeclaration.Key`, is disjoint from a nominal binder with the same
key, and owns every declaration-ordered resource type parameter including a
phantom parameter.
function binders come from a `FunctionType` with nonempty declared type or
effect parameters and require a nonempty `SourceIdentity`. A generic schema
record and its signature must declare the same type/effect parameter IDs in
the same order. For
an Instance declaration the input identity carries its trait, argument heads,
and constraints. `encodeInterfaceTables` replaces that declaration's own raw
parameters by declaration-order local ordinals, sorts instances by the fully
expanded alpha-normalized identity, emits `CanonicalInstanceIndex`, and
returns `InstanceRemap`; callers never reproduce that algorithm. Sort the
remaining owners by `(Kind, canonical identity)`; map
raw type-parameter IDs to their
declaration-order ordinal and reject an ID owned by zero or multiple binders.
Function and operation binders use their declared
TypeParameters/EffectVariables order, including phantom parameters, and
must own every reachable raw occurrence; an occurrence absent from the
declared list or a tail kind differing from its declaration is an error.
Operation declarations permit only Flexible effect variables; Opaque sources
are conservative function-scheme facts, never caller-supplied operation
arguments. The isolated
`encodeOperationDeclarationGraph` permits those references, alpha-normalizes
them to canonical binder refs, and includes the fully qualified key,
constraints, parameters/ownership, result/ownership, and effect row. Its
FNV-1a hash is the declaration fingerprint used only by semantics, v2
catalogs, and specialization lookup. Runtime descriptor graphs are a separate
domain and are required to be closed/monomorphized: their binder table is
empty, every `ClosedEffectOperation` argument and substituted signature is
closed, and any `TypeParameterType` or open effect row produces
`OpenRuntimeDescriptor`.
Returned type-parameter/effect-variable mappings are sorted by raw source ID
for deterministic lookup; every schema-level constraint is encoded through
those mappings, and a missing source is an error rather than a raw-ID fallback.

Nested records encode fields in this exact order after applying that binder
map:

```text
ModuleIdentity           := CanonicalName:string
NominalTypeKey           := Module:ModuleIdentity, Name:string
SymbolIdentity           := Module:ModuleIdentity, Name:string
LocalFunctionIdentity    := Module:ModuleIdentity,
                            LexicalDeclarationPath:vector<u32le>
FunctionDeclarationIdentity := Kind:u8 (Symbol=0, Local=1),
                            Payload:(SymbolIdentity or LocalFunctionIdentity)
RecordField              := Name:string, Type:u32le
FunctionParameter        := Type:u32le, Ownership:u8
OwnedSlotField           := Type:u32le, Ownership:u8
TypeConstraint           := Parameter:TypeParameterRef, Trait:NominalTypeKey,
                            Arguments:vector<u32le>
EffectOperationKey       := Effect:NominalTypeKey, Operation:string
EffectOperationApplication := Key:EffectOperationKey,
                              TypeArguments:vector<u32le>,
                              EffectArguments:vector<u32le>
FunctionType             := TypeParameterCount:u32le,
                            EffectVariableKinds:vector<u8>,
                            Constraints:vector<TypeConstraint>,
                            Parameters:vector<FunctionParameter>,
                            Result:u32le, ResultContract:u8, Effects:u32le,
                            Convention:u8,
                            SourceIdentity:option<FunctionDeclarationIdentity>,
                            ExportedSymbol:option<SymbolIdentity>
EffectTailProjection     := Source:EffectVariableRef,
                            ExcludedOperations:
                              vector<EffectOperationApplication>
EffectRow                := Operations:vector<EffectOperationApplication>,
                            Tails:vector<EffectTailProjection>,
                            MayRaise:u8, MayCancel:u8
EffectOperationSignature:= Key:EffectOperationKey,
                            TypeParameterCount:u32le,
                            EffectVariableKinds:vector<u8>,
                            Constraints:vector<TypeConstraint>,
                            Parameters:vector<FunctionParameter>,
                            Result:u32le, ResultContract:u8, Effects:u32le
ClosedEffectOperation   := Key:EffectOperationKey,
                            TypeArguments:vector<u32le>,
                            EffectArguments:vector<u32le>,
                            Parameters:vector<FunctionParameter>,
                            Result:u32le, ResultContract:u8, Effects:u32le
```

Structural type records are `tag:u8, payload_byte_count:u64le, payload`; tags
are the variant index declared above and payloads are exactly:

```text
Primitive(0)     := PrimitiveType:u8
TypeParameter(1) := Parameter:TypeParameterRef
Tuple(2)         := Elements:vector<u32le>
Sequence(3)      := Element:u32le
Set(4)           := Element:u32le
Dictionary(5)    := Key:u32le, Value:u32le
Record(6)        := Fields:vector<RecordField>, RowRest:option<u32le>
Nominal(7)       := Key:NominalTypeKey, Arguments:vector<u32le>
Array(8)         := Element:u32le, Mutable:u8
Channel(9)       := Element:u32le
Promise(10)      := Element:u32le, ResultContract:u8, Effects:u32le
Cursor(11)       := Element:u32le, ElementContract:u8
CursorStep(12)   := Element:u32le, ElementContract:u8
OutcomeTaskGroup(13) := <empty payload>
Resource(14)     := Key:NominalTypeKey, Arguments:vector<u32le>
AbiOpaque(15)    := Kind:u8
OwnedSlotState(16) := Kind:u8, Fields:vector<OwnedSlotField>,
                      DropIdentity:FunctionDeclarationIdentity
Function(17)     := FunctionType
Sum(18)          := Alternatives:vector<u32le>
```

Every Resource argument participates in dependency ordering, cycle checks,
canonical remap, and post-remap re-normalization. Golden tests round-trip
parameterized resources, reject wrong resource arity, foreign/stale argument
IDs, and recursive anonymous
resource argument cycles, and prove different element/argument graphs remain
different when fingerprints collide.

An effect record and operation-signature record are each
`payload_byte_count:u64le, payload` using the layouts above. The descriptor/
fingerprint form of one isolated reachable graph is:

```text
encoding_version:u32le (=2)
binder_count:u32le, binder_records[binder_count]
type_count:u32le, type_records[type_count]
effect_count:u32le, effect_records[effect_count]
operation_count:u32le, operation_records[operation_count]
root_kind:u8 (type=0, effect=1, operation=2)
root_index:u32le
```

For a type or effect root, `operation_count` is exactly zero. For a runtime
operation root it is exactly one `ClosedEffectOperation`; declaration
fingerprinting uses the separate binder-aware
`encodeOperationDeclarationGraph` signature record above. Applications
inside every effect row encode a terminal fully qualified key plus their
ordered type/effect arguments; they never pull another operation signature
into the graph. Thus an operation whose residual row mentions itself does not
recursively embed its declaration. Semantic/interface verification must
resolve every key to either one local declaration or one imported declaration
and validate argument arity/constraints before lowering, but key encoding
itself never depends on where it resolves.

Counts above `UINT32_MAX`, unknown tags/discriminants, invalid booleans or
options, out-of-range indices, noncanonical order, and trailing bytes are
errors. To assign canonical indices, recursively expand each referenced type
and effect into the same payload with referenced records and canonical binder
refs substituted, sort the unique expanded byte keys lexicographically, and
then rewrite references to the resulting indices. Sort operation signatures
by their fully expanded bytes after type/effect reindexing. Nominal recursion
terminates at the
`NominalTypeKey`; it never expands a nominal declaration body. Fingerprints
are FNV-1a over this complete isolated-graph byte sequence, including version
and root and excluding only stored fingerprint fields.

Canonicalize record fields, effect applications, and every semantically
set-valued constraint vector by fully expanded bytes, rejecting duplicates
before hashing. Record fields sort by UTF-8 name and retain their optional
row-rest TypeId; projection/substitution may replace a row-rest parameter only
with another RecordType, merges fields by name, and rejects overlap rather
than choosing a side. An application's argument vectors remain declaration ordered;
the row's application set is sorted by key plus expanded arguments.
An effect row is the lossless normalized form of ACI join: known applications
are unioned, `MayRaise`/`MayCancel` are ORed, and every independent flexible
or opaque source is retained as an `EffectTailProjection`. Normalize each
tail's excluded complete operation applications, including ordered type and
effect arguments, then sort/deduplicate tails by
`(canonical binder ref, kind, excluded-application bytes)` without merging
distinct masks. This preserves shared sources and symbolic handler masks
across every arrow in one frozen scheme and permits a handler for
`E.op<Int>` to leave `E.op<String>` in the same tail.

Generic `EffectArguments` correspond, in declaration order, only to Flexible
effect-variable declarations. Substituting a tail `(source, exclusions)` with
its argument row first substitutes every type/effect argument in each excluded
application, removes only byte-identical known applications, unions those
complete exclusions into every nested tail, preserves raise/cancel facts, and
then renormalizes the ACI union. Opaque tails are never caller-substituted.
A row is closed exactly when `Tails` is empty; reachable opaque or
unsubstituted flexible tails are rejected before `GenericPrepared`/
runtime-descriptor emission. Phantom Flexible declarations still consume a
key argument position even when no tail references them.
Type-parameter declaration vectors retain their source-declared order
because that order defines explicit type-argument positions. Include
`MayRaise` and `MayCancel` in effect-row
equality, interning, formatting, remapping, and canonical bytes. An effect
application key is its fully qualified `EffectOperationKey`, never an
unqualified display string. Store interned
nodes in stable-address storage so a reference returned by `at()` or
`effect()` remains valid across later interning. Represent recursive ADTs
through `NominalTypeKey`; never insert a pointer cycle into the table.
`ModuleIdentity`, `NominalTypeKey`, `SymbolIdentity`, and every strong ID have
validated canonical forms, equality, ordering, and explicit hasher objects;
all `unordered_map` declarations in this plan name those hashers rather than
injecting partial specializations into `std`.
`TypeTable::validate` rejects direct TypeId/EffectRowId reference cycles,
including a record row rest or effect application/tail substitution argument
that reaches its containing record/row;
source-level recursive ADTs terminate through `NominalTypeKey` declarations
and runtime operation/residual-row recursion is represented later by Task
13's explicit SCC records, not a cyclic structural table.
Extend `ModuleIdentity` with value equality, lexicographic ordering, and
`ModuleIdentityHasher`; its constructor remains the sole validation boundary.

- [ ] **Step 4: Project solved inference graphs once per semantic model**

Replace the effect solver's lossy display strings with opaque solver atoms.
The solver deliberately remains ignorant of semantic types:

```cpp
using EffectAtomId = std::uint32_t;
struct EffectProjection {
  EffectVarId Variable;
  std::vector<EffectAtomId> ExcludedAtoms;
  bool Opaque;
};
struct EffectNormalForm {
  std::vector<EffectAtomId> KnownAtoms;
  std::vector<EffectProjection> Tails;
};
EffectRef EffectSolver::atoms(std::vector<EffectAtomId>);
EffectRef EffectSolver::mask(EffectRef, std::vector<EffectAtomId>);
EffectConstraintResult EffectSolver::addAtom(EffectRef, EffectAtomId);
EffectConstraintResult EffectSolver::equateAtoms(EffectAtomId,
                                                 EffectAtomId);
EffectAtomId EffectSolver::canonicalAtom(EffectAtomId) const;
```

`join`, `include`, `equate`, graph freeze/instantiate, and masks preserve those
IDs with the current ACI/multiple-tail semantics. The solver owns an atom
union-find: every normalization, known/excluded subtraction, constraint
decision, summary, and frozen-template write first maps through
`canonicalAtom`. `equateAtoms` transactionally merges two representatives,
invalidates/rebuilds affected caches, and rechecks deferred/conflicting
constraints before publishing. Type unification calls it whenever two solved
operation applications become equal; generalization may freeze a scheme only
after all of its reachable atoms are canonicalized. Instantiating a template
therefore starts with representative IDs, never a stale equivalence class.
They never compare or format an atom as a string. Define `EffectRefHasher`
beside `EffectRef` and use it in every effect-reference hash table.

`TypeChecker` owns the matching append-only catalog; published entries are
immutable and new instance-local atoms are staged transactionally:

```cpp
struct OperationEffectAtom {
  model::EffectOperationKey Key;
  std::vector<MonoTypePtr> TypeArguments;
  std::vector<EffectRef> EffectArguments;
};
struct MayRaiseEffectAtom {};
struct MayCancelEffectAtom {};
using SolvedEffectAtom = std::variant<OperationEffectAtom,
                                      MayRaiseEffectAtom,
                                      MayCancelEffectAtom>;
class EffectAtomCatalog final {
public:
  class AppendTransaction {
  public:
    EffectAtomId intern(SolvedEffectAtom Atom);
    void commit();
    ~AppendTransaction(); // rolls back unless committed
  };
  [[nodiscard]] AppendTransaction beginAppend();
  [[nodiscard]] bool valid(EffectAtomId Atom) const noexcept;
  [[nodiscard]] const SolvedEffectAtom &at(EffectAtomId Atom) const;
  [[nodiscard]] std::string format(EffectAtomId Atom) const;
};
```

An operation atom resolves to one qualified key plus ordered, fully solved
monotype and effect-root arguments. `MayRaise` and `MayCancel` are explicit
fact atoms and are illegal in a mask exclusion. The catalog formatter derives
diagnostic spellings; no display name re-enters the solver. Two applications
of the same generic key at different arguments remain distinct atoms. If
unification makes two raw IDs equal, the catalog maps both to the solver's one
representative and `equateAtoms` has already made masks/equality constraints
equivalence-aware; projection still deduplicates defensively. Every raw and
representative atom in a frozen graph must resolve exactly once.

Polymorphic schemes do not reuse those raw catalog atoms when instantiated.
Generalization computes a fixed-point closure beginning with every
`SchemeEffectRoot`: summarize the reachable graph; recursively traverse every
known/excluded atom's TypeArguments (including nested Arrows and record-row
rests), collecting free type variables and their latent effect roots; add its
EffectArguments; and repeat through newly reached atoms. Freeze that complete
graph and retain its referenced atom templates/free binders in the scheme.
`EffectSolver::instantiate` takes an `EffectAtomInstantiator` callback and
clones into private graph storage first. It supplies the callback with the
complete old-to-new EffectRef map; the TypeChecker combines that map with its
fresh type-variable substitution, clones each operation atom's ordered type
and effect arguments (including phantom positions) through an
`EffectAtomCatalog` append transaction, and returns an instance-local atom ID.
The solver rewrites every known/excluded atom to those new IDs, canonicalizes
them, and publishes only after every callback succeeds. Catalog and solver
transactions commit together or both roll back. Sibling scheme instantiations
therefore share no raw type/effect variables or atom IDs while remaining
structurally alpha-equivalent. Tests instantiate one masked generic scheme
twice at different type and effect arguments and prove independent
known/excluded sets and byte-distinct closed applications.

Before generalization, name resolution replaces every nominal application and
trait constraint in inference with a canonical `model::NominalTypeKey`.
`MonoType::App` retains that resolved key and `typechecker::Constraint` retains
the resolved trait key; short/display names are diagnostic data only. The
TypeChecker obtains local and imported declarations from the typed semantic
catalog described below and rejects an unresolved or ambiguous identity before
projection. Two modules may therefore define the same short nominal or trait
name without colliding. Tests project both local and imported same-spelling
types/traits and require distinct structural identities.

Replace inference's unordered/display-only sum handling with an explicit
`MonoType::Sum`/`TypeArena::make_sum` node whose alternatives are normalized
as an idempotent, commutative set after unifier resolution. Substitution,
occurs checks, free-variable/generalization walks, constraint traversal,
effect-root discovery, instantiation, pretty-printing, and imported-scheme
reconstruction visit every alternative deterministically. Exact Sum-to-Sum
unification compares the normalized alternative set; contextual use of one
alternative where a declared Sum is expected records one
`SumInjectionProjection` on that exact expression. Widening an already-Sum
value to a different superset is rejected until the language has an explicit
subtyping rule. `freezeSemanticBatch` projects the source and target together,
requires source membership in the normalized target, and persists a resolved
node fact. AST lowering consumes that fact exactly once into `InjectSumInst`;
it never infers a coercion from expected LLVM shape.

A typed pattern over a Sum emits `RuntimeTypeIsTest` plus
`RuntimeTypePayloadProjection`. Over a statically exact non-Sum input it folds
to true with an identity binding when the annotation is equal and to false
when the types are disjoint; it does not allocate an artificial Sum box. A
typed pattern with an open/overlapping unresolved relation is rejected during
semantics. Tests cover annotated function returns, branch joins, nested sums,
generic substitution, typed patterns on both Sum and exact values, and all
scheme/effect free-variable walks.

Extend `typechecker::TypeScheme` with an explicit declaration-ordered
`QuantifiedEffectVariables` vector of `{EffectVarId Variable,
EffectVariableKind Kind}` and replace its bare root vector with
`SchemeEffectRoot{MonoTypePtr Arrow, EffectRef Effect}` records. Inferred
binders use deterministic first occurrence order; parsed/imported binders
retain declaration order; all phantom binders remain in the vector. The
existing `quantified_vars` and `effect_graph` remain, while the scheme freezes
the `SchemeEffectRoot::Effect` values as one graph so shared and independent
sources survive. Collect one record per distinct Arrow node in deterministic
preorder across the body and every constraint, visiting all application,
tuple, record-field, and record-row-rest children; do not deduplicate records
merely because two Arrows share one EffectRef. Each record must equal its
Arrow's `arrow_effect`, every reachable Arrow has exactly one record, and no
unreachable/foreign root is accepted. Project with one
binder-aware transactional batch for the entire semantic-model construction:

```cpp
struct CallableTypeContract {
  compiler::typechecker::MonoTypePtr Arrow;
  std::vector<model::ParameterOwnership> ParameterOwnerships;
  model::ResultOwnership ResultContract;
  model::CallingConvention Convention;
  SourceRange Range;
};
using SchemeIdentity = model::FunctionDeclarationIdentity;
struct SchemeInstantiationProjection {
  SchemeIdentity Scheme;
  std::vector<compiler::typechecker::MonoTypePtr> TypeArguments;
  std::vector<compiler::typechecker::EffectRef> EffectArguments;
  SourceRange Range;
};
struct ResolvedOperationApplicationProjection {
  model::EffectOperationKey Key;
  std::vector<compiler::typechecker::MonoTypePtr> TypeArguments;
  std::vector<compiler::typechecker::EffectRef> EffectArguments;
  std::vector<compiler::typechecker::MonoTypePtr> ParameterTypes;
  std::vector<model::ParameterOwnership> ParameterOwnerships;
  compiler::typechecker::MonoTypePtr ResultType;
  model::ResultOwnership ResultContract;
};
struct PerformProjection {
  ResolvedOperationApplicationProjection Application;
  compiler::typechecker::MonoTypePtr ContinuationResultType;
  compiler::typechecker::EffectRef ResumeEffects;
  SourceRange Range;
};
struct HandlerClauseProjection {
  ResolvedOperationApplicationProjection Application;
  compiler::typechecker::MonoTypePtr SourceResumeType;
  compiler::typechecker::EffectRef ReinstatedResumeEffects;
  SourceRange Range;
};
using OperationOccurrenceProjection =
    std::variant<PerformProjection, HandlerClauseProjection>;
struct DeferredTraitEvidenceProjection {
  SchemeIdentity OwningScheme;
  compiler::typechecker::Constraint Constraint;
};
struct ResolvedTraitTargetProjection {
  SchemeIdentity SelectedTarget;
  std::vector<compiler::typechecker::MonoTypePtr> TargetTypeArguments;
  std::vector<compiler::typechecker::EffectRef> TargetEffectArguments;
};
using TraitSelectionEvidenceProjection =
    std::variant<DeferredTraitEvidenceProjection,
                 ResolvedTraitTargetProjection>;
struct TraitMethodSelectionProjection {
  model::NominalTypeKey Trait;
  std::string Method;
  std::vector<compiler::typechecker::MonoTypePtr> InstanceArguments;
  std::vector<compiler::typechecker::MonoTypePtr> MethodTypeArguments;
  std::vector<compiler::typechecker::EffectRef> MethodEffectArguments;
  TraitSelectionEvidenceProjection Evidence;
  SourceRange Range;
};
struct CollectionKeyOperationsProjection {
  TraitMethodSelectionProjection Hash;
  TraitMethodSelectionProjection Equals;
};
struct SumInjectionProjection {
  compiler::typechecker::MonoTypePtr Alternative;
  compiler::typechecker::MonoTypePtr Sum;
};
struct NodeSemanticProjection {
  const ast::AstNode *Node;
  compiler::typechecker::MonoTypePtr Type;
  compiler::typechecker::EffectRef Effects;
  std::optional<SchemeInstantiationProjection> Instantiation;
  std::optional<OperationOccurrenceProjection> OperationOccurrence;
  std::optional<TraitMethodSelectionProjection> TraitMethodUse;
  std::optional<CollectionKeyOperationsProjection> KeyOperations;
  std::optional<SumInjectionProjection> SumInjection;
  SourceRange Range;
};
using ProjectionDeclarationId = std::uint32_t;
struct InstanceScopeIdentityProjection {
  model::NominalTypeKey Trait;
  std::vector<compiler::typechecker::MonoTypePtr> Arguments;
  std::vector<compiler::typechecker::Constraint> Constraints;
};
using ProjectionDeclarationIdentity =
    std::variant<model::NominalTypeKey,
                 model::FunctionDeclarationIdentity,
                 InstanceScopeIdentityProjection,
                 model::EffectOperationKey>;
enum class DeclarationVisibility : std::uint8_t {
  Private = 0, Module = 1, Public = 2
};
struct DeclarationScopeProjection {
  ProjectionDeclarationId Id;
  std::optional<ProjectionDeclarationId> Parent;
  model::BinderOwnerKind Kind;
  ProjectionDeclarationIdentity Identity;
  std::span<const compiler::typechecker::TypeId> TypeParameters;
  std::span<const compiler::typechecker::QuantifiedEffectVariable>
      EffectVariables;
  std::span<const CallableTypeContract> CallableContracts;
  SourceRange Range;
};
struct FunctionSchemeProjection {
  ProjectionDeclarationId Scope;
  const compiler::typechecker::TypeScheme &Scheme;
  std::span<const model::ParameterOwnership> ParameterOwnerships;
  model::ResultOwnership ResultContract;
  model::CallingConvention Convention;
  std::optional<model::FunctionDeclarationIdentity> SourceIdentity;
  std::optional<model::SymbolIdentity> ExportedSymbol;
  DeclarationVisibility Access;
  std::span<const NodeSemanticProjection> NodeFacts;
  SourceRange Range;
};
struct OperationSchemeProjection {
  ProjectionDeclarationId Scope;
  model::EffectOperationKey Key;
  std::span<const compiler::typechecker::TypeId> TypeParameters;
  std::span<const compiler::typechecker::QuantifiedEffectVariable>
      EffectVariables;
  std::span<const compiler::typechecker::Constraint> Constraints;
  std::span<const compiler::typechecker::MonoTypePtr> ParameterTypes;
  std::span<const model::ParameterOwnership> ParameterOwnerships;
  compiler::typechecker::MonoTypePtr ResultType;
  model::ResultOwnership ResultContract;
  DeclarationVisibility Access;
  compiler::typechecker::EffectRef Effects;
  SourceRange Range;
};
struct ConstructorProjection {
  std::string Name;
  std::uint32_t Tag;
  std::span<const compiler::typechecker::MonoTypePtr> Fields;
  SourceRange Range;
};
struct NominalDeclarationProjection {
  ProjectionDeclarationId Scope;
  model::NominalTypeKey Key;
  std::span<const ConstructorProjection> Constructors;
  DeclarationVisibility Access;
  bool Opaque;
  SourceRange Range;
};
struct ResourceDeclarationProjection {
  ProjectionDeclarationId Scope;
  model::NominalTypeKey Key;
  std::span<const compiler::typechecker::TypeId> TypeParameters;
  model::ResourceAbiKind AbiKind;
  model::ResourceShareability Shareability;
  std::optional<std::string> TryRetainNativeSymbol;
  std::string ReleaseNativeSymbol;
  DeclarationVisibility Access;
  SourceRange Range;
};
struct TraitMethodProjection {
  ProjectionDeclarationId Scope;
  std::string Name;
  const compiler::typechecker::TypeScheme &Scheme;
  std::span<const model::ParameterOwnership> ParameterOwnerships;
  model::ResultOwnership ResultContract;
  model::CallingConvention Convention;
  std::optional<ResolvedTraitTargetProjection> DefaultTarget;
  SourceRange Range;
};
struct TraitDeclarationProjection {
  ProjectionDeclarationId Scope;
  model::NominalTypeKey Key;
  std::span<const compiler::typechecker::Constraint> Superclasses;
  std::span<const TraitMethodProjection> Methods;
  DeclarationVisibility Access;
  SourceRange Range;
};
struct TraitMethodBindingProjection {
  ProjectionDeclarationId InstanceScope;
  ProjectionDeclarationId MethodScope;
  std::string Method;
  ResolvedTraitTargetProjection Target;
  SourceRange Range;
};
struct TraitInstanceProjection {
  ProjectionDeclarationId Scope;
  model::NominalTypeKey Trait;
  std::span<const compiler::typechecker::MonoTypePtr> Arguments;
  std::span<const compiler::typechecker::Constraint> Constraints;
  std::span<const TraitMethodBindingProjection> Methods;
  DeclarationVisibility Access;
  SourceRange Range;
};
struct DeriveRequestProjection {
  model::NominalTypeKey Type;
  std::span<const model::NominalTypeKey> Traits;
  SourceRange Range;
};
struct SemanticProjectionBatch {
  std::span<const DeclarationScopeProjection> Scopes;
  std::span<const FunctionSchemeProjection> Functions;
  std::span<const OperationSchemeProjection> Operations;
  std::span<const NominalDeclarationProjection> Nominals;
  std::span<const ResourceDeclarationProjection> Resources;
  std::span<const TraitDeclarationProjection> Traits;
  std::span<const TraitInstanceProjection> Instances;
  std::span<const DeriveRequestProjection> Derivations;
};
struct ProjectedSchemeInstantiation {
  SchemeIdentity Scheme;
  std::vector<model::TypeId> TypeArguments;
  std::vector<model::EffectRowId> EffectArguments;
};
struct ProjectedOperationApplication {
  model::EffectOperationKey Key;
  std::vector<model::TypeId> TypeArguments;
  std::vector<model::EffectRowId> EffectArguments;
  std::vector<model::FunctionParameter> Parameters;
  model::TypeId ResultType;
  model::ResultOwnership ResultContract;
};
struct ProjectedPerformFact {
  ProjectedOperationApplication Application;
  model::TypeId ContinuationResultType;
  model::EffectRowId ResumeEffects;
};
struct ProjectedHandlerClauseFact {
  ProjectedOperationApplication Application;
  model::TypeId SourceResumeType;
  model::EffectRowId ReinstatedResumeEffects;
};
using ProjectedOperationOccurrence =
    std::variant<ProjectedPerformFact, ProjectedHandlerClauseFact>;
struct ProjectedTraitMethodUseFact {
  model::NominalTypeKey Trait;
  std::string Method;
  std::vector<model::TypeId> InstanceArguments;
  std::vector<model::TypeId> MethodTypeArguments;
  std::vector<model::EffectRowId> MethodEffectArguments;
  model::TraitSelectionEvidence Evidence;
};
struct ProjectedCollectionKeyOperations {
  ProjectedTraitMethodUseFact Hash;
  ProjectedTraitMethodUseFact Equals;
};
struct ProjectedSumInjection {
  model::TypeId Alternative;
  model::TypeId Sum;
};
struct ProjectedNodeFact {
  const ast::AstNode *Node;
  model::TypeId Type;
  model::EffectRowId Effects;
  std::optional<ProjectedSchemeInstantiation> Instantiation;
  std::optional<ProjectedOperationOccurrence> OperationOccurrence;
  std::optional<ProjectedTraitMethodUseFact> TraitMethodUse;
  std::optional<ProjectedCollectionKeyOperations> KeyOperations;
  std::optional<ProjectedSumInjection> SumInjection;
};
struct ProjectedFunction {
  ProjectionDeclarationId Scope;
  model::TypeId Signature;
  DeclarationVisibility Access;
  std::vector<model::TypeParameterId> InheritedTypeParameters;
  std::vector<model::EffectVariableDeclaration> InheritedEffectVariables;
  std::vector<model::TypeParameterId> DeclaredTypeParameters;
  std::vector<model::EffectVariableDeclaration> DeclaredEffectVariables;
  std::vector<ProjectedNodeFact> NodeFacts;
};
struct ProjectedDeclarationScope {
  model::BinderOwnerKind Kind;
  model::CanonicalBinderOwnerIdentity Identity;
  std::optional<std::uint32_t> Parent;
  std::vector<model::TypeParameterId> TypeParameters;
  std::vector<model::EffectVariableDeclaration> EffectVariables;
};
struct ProjectedConstructor {
  std::string Name;
  std::uint32_t Tag;
  std::vector<model::TypeId> Fields;
};
struct ProjectedNominalDeclaration {
  model::NominalTypeKey Key;
  std::vector<model::TypeParameterId> TypeParameters;
  std::vector<ProjectedConstructor> Constructors;
  DeclarationVisibility Access;
  bool Opaque;
};
struct ProjectedResourceDeclaration {
  model::NominalTypeKey Key;
  std::vector<model::TypeParameterId> TypeParameters;
  model::ResourceAbiKind AbiKind;
  model::ResourceShareability Shareability;
  std::optional<std::string> TryRetainNativeSymbol;
  std::string ReleaseNativeSymbol;
  DeclarationVisibility Access;
};
struct ProjectedTraitMethod {
  std::string Name;
  model::TypeId Signature;
  std::optional<model::TraitTargetApplication> DefaultTarget;
};
struct ProjectedTraitDeclaration {
  model::NominalTypeKey Key;
  std::vector<model::TypeParameterId> TypeParameters;
  std::vector<model::TypeConstraint> Superclasses;
  std::vector<ProjectedTraitMethod> Methods;
  DeclarationVisibility Access;
};
struct ProjectedTraitMethodBinding {
  std::string Method;
  model::TraitTargetApplication Target;
};
struct ProjectedTraitInstance {
  model::NominalTypeKey Trait;
  std::vector<model::TypeParameterId> TypeParameters;
  std::vector<model::TypeId> Arguments;
  std::vector<model::TypeConstraint> Constraints;
  std::vector<ProjectedTraitMethodBinding> Methods;
  DeclarationVisibility Access;
};
struct ProjectedOperationDeclaration {
  model::EffectOperationSignature Signature;
  DeclarationVisibility Access;
};
struct ProjectedDeriveRequest {
  model::NominalTypeKey Type;
  std::vector<model::NominalTypeKey> Traits;
  SourceRange Range;
};
struct ProjectedInterfaceSeed {
  std::vector<ProjectedDeclarationScope> Scopes;
  std::vector<ProjectedNominalDeclaration> Nominals;
  std::vector<ProjectedResourceDeclaration> Resources;
  std::vector<ProjectedTraitDeclaration> Traits;
  std::vector<ProjectedTraitInstance> Instances;
  std::vector<ProjectedOperationDeclaration> Operations;
  std::vector<ProjectedDeriveRequest> Derivations;
};
struct ProjectedSemanticBatch {
  std::vector<ProjectedFunction> Functions;
  std::vector<model::EffectOperationSignature> Operations;
  ProjectedInterfaceSeed InterfaceSeed;
};
enum class StructuralProjectionErrorCode : std::uint8_t {
  InvalidInput, WrongArity, WrongArrowShape, ForeignEffect,
  MissingEffectAtom, UnboundBinder, BinderKindMismatch,
  MissingCallableContract, DuplicateCallableContract, OpenValueType,
  InvalidRecordRow, InvalidParent, CyclicParent, DuplicateNode,
  UnresolvedIdentity, AmbiguousInstantiation
};
struct StructuralProjectionError {
  StructuralProjectionErrorCode Code;
  SourceRange Range;
  std::string Path;
  std::string Message;
};

class StructuralTypeProjection final {
public:
  StructuralTypeProjection(
      model::TypeTable &Types, const model::ModuleIdentity &Module,
      const compiler::typechecker::EffectSolver &Effects,
      const compiler::typechecker::EffectAtomCatalog &EffectAtoms);
  std::expected<model::TypeId, StructuralProjectionError>
  freezeClosedMonotype(compiler::typechecker::MonoTypePtr Type,
                       SourceRange Range);
  std::expected<ProjectedSemanticBatch,
                std::vector<StructuralProjectionError>>
  freezeSemanticBatch(const SemanticProjectionBatch &Batch);
private:
  struct ProjectionScope;
  std::expected<model::EffectRowId, StructuralProjectionError>
  freezeEffect(compiler::typechecker::EffectRef Effect,
               ProjectionScope &Scope, SourceRange Range,
               std::string_view Path);
  model::TypeTable &Types_;
  model::ModuleIdentity Module_;
  const compiler::typechecker::EffectSolver &Effects_;
  const compiler::typechecker::EffectAtomCatalog &EffectAtoms_;
  std::unordered_map<const compiler::typechecker::MonoType *, model::TypeId>
      ClosedTypeMemo_;
};
```

`freezeSemanticBatch` first validates unique raw declaration-scope IDs, owner
kinds, a finite acyclic declaration forest, unique AST-node ownership,
operation keys, and derive-request targets. The forest spans Nominal,
Trait, Instance, Function, and Operation owners; constructor/method/function
scopes may inherit their declaring nominal/trait/instance/function scope. An
instance method binding declares no binder owner.
Nominal, Trait, and Instance owners may declare type parameters only;
`DeclarationScopeProjection::EffectVariables` must be empty for those three
kinds. Only Function and Operation owners declare effect variables, and an
operation permits only Flexible declarations. A nested function may inherit
the type parameters of enclosing nominal/trait/instance owners and the
type/effect parameters of enclosing functions, but no schema encoder invents
an effect-binder vector for a nominal, trait, or instance. Projection and v2
decode reject a nonempty forbidden vector before publishing any record.
Projection evaluates an instance method target in a transient composite view:
ordinary ancestry is the Instance scope,
while the referenced trait method's own type/effect binders are read-only
aliases to the already-projected MethodScope binders. The maps must be
disjoint, and copied source binder kind/arity must exactly match the trait
method declaration. Target-template references therefore point only to the
serialized Instance and trait-method Function binder declarations; no
synthetic/local binding owner is emitted.
It then creates all private scopes and installs every declaration's own binders
in declaration order before projecting any field, signature, constraint,
target application, node fact, row, or mask. Instance identities are projected
only after their private binder maps exist; canonical owner-identity uniqueness
is checked after that projection and before commit. A child scope has read-only access
to every ancestor mapping and adds only its own declarations; an inherited
binder is referenced by its ancestor's destination ID and is never redeclared.
Own binders are retained even when phantom. `ProjectedFunction` nevertheless
returns the complete effective environment explicitly: inherited binders in
outermost-to-parent declaration order, then the function's own declarations.
Task 15 uses that environment for local generic extraction/keying; it never
tries to infer ancestry from reachable type occurrences. All scopes remain
alive until the complete semantic batch has projected successfully, then are
discarded together. This is the only API that projects open semantic values;
callers cannot hold or reuse a scope token.

Each source derive request is copied from
`SemanticProjectionBatch::Derivations` into
`ProjectedInterfaceSeed::Derivations` in source order after its nominal target
and every requested trait identity resolve in the staged declaration/catalog
view. Duplicate traits, a non-derivable trait, a foreign/unknown target, or a
request whose range is outside the projected source is a ranged projection
error. Task 15's transactional `runDerivations` consumes only this projected
owning list; it never rescans AST. Task 6 supplies the reusable typed
pattern/body synthesis helper but does not yet own the interface seed or trait
resolver.
syntax or reconstructs derive intent from generated names.

The internal scope is:

```cpp
struct StructuralTypeProjection::ProjectionScope {
  ProjectionDeclarationId Id;
  const ProjectionScope *Parent;
  model::BinderOwnerKind Kind;
  model::CanonicalBinderOwnerIdentity Identity;
  std::vector<model::TypeParameterId> DeclaredTypes;
  std::vector<model::EffectVariableDeclaration> DeclaredEffects;
  std::unordered_map<compiler::typechecker::TypeId,
                     model::TypeParameterId> TypeBinders;
  std::unordered_map<compiler::typechecker::EffectVarId,
                     model::EffectVariableId> EffectBinders;
  std::unordered_map<compiler::typechecker::MonoTypePtr,
                     const CallableTypeContract *> CallableContracts;
  std::unordered_map<const compiler::typechecker::MonoType *, model::TypeId>
      NonArrowMemo;
  std::unordered_map<compiler::typechecker::EffectRef, model::EffectRowId,
                     compiler::typechecker::EffectRefHasher> EffectMemo;
};
```

The batch is staged against a cloned append transaction and publishes no type,
effect, declaration, operation, or node fact on error. No binder map or context-sensitive
memo survives the batch or a failed projection. `ClosedTypeMemo_` is used only
by the empty-scope closed-value API. `NonArrowMemo` is never consulted for
Arrow nodes. Function types are
interned from their complete projected `FunctionType` record, including
ownership, convention, symbols, effects, and active binder context. Projecting
one shared Arrow under two different legal callable contracts must therefore
produce distinct IDs. Every failure is returned with a stable code, producer
range, structural path, and message; `SemanticModel` converts it to the normal
ranged diagnostic stream. Projection never throws, returns an invalid ID, or
falls back to a raw/display identity.

`PromiseType` stores the promised Success type, its result-ownership contract,
and the complete effect row that an await may re-emit. It is not merely an
element container: await lowering and task completion validate all three
fields, and canonical encoding/remapping follows the row graph.

Each function projection traverses its scheme body, constraints, node facts,
instantiations, and all scheme effect roots through its one scope/memo. Its
root `ParameterOwnerships.size()` peels exactly that
many source Arrow nodes. Every intermediate partial-application Arrow must
have a closed empty latent row; the last peeled Arrow supplies the emitted
`FunctionType::Effects`. A remaining Arrow result is a returned function and
must have its own `CallableTypeContract`. Every other reachable Arrow in a
parameter, constraint, operation signature, or nested result likewise has
exactly one contract; the projector never guesses its arity, ownership, result
contract, or calling convention. It verifies parameter ownership arity, arrow
shape, declared binder ownership/kind, and exact Arrow-to-effect-root
agreement, consuming every root record once.
It emits own declarations even for phantom binders. Operation projection applies the
same binder, constraint, ownership, atom-resolution, and residual-row rules to
one operation declaration rather than maintaining a second projection path.
`freezeEffect` resolves every operation atom through the catalog and
recursively projects its ordered type/effect arguments, maps fact atoms to
`MayRaise`/`MayCancel`, and retains every independent tail and shared raw
source from `Effects_.summarize(Effect)`. Every reference must belong to that
solver. Every exclusion must resolve to an operation atom; facts cannot be
masked. Opaque variables project as Opaque tails and may never be
caller arguments. A catalog miss, foreign root, unbound binder, or binder-kind
disagreement is a ranged semantic error rather than a display-string fallback.

Each identifier, operator, trait-method, and first-class function use that
instantiates a scheme carries one `SchemeInstantiationProjection` produced by
the TypeChecker. Its type and every Flexible effect argument are recorded in
declaration order, including phantoms, and projected in the use's owning
scope. This cutover does not add source type/effect-application syntax or
binder defaults. Therefore an argument must be solved from an actual
occurrence in the declaration/constraint and the use's ordinary expected
type; a truly phantom binder is necessarily unsolved at a source use and
produces a ranged ambiguity diagnostic. Projection and AST lowering never
reconstruct, guess, or drop an argument. The returned fact is the sole input
to Task 15's `GenericInstantiation`. Source tests cover two fully constrained
instantiations of local/imported calls and first-class values; direct semantic,
Typed IR, and v2 fixtures cover declaration-ordered unused type/effect binders
and prove distinct explicit specialization keys.

`ProjectedSemanticBatch::InterfaceSeed` owns the complete local semantic
declaration overlay: nominal opacity/constructors, traits/superclasses/methods/
default targets, and instances/constraints/method bindings, all referring to
the same projected `TypeTable`. Task 15 extends that seed with exact import
alias/selective/wildcard/reexport and export metadata and uses it as the sole
source for local trait resolution and interface construction. Runtime AST/IR
lowering may retain only runtime-relevant projections; it must not reconstruct
interface declarations later. Tests include a constructor field using a
nominal-owned phantom/open-row binder, a generic trait method using
trait-owned plus method-owned binders, an instance target using an
instance-owned binder, and atomic rollback after a failure in each category.
Its `Scopes` are parent-before-child canonical destination records and retain
every phantom binder owner. Source `deriving` names are resolved to supported
fully qualified trait keys before commit; requested order is kept only for
diagnostics, while output deduplicates/canonicalizes by trait key. Visibility
is copied on every function, operation, nominal, trait, and instance header.
Task 15's public-slice builder filters by visibility/ABI reachability, computes
the retained scope closure, and passes those exact declarations to
`encodeInterfaceTables`; it never reconstructs binder ownership from type
occurrences.

Deferred trait evidence stores the complete canonical structural constraint,
not a producer-order integer. Projection normalizes and validates that this
constraint occurs exactly once in the owning scheme; alpha-renaming or
permuting the TypeChecker's constraint insertion order therefore cannot
retarget evidence or change fragment bytes. Resolved evidence stores the
complete target declaration and ordered type/effect arguments. The same
`TraitSelectionEvidence` is persisted into Typed IR and is revalidated after
every remap/substitution.

The public `freezeClosedMonotype` accepts only a closed non-Arrow value type. Open
variables, record row variables, or any Arrow are errors outside the
binder/contract-aware function or operation entry points.

`MRecord::row_rest` projects through the same type-binder map into
`RecordType::RowRest`; it is never discarded or rendered into the field list.
The structural verifier requires a row rest to resolve to a declared row
parameter or another record, rejects duplicate/overlapping fields after
substitution, and requires every record reachable from executable IR to be
closed by `GenericPrepared`.

Change `NodeSemantics` to carry `model::TypeId Type`,
`model::EffectRowId EffectRow`, and an optional owning
`ProjectedSchemeInstantiation Instantiation`,
`ProjectedOperationOccurrence`, and
`ProjectedTraitMethodUseFact`, plus optional
`ProjectedCollectionKeyOperations KeyOperations` and
`ProjectedSumInjection SumInjection`. Construct it only by moving the
corresponding `ProjectedNodeFact` returned by the batch; no later per-node
projection is permitted. Keep its existing `InferredType` and `Effects`
display strings only as a temporary read-only legacy projection until Task 17.
Define `BindingIdHasher` beside `BindingId` and use it in every binding-keyed
unordered container.
Add:

```cpp
[[nodiscard]] const model::TypeTable &types() const noexcept;
// Compiler-pipeline only: shares the same append-only arena with Typed IR.
[[nodiscard]] std::shared_ptr<model::TypeTable>
sharedTypeArena() const noexcept;
[[nodiscard]] std::shared_ptr<const SourceManager> sources() const noexcept;
[[nodiscard]] std::optional<BindingId>
bindingFor(const ast::AstNode *Node) const noexcept;
[[nodiscard]] const ProjectedSchemeInstantiation *
instantiationFor(const ast::AstNode *Node) const noexcept;
[[nodiscard]] const ProjectedOperationOccurrence *
operationOccurrenceFor(const ast::AstNode *Node) const noexcept;
[[nodiscard]] const ProjectedTraitMethodUseFact *
traitMethodUseFor(const ast::AstNode *Node) const noexcept;
[[nodiscard]] const ProjectedCollectionKeyOperations *
keyOperationsFor(const ast::AstNode *Node) const noexcept;
[[nodiscard]] const ProjectedSumInjection *
sumInjectionFor(const ast::AstNode *Node) const noexcept;
[[nodiscard]] std::span<const model::EffectOperationSignature>
effectOperations() const noexcept;
```

Every occurrence and node fact must use the same projector so quantified type
variables, effect variables, shared effect tails, masks, and record rows retain
consistent structural identity across one module.
For a Sum injection, that transaction projects Alternative and Sum in the
same binder scope, requires Sum to be the normalized stored `SumType`, and
proves Alternative is exactly one member before publishing either ID. Missing,
duplicate, nonmember, singleton-marker, foreign-node, and range-mismatched
facts fail the whole batch unchanged. Semantic-model move/accessor,
AST-consumption, parser/remap, and rollback tests cover the field explicitly.
`SemanticModel` owns the projected table through one non-null
`std::shared_ptr<model::TypeTable>` and exposes mutation only through the
compiler-internal `sharedTypeArena()` handoff. `TypeTable` is an append-only,
compilation-thread-confined intern arena with stable-address records: later IR
specialization may intern new closed nodes, but can neither replace nor mutate
an existing node. Semantic facts therefore remain immutable and valid while a
lowered module shares and extends the arena. Public semantic clients receive
only `types() const`; no copy, move, or raw-reference lifetime bridge exists.
Because Yona currently has no effect-declaration syntax, semantic analysis
builds this operation table by unifying every resolved qualified `perform`
site and matching handler clause in the module. The key is the canonical
module/effect/operation identity; parameter/result ownership, structural
types, and the declaration's residual effect row must agree at every
occurrence. Contextual perform/handler resume facts are checked separately as
described below.
An unconstrained or disagreeing occurrence is a ranged type error, never a
default-to-Int signature. Imported occurrences use the exact v2 operation
signature installed by Task 15. The interprocedural solver propagates
`MayRaise` and `MayCancel` through calls and open rows; unknown/native calls
are conservative unless their declared signature proves otherwise.

Every resolved `perform` and handler-clause occurrence additionally owns one
`ProjectedOperationOccurrence`. Both variants contain the common resolved
application: declaration key, all declaration-ordered type/effect arguments
(including phantoms), and substituted parameter/result contracts. Only a
perform adds its enclosing CFG continuation-result type and exact suffix row;
only a handler clause adds its contextual source resume `R -> B` and the row
that reinstalls that particular handler. Semantic analysis unifies only the
common application against the one operation declaration. Resume facts agree
within one handle/group, not globally: two handlers of the same application
may return different `B`, and a perform outside either has no source resume
binding. AST lowering consumes the variant directly to construct
`OperationUse`, handler masks, and dispatch groups; it never derives an
application from the containing expression row. Tests include two such
different-result handlers and a plain perform. Likewise,
every constrained identifier/operator/derived/first-class method occurrence
owns one `ProjectedTraitMethodUseFact` with the fully qualified method,
ordered instance/method arguments, and either an exact closed selected target
plus its ordered specialization arguments or deferred evidence naming the
owning open scheme's canonical structural constraint. Type checking validates deferred uses
against that declared constraint without inventing an instance. Task 15
substitutes and resolves through the same catalog; it requires equality for an
already-resolved fact and validates the exact constraint/trait/method before
replacing deferred evidence. A constrained `f a` specialized to two different
instances must select two different targets.
Every set or dictionary construction/update source node separately persists
the exact Hash and Eq selections for its element/key type in
`ProjectedCollectionKeyOperations`; both use the same projection and evidence
rules. Type inference creates these facts while checking the collection, so
AST/Typed IR lowering never guesses trait names from a type or silently selects
builtin pointer/hash behavior. Generic facts may be deferred to their owning
scheme; closed facts name exact targets.
Tests cover phantom type/effect operation binders, an effectful resume, an
operator routed through a superclass, and a phantom generic method target.

Handler inference commits to deep, one-shot resumptions. For a handled body
of type `A`, whole handle type `B`, and operation result `R`, the source resume
binding has structural type `R -> B` with the solver-derived residual row; the
internal raw suffix `R -> A` is never exposed to source code. Only the handled
body row is masked by the handler. Handler-clause and return-clause rows join
the residual row without that mask, so a same-operation perform lexically in
either clause remains visible to an outer handler, while the separately
recorded resume row reinstalls the handler around resumed body execution.
Ownership facts mark the resume binding linear and reject duplication or a
second application rather than promising multi-shot cloning.

- [ ] **Step 5: Wire components and verify**

Add `StructuralType.cpp` and `CanonicalEncoding.cpp` to `yona_model`,
projection to `yona_semantics`, and both tests to `tests`, then run:

```bash
cmake --preset x64-debug-linux
cmake --build --preset build-debug-linux --target tests -j2
./out/build/x64-debug-linux/tests -tc='Structural type*,Semantic model structural type*'
git diff --check
```

Expected: all focused cases pass; `yona_interface` still links only
`yona_model` and does not acquire a Typed IR dependency.

- [ ] **Step 6: Commit the structural boundary**

```bash
git add include/yona/Model/StructuralType.h src/Model/StructuralType.cpp \
  include/yona/Model/CanonicalEncoding.h src/Model/CanonicalEncoding.cpp \
  include/yona/Model/EffectSolver.h src/Model/EffectSolver.cpp \
  include/yona/Model/InferType.h include/yona/Model/TypeArena.h \
  src/Model/TypeArena.cpp include/yona/Semantics/Unification.h \
  src/Semantics/Unification.cpp \
  include/yona/Model/ModuleIdentity.h src/Model/ModuleIdentity.cpp \
  include/yona/Semantics/StructuralTypeProjection.h \
  src/Semantics/StructuralTypeProjection.cpp \
  include/yona/Semantics/TypeChecker.h src/Semantics/TypeChecker.cpp \
  include/yona/Semantics/SemanticModel.h src/Semantics/SemanticModel.cpp \
  test/Model/StructuralTypeTest.cpp test/Model/EffectSolverTest.cpp \
  test/Semantics/TypeCheckerTest.cpp test/Semantics/SemanticModelTest.cpp \
  cmake/YonaComponents.cmake CMakeLists.txt
git add docs/superpowers/plans/2026-09-01-typed-ir-codegen-rewrite.md
git commit -m "feat: add canonical structural types"
```

### Task 3: Replace the bootstrap container with canonical SSA Typed IR

**Files:**

- Create: `include/yona/TypedIr/Ids.h`
- Create: `include/yona/TypedIr/Representation.h`
- Create: `include/yona/TypedIr/Instruction.h`
- Create: `include/yona/TypedIr/Verifier.h`
- Create: `src/TypedIr/Verifier.cpp`
- Create: `include/yona/TypedIr/Printer.h`
- Create: `src/TypedIr/Printer.cpp`
- Create: `include/yona/TypedIr/Parser.h`
- Create: `src/TypedIr/Parser.cpp`
- Create: `test/TypedIr/BuilderTest.cpp`
- Create: `test/TypedIr/VerifierTest.cpp`
- Create: `test/TypedIr/PrinterParserTest.cpp`
- Create: `test/TypedIr/PropertyTest.cpp`
- Create: `scripts/run-focused-tests.py`
- Create: `test/CMake/focused_test_runner_contract.py`
- Replace: `include/yona/TypedIr/TypedIr.h`
- Replace: `src/TypedIr/TypedIr.cpp`
- Replace: `include/yona/TypedIr/Builder.h`
- Replace: `src/TypedIr/Builder.cpp`
- Replace: `test/TypedIr/TypedIrTest.cpp`
- Modify: `cmake/YonaComponents.cmake`
- Modify: `CMakeLists.txt`

**Interfaces:**

- Consumes: Task 2's `model::TypeTable`, `TypeId`, and `EffectRowId`.
- Produces: strong Typed IR IDs, `ModulePhase`, SSA `Module`, `Function`,
  `Block`, `Value`, `Instruction`, `Terminator`, ID-only `Builder`,
  `verifyModule`, `printModule`, and `parseModule`.
- Invariant: no object returned by insertion is a reference or pointer that a
  later vector insertion can invalidate.

- [ ] **Step 1: Write red core/builder/verifier tests**

Use explicit malformed modules rather than testing only happy paths:

```cpp
TEST_CASE("Typed IR builder: block arguments are typed phi values") {
  auto Fixture = makeScalarModule();
  auto &Builder = Fixture.Builder;
  const auto Function = Builder.addFunction(Fixture.intToIntFunction());
  const auto Merge = Builder.createBlock(
      Function, {.DebugName = "merge", .Range = Fixture.Range});
  const auto Result = Builder.addBlockArgument(
      Function, Merge,
      {.Type = Fixture.Int, .Ownership = OwnershipKind::Trivial,
       .DebugName = "result", .Range = Fixture.Range});
  Builder.setTerminator(Function, Merge, Return{Result}, Fixture.Range);
  const auto Module = Builder.finish(ModulePhase::Canonical);
  CHECK(verifyModule(Module).has_value());
}

TEST_CASE("Typed IR verifier rejects non-dominating values") {
  auto Module = makeNonDominatingUseModule();
  const auto Result = verifyModule(Module);
  REQUIRE_FALSE(Result.has_value());
  CHECK(hasRule(Result.error(), VerificationRule::Dominance));
}

TEST_CASE("Typed IR verifier: transferred is not a value ownership kind") {
  CHECK(valueOwnershipKindName(OwnershipKind::Owned) == "owned");
  CHECK(enumCount<OwnershipKind>() == 3);
}
```

Cover invalid IDs, missing/double terminators, duplicate definitions,
same-block use-before-definition, wrong successor arity/type/ownership, wrong
condition/return/call types, definition/declaration/linkage mismatches,
missing/invalid block and terminator ranges, phase-illegal operations, and
legal unreachable blocks.

Create top-level sentinel cases named exactly `Typed IR core: strong IDs reject
cross-domain use`, `Typed IR text: canonical modules round trip byte-for-byte`,
and `Typed IR property: seeded well-typed modules round trip`. Together with
the named builder/verifier cases above, these make every positive Task 3
focused-test prefix independently reachable.

- [ ] **Step 2: Run the core tests and confirm bootstrap API mismatch**

Run:

```bash
cmake --build --preset build-debug-linux --target tests -j2
./out/build/x64-debug-linux/tests -tc='Typed IR core*,Typed IR builder*,Typed IR verifier*'
```

Expected: compile failures for `BlockId`, `ModulePhase`, and `verifyModule`.

- [ ] **Step 3: Define strong IDs and the canonical storage model**

`Ids.h` includes `yona/Model/StructuralType.h` and reuses the one model-level
implementation; it defines no second ID storage or hasher. Inside
`namespace yona::typed_ir` it provides only these forwarding aliases:

```cpp
template <class Tag> using StrongId = model::StrongId<Tag>;
using StrongIdHasher = model::StrongIdHasher;

using FunctionId = StrongId<struct FunctionIdTag>;
using BlockId = StrongId<struct BlockIdTag>;
using ValueId = StrongId<struct ValueIdTag>;
using EdgeId = StrongId<struct EdgeIdTag>;
using GlobalId = StrongId<struct GlobalIdTag>;
using NominalTypeId = StrongId<struct NominalTypeIdTag>;
using ConstructorId = StrongId<struct ConstructorIdTag>;
using PatternId = StrongId<struct PatternIdTag>;
using MatchPlanId = StrongId<struct MatchPlanIdTag>;
using DecisionNodeId = StrongId<struct DecisionNodeIdTag>;
using CleanupRegionId = StrongId<struct CleanupRegionIdTag>;
using CallableDescriptorId = StrongId<struct CallableDescriptorIdTag>;
using RecursiveClosureGroupDescriptorId =
    StrongId<struct RecursiveClosureGroupDescriptorIdTag>;
using OperationId = StrongId<struct OperationIdTag>;
using OperationInstanceId = StrongId<struct OperationInstanceIdTag>;
using RuntimeEffectRowId = StrongId<struct RuntimeEffectRowIdTag>;
using HandlerBoundaryDescriptorId =
    StrongId<struct HandlerBoundaryDescriptorIdTag>;
using TryBoundaryDescriptorId = StrongId<struct TryBoundaryDescriptorIdTag>;
using ContinuationBoundaryDescriptorId =
    std::variant<HandlerBoundaryDescriptorId, TryBoundaryDescriptorId>;
using BoundaryContextContractId =
    StrongId<struct BoundaryContextContractIdTag>;
using BoundaryRequirementSetId =
    StrongId<struct BoundaryRequirementSetIdTag>;
class ModuleMutationDomain final {
  friend class Builder;
  explicit ModuleMutationDomain(model::IdDomain Identity)
      : Identity_(Identity) {}
public:
  [[nodiscard]] model::IdDomain identity() const noexcept { return Identity_; }
private:
  model::IdDomain Identity_;
};
class FunctionLocalDomain final {
  friend class Builder;
  explicit FunctionLocalDomain(model::IdDomain Identity)
      : Identity_(Identity) {}
public:
  [[nodiscard]] model::IdDomain identity() const noexcept { return Identity_; }
private:
  model::IdDomain Identity_;
};
using ControlRegionId = StrongId<struct ControlRegionIdTag>;
using GeneratorId = StrongId<struct GeneratorIdTag>;
using ContinuationSegmentId = StrongId<struct ContinuationSegmentIdTag>;
using ContinuationTransitionTableId =
    StrongId<struct ContinuationTransitionTableIdTag>;
```

Compile-time tests assert `typed_ir::StrongId<Tag>` is exactly the
`model::StrongId<Tag>` implementation and both use
`model::StrongIdHasher`, while distinct model/Typed-IR tag types remain
non-convertible and a foreign-domain ordinal is still rejected at runtime.

Define the foundation records in `TypedIr.h` and `Instruction.h`:

```cpp
enum class ModulePhase {
  Canonical, GenericPrepared, PatternCanonical, AsyncPlanned,
  GeneratorLowered, ControlFlow, AsyncPrepared, CleanupPrepared,
  EffectPrepared, ClosureConverted,
  OperationInstantiated, EffectOutlined,
  TailCallsLowered, AcceleratorSelected,
  ControlOutcomeLowered, RepresentationSelected, OwnershipLowered,
  CleanupLowered, LlvmReady
};
enum class OwnershipKind { Trivial, Borrowed, Owned };
enum class BorrowProvenanceKind : std::uint8_t {
  BorrowParameter = 0,
  StaticLifetime = 1,
  OwnedValue = 2,
  ScopedUniqueLoan = 3,
  ScopedUniqueLoanParameter = 4
};
struct BorrowProvenance {
  BorrowProvenanceKind Kind;
  std::optional<ValueId> Root;
};
enum class PhysicalRepresentation {
  Unselected, NoCarrier, I1, I8, I32, I64, F64,
  ManagedPointer, UnmanagedPointer, AbiWord, AbiValue
};
enum class ValueOrigin { Parameter, BlockArgument, Constant, Instruction };
enum class UnaryOpcode { NegateInt, NegateFloat, LogicalNot, BitwiseNot };
enum class BinaryOpcode {
  AddInt, AddFloat, SubtractInt, SubtractFloat, MultiplyInt, MultiplyFloat,
  DivideIntSigned, DivideFloat, RemainderIntSigned, Equal, NotEqual,
  LessIntSigned, LessFloatOrdered, LessEqualIntSigned, LessEqualFloatOrdered,
  GreaterIntSigned, GreaterFloatOrdered, GreaterEqualIntSigned,
  GreaterEqualFloatOrdered
};

struct UnitLiteral {};
struct BoolLiteral { bool Value; };
struct ByteLiteral { std::uint8_t Value; };
struct CharLiteral { char32_t Value; };
struct IntLiteral { std::int64_t Value; };
struct FloatLiteral { std::uint64_t Ieee754Bits; };
struct StringLiteral { std::string Utf8; };
struct SymbolLiteral { std::string Utf8; };
using LiteralValue = std::variant<
    UnitLiteral, BoolLiteral, ByteLiteral, CharLiteral, IntLiteral,
    FloatLiteral, StringLiteral, SymbolLiteral>;
struct ConstantInst { LiteralValue Value; };
struct UnaryInst { UnaryOpcode Opcode; ValueId Operand; };
struct BinaryInst { BinaryOpcode Opcode; ValueId Left; ValueId Right; };
struct InjectSumInst { ValueId Alternative; model::TypeId Sum; };
struct DirectCallInst {
  FunctionId Callee;
  std::vector<ValueId> Arguments;
  std::optional<ValueId> CallableEnvironment;
  std::optional<ValueId> BoundaryContext;
};
struct LexicalRefInst { semantics::BindingId Binding; };

struct BranchTarget {
  EdgeId Edge;
  BlockId Block;
  std::vector<ValueId> Arguments;
};
struct EdgeResultContract {
  model::TypeId Type;
  OwnershipKind Ownership;
  std::optional<NominalTypeId> NominalIdentity;
  std::string DebugName;
};
struct ProducedBranchTarget {
  EdgeId Edge;
  BlockId Block;
  std::vector<EdgeResultContract> ProducedPrefix;
  std::vector<ValueId> Arguments;
};
struct TrapCompilerFailure { BranchTarget CleanupThenTrap; };
using RuntimeFailureDisposition = TrapCompilerFailure;
struct Branch { BranchTarget Target; };
struct CondBranch {
  ValueId Condition;
  BranchTarget TrueTarget;
  BranchTarget FalseTarget;
};
struct SwitchCase { std::int64_t Key; BranchTarget Target; };
struct Switch {
  ValueId Scrutinee;
  std::vector<SwitchCase> Cases;
  BranchTarget Default;
};
struct Return { std::optional<ValueId> Result; };
struct Raise { ValueId Exception; };
struct OperationUse {
  OperationId Declaration;
  std::vector<model::TypeId> TypeArguments;
  std::vector<model::EffectRowId> EffectArguments;
};
using OperationReference = std::variant<OperationUse, OperationInstanceId>;
struct Perform {
  OperationReference Operation;
  std::vector<ValueId> Arguments;
  ProducedBranchTarget ResumeTarget;
  model::EffectRowId ResumeEffects;
};
struct Resume { ValueId Continuation; ValueId Argument; };
struct Unreachable {};

struct Value {
  model::TypeId Type;
  PhysicalRepresentation Representation;
  SourceRange Range;
  OwnershipKind Ownership;
  std::optional<BorrowProvenance> Borrow;
  std::optional<NominalTypeId> NominalIdentity;
  std::optional<BoundaryRequirementSetId> CallableBoundaryRequirements;
  ValueOrigin Origin;
  std::string DebugName;
};

using InstructionPayload = std::variant<
    ConstantInst, UnaryInst, BinaryInst, InjectSumInst,
    DirectCallInst, LexicalRefInst>;
using Terminator = std::variant<
    Branch, CondBranch, Switch, Return, Raise, Perform,
    Resume, Unreachable>;

struct Instruction {
  std::optional<ValueId> Result;
  SourceRange Range;
  InstructionPayload Payload;
};
struct RangedTerminator {
  SourceRange Range;
  Terminator Payload;
};
struct Block {
  std::string DebugName;
  SourceRange Range;
  std::vector<ValueId> Arguments;
  std::vector<Instruction> Instructions;
  std::optional<RangedTerminator> End;
  std::optional<ControlRegionId> ControlRegion;
};
struct ConstructorDeclaration {
  ConstructorId Id;
  std::string Name;
  std::uint32_t Tag;
  std::vector<model::TypeId> Fields;
};
struct NominalDeclaration {
  NominalTypeId Id;
  model::NominalTypeKey Key;
  std::vector<model::TypeParameterId> TypeParameters;
  std::vector<ConstructorDeclaration> Constructors;
};
struct OperationParameter {
  model::TypeId Type;
  model::ParameterOwnership Ownership;
};
struct OperationDeclaration {
  OperationId Id;
  model::EffectOperationKey Key;
  std::vector<model::TypeParameterId> TypeParameters;
  std::vector<model::EffectVariableDeclaration> EffectVariables;
  std::vector<model::TypeConstraint> Constraints;
  std::vector<OperationParameter> Parameters;
  model::TypeId ResultType;
  model::ResultOwnership ResultContract;
  model::EffectRowId Effects;
  std::uint64_t DeclarationFingerprint;
};
struct OperationInstance {
  OperationInstanceId Id;
  OperationId Declaration;
  std::vector<model::TypeId> TypeArguments;
  std::vector<model::EffectRowId> EffectArguments;
  std::vector<OperationParameter> Parameters;
  model::TypeId ResultType;
  model::ResultOwnership ResultContract;
  model::EffectRowId SemanticEffects;
  RuntimeEffectRowId RuntimeEffects;
  std::uint64_t RuntimeFingerprint;
};
struct RuntimeEffectRow {
  RuntimeEffectRowId Id;
  model::EffectRowId SemanticRow;
  std::vector<OperationInstanceId> Operations;
  bool MayRaise;
  bool MayCancel;
  std::uint64_t StructuralFingerprint;
};
struct Global {
  GlobalId Id;
  model::SymbolIdentity Symbol;
  model::TypeId Type;
  OwnershipKind Ownership;
};
struct BuiltinTypeIds {
  model::TypeId ExceptionValue;
  model::TypeId EffectRequest;
  model::TypeId ControlOutcome;
  model::TypeId ExecutionContext;
  model::TypeId CallableInvocationEnvironment;
  model::TypeId ContinuationBoundaryContext;
};
enum class FunctionDefinitionKind { Definition, Imported, NativeExtern };
enum class FunctionVisibility { Private, Module, Public };
enum class FunctionEntryKind {
  Typed = 0, OutcomeRouter = 1, CleanupDrop = 2,
  KeyHashAdapter = 3, KeyEqualsAdapter = 4
};
enum class NativeAsyncKind : std::uint8_t {
  Synchronous = 0, ThreadPool = 1, DedicatedOutcome = 2
};
enum class NativeBoundaryRoute : std::uint8_t {
  CheckedDirectV2 = 0, CheckedOutcomeV2 = 1, StableExternal = 2
};
struct FunctionLinkage {
  FunctionDefinitionKind Kind;
  FunctionVisibility Visibility;
  std::optional<model::SymbolIdentity> Symbol;
  std::optional<std::string> NativeSymbol;
  std::optional<NativeAsyncKind> AsyncKind;
  std::optional<NativeBoundaryRoute> NativeRoute;
};
struct GenericCaptureBinding {
  semantics::BindingId Binding;
  model::TypeId Type;
  model::ParameterOwnership Ownership;
  std::string DebugName;
};
struct BoundaryContextContract {
  BoundaryContextContractId Id;
  std::vector<ContinuationBoundaryDescriptorId> Guaranteed; // canonical ID order
};
struct BoundaryRequirementSet {
  BoundaryRequirementSetId Id;
  std::vector<ContinuationBoundaryDescriptorId> Required; // canonical ID order
};
struct Function {
  FunctionId Id;
  std::shared_ptr<const FunctionLocalDomain> LocalDomain;
  std::string Name;
  SourceRange Range;
  model::TypeId Signature;
  FunctionEntryKind EntryKind;
  FunctionLinkage Linkage;
  std::optional<model::GenericBinderEnvironment> GenericBinders;
  std::vector<GenericCaptureBinding> GenericCaptures;
  std::optional<RuntimeEffectRowId> RuntimeEffects;
  std::optional<ValueId> ExecutionContextParameter;
  std::optional<ValueId> CallableEnvironmentParameter;
  std::optional<ValueId> BoundaryContextParameter;
  std::optional<BoundaryContextContractId> BoundaryContextContract;
  std::optional<BoundaryRequirementSetId> AmbientBoundaryRequirements;
  std::vector<ValueId> Parameters;
  std::vector<Value> Values;
  std::vector<Block> Blocks;
  std::optional<BlockId> Entry;
};
struct Module {
  model::ModuleIdentity Identity;
  std::shared_ptr<const ModuleMutationDomain> MutationDomain;
  std::shared_ptr<model::TypeTable> Types;
  BuiltinTypeIds Builtins;
  std::vector<model::ModuleIdentity> Imports;
  std::vector<NominalDeclaration> Nominals;
  std::vector<model::ResourceDeclaration> Resources;
  std::vector<OperationDeclaration> Operations;
  std::vector<OperationInstance> OperationInstances;
  std::vector<RuntimeEffectRow> RuntimeEffectRows;
  std::vector<BoundaryContextContract> BoundaryContextContracts;
  std::vector<BoundaryRequirementSet> BoundaryRequirementSets;
  std::vector<Global> Globals;
  std::vector<Function> Functions;
  std::shared_ptr<const SourceManager> Sources;
  SourceId Source;
  ModulePhase Phase;
};
```

`MutationDomain` is a nonserialized, non-forgeable compilation-session token
with a process-unique nonzero identity; every module-owned StrongId carries
that identity. Each Function likewise gets a distinct `FunctionLocalDomain`,
and all of its BlockId/ValueId values carry that local identity rather than
only a dense ordinal. The two owning shared pointers keep token lifetimes
stable, and every lookup compares the expected domain before range or
dominance checks. Moving an unchanged Module/Function preserves its token;
parsing, cloning as a new destination, runtime-module rebuilding, and function
reconstruction create fresh tokens plus exhaustive ID remaps. Canonical
TIRF/interface bytes encode only deterministic local indices and reconstruct
fresh domains when parsed. The verifier requires both tokens non-null and
prevents a specialization cache's destination-local FunctionId results from
being reused in another module. Tests collide ordinal-zero function IDs across
two modules and ordinal-zero block/value IDs across two functions in one
module; Builder, successor/operand visitors, dominance, parser/remap, and
verification must reject the foreign domain before indexing.

`Module::Types` is the same non-null append-only arena shared by the producing
`SemanticModel`; all prose references such as `Module.Types` mean
`*Module.Types`. Builder validates the pointer and every builtin/reference
domain before insertion, then shares it without moving the table object.
Cloning/parsing into an independent module allocates a new arena/domain and
performs the existing exhaustive remap, including `Module::Resources`. The
resource vector is canonical key order and is part of the module verifier,
printer/parser, fingerprint, clone, equality, and transactional rebuild; it is
never an out-of-band catalog pointer. Generic extraction includes the closed
declaration closure for every ResourceType reachable from a signature,
capture, instruction, or nested type. TIRF prints that closure, specialization
remaps it, and generic preparation imports/deduplicates it into the runtime
module before any representation or descriptor query. Tests cover a resource
inside a cross-module generic capture/result, missing and duplicate policy
rows, and two structurally conflicting rows for one key with full rollback.
A lowering regression keeps the
SemanticModel alive after `lowerModule`, proves both objects hold the same
arena/domain, appends a specialization-only type, and re-verifies every
preexisting semantic and IR TypeId.

`Value::Borrow` is present exactly when `Ownership == Borrowed`. A Borrowed
function parameter has `BorrowParameter` with no stored root—the parameter
itself is the implicit root, so callers need not predict the `ValueId` that
`Builder::addParameter` will allocate. A derived `BorrowParameter` view stores
that dominating parameter as `Root`; `StaticLifetime` has no root,
`OwnedValue` roots at a dominating live Owned value, and
`ScopedUniqueLoan` roots at a dominating sole Owned value under the verifier's
exclusive-loan rules. `ScopedUniqueLoanParameter` is rootless exactly on the
hidden first actual Borrow parameter of a prepared handled/protected body; a
view derived from it stores that dominating parameter as Root. It is a
callee-local capability and never names the caller's foreign ValueId. Every
other kind/root combination is invalid. Borrow
provenance is canonical Typed IR data: builder, text codec, all ID remappers,
SSA/dominance checks, and ownership/cleanup verification preserve it. Because
callable and operation results cannot be Borrowed, no call invents an
unexpressed cross-function lifetime.
The verifier rejects a rootless `BorrowParameter` anywhere except an actual
Borrow parameter and rejects a rooted one whose root is not such a parameter.
It applies the analogous actual-parameter/root rule to
`ScopedUniqueLoanParameter` and additionally forbids that capability from an
ordinary call, block escape, return, capture, store, or nested loan.

`Function::GenericCaptures` is empty for closed, imported, and native
functions. For an open nested definition it is the complete declaration-order
lexical capture schema. The verifier requires unique bindings, exact
type/ownership agreement with every corresponding `LexicalRefInst`, and no
undeclared free lexical reference. The canonical printer/parser includes this
field explicitly, so a generic fragment cannot rely on an ambient lowering
context to recover its environment.

`Function::GenericBinders` is present exactly on an open Definition through
Canonical fragment encoding. Its inherited and declared partitions come from
Task 2, and their concatenation is the function's effective lifted binder
order; all four lists retain phantoms. It is absent on closed, Imported, and
NativeExtern functions and on every runtime-reachable function at
`GenericPrepared`. The canonical printer/parser/remapper includes the complete
partition, so a TIRF reader never guesses ancestry from type occurrences.

`FunctionLinkage::{AsyncKind,NativeRoute}` are present exactly for
`NativeExtern` and are always explicit, including `Synchronous`; other linkage
kinds reject both. `CheckedDirectV2` is the one structural checked-native ABI,
`CheckedOutcomeV2` is the dedicated opcode-authenticated platform ABI, and
`StableExternal` is the allowlisted platform C ABI. The verifier requires
route/async consistency: StableExternal is Synchronous, CheckedOutcomeV2 is
DedicatedOutcome, and CheckedDirectV2 is Synchronous or ThreadPool. It also
forbids an Outcome opcode on either non-Outcome route.
Frontend normalization maps semantic Sync/ThreadPool/DedicatedOutcome to this enum
before syntax metadata disappears. The removed legacy raw-native-pointer enum has no replacement ABI
and is a ranged migration error; the greenfield pipeline never accepts or
serializes it. The canonical printer/parser, generic fragments, v2 `Linkage`,
and LLVM verifier preserve both closed enums. Task 8 selects checked direct
lowering from `NativeRoute`; Task 14 jointly selects direct call, universal
worker submission, or the dedicated platform Outcome path from route,
AsyncKind, and authenticated opcode, never from a symbol spelling.

`RuntimeEffectRows` is the post-instantiation carrier that the C ABI consumes;
it is distinct from the semantic `TypeTable` rows used for inference and
generic substitution. `Function::RuntimeEffects` is empty through
`ClosureConverted` and required for every reachable definition/import/native
declaration from `OperationInstantiated` onward. Each runtime row keeps its
closed semantic row plus the exact ordered `OperationInstanceId` list, so the
backend never tries to reconstruct closed instances from bare operation keys.
Runtime rows and operation instances may be mutually recursive and are
predeclared, filled, and verified as one SCC before publication.
`ExecutionContextParameter` is empty on ordinary non-adapter functions before
Task 12 selects their control modes. Task 9 creates it immediately on every
universal adapter because that ABI is unconditionally outcome-based. It is a
hidden Borrowed builtin value, never part of source arity or generic
substitution. After Task 12 it is mandatory exactly for explicit-outcome
functions, all universal adapters, `OutcomeRouter` entries, and `CleanupDrop`
entries. Task 12 forces every `OutcomeRouter` to explicit-outcome mode and
installs the context on both special entry kinds even when a CleanupDrop's
closed semantic row is empty; ordinary direct-return Typed entries have none.
Task 9 installs `BoundaryContextParameter` on every universal adapter when it
creates that ABI; ordinary non-adapter Yona direct entries,
`DirectCallInst::BoundaryContext`, and
`ApplyCallableInst::BoundaryContext` remain empty through Task 12. Task 13
installs their distinct hidden Borrow-only continuation-boundary channel on
all Yona direct entries/calls before any prepared effect body is published;
it is never inferred by LLVM or folded into the execution/callable context.
`FunctionEntryKind::Typed` is the only kind admitted in Canonical input and
serialized TIRF. The Canonical-to-GenericPrepared transition may add only the
predeclared private `KeyHashAdapter`/`KeyEqualsAdapter` entries described in
Task 11; GenericPrepared through ControlFlow admits Typed plus those two
fixed-ABI kinds. Task 13 may create a private `OutcomeRouter` entry, whose structural Signature
describes its logical state/body-result/handle-result contract but whose hidden
whole-outcome input is represented only by `RouteInnerOutcome` below; Task 13
also gives installed-boundary invocations the non-SSA detached-node capability
consumed by that router's dedicated terminators. Such a
function can be referenced only by one handler/try-boundary descriptor and is
never a source callable, import, export, or generic.
`CleanupDrop` is likewise private/non-generic and referenced by exactly one
`OwnedSlotStateType::DropIdentity`; its hidden C ABI consumes that state and a
Borrowed execution context and cannot return a language outcome.

`Perform::ResumeTarget` receives the operation result as its first block
argument and any explicitly threaded state afterward. It is a canonical CFG
edge, not a callable descriptor: the descriptor does not exist until effect
preparation splits out the continuation. `ResumeEffects` is the semantic
effect row of that exact continuation suffix and is copied from the solved
semantic model, never inferred from CFG reachability. `LexicalRefInst` carries
`semantics::BindingId`, never an outer function's `ValueId`; closure
conversion eliminates it later.

`ConstantInst` owns its literal payload. Its result type must be the exact
matching primitive; String produces an Owned managed value, while Unit/Bool/
Byte/Char/Int/Float/Symbol are Trivial. Char accepts only Unicode scalar
values; String and Symbol bytes must be valid UTF-8 but may contain encoded
NUL; Float stores the IEEE-754 bit pattern rather than a host-formatted number.
Symbol retains canonical spelling—not a module-local numeric interning ID—so
fragments and separately compiled modules agree. The canonical printer uses
length-delimited escaped UTF-8 for String/Symbol and fixed lowercase
hexadecimal float bits; the parser reconstructs the identical variant and
payload.

Operation identity has two noninterchangeable domains.
`DeclarationFingerprint` is FNV-1a over Task 2's binder-aware isolated
declaration bytes and is used only by semantics, v2 catalogs, and
specialization lookup. `RuntimeFingerprint` exists only on a closed
`OperationInstance` and hashes `encodeClosedDescriptorGraph` bytes used by
emitted descriptors/handler collision buckets. Neither derives from a
module-local ID, both exclude stored fingerprint fields, and hash equality is
only a prefilter before complete canonical-byte equality in the same domain.
The verifier rejects a declaration fingerprint computed with the closed codec
or a runtime fingerprint attached to an open declaration.

- [ ] **Step 4: Implement the ID-only builder and whole-module verifier**

Expose this builder contract:

```cpp
struct ModuleSpec {
  model::ModuleIdentity Identity;
  std::shared_ptr<model::TypeTable> Types;
  BuiltinTypeIds Builtins;
  std::shared_ptr<const SourceManager> Sources;
  SourceId Source;
};
struct NominalSpec {
  model::NominalTypeKey Key;
  std::vector<model::TypeParameterId> TypeParameters;
};
struct ConstructorSpec {
  std::string Name;
  std::uint32_t Tag;
  std::vector<model::TypeId> Fields;
};
struct OperationSpec {
  model::EffectOperationKey Key;
  std::vector<model::TypeParameterId> TypeParameters;
  std::vector<model::EffectVariableDeclaration> EffectVariables;
  std::vector<model::TypeConstraint> Constraints;
  std::vector<OperationParameter> Parameters;
  model::TypeId ResultType;
  model::ResultOwnership ResultContract;
  model::EffectRowId Effects;
  std::uint64_t DeclarationFingerprint;
};
struct GlobalSpec {
  model::SymbolIdentity Symbol;
  model::TypeId Type;
  OwnershipKind Ownership;
};
struct FunctionSpec {
  std::string Name;
  SourceRange Range;
  model::TypeId Signature;
  FunctionLinkage Linkage;
};
struct BlockSpec { std::string DebugName; SourceRange Range; };
struct ValueSpec {
  model::TypeId Type;
  OwnershipKind Ownership;
  std::optional<BorrowProvenance> Borrow;
  SourceRange Range;
  std::string DebugName;
};

class Builder final {
public:
  explicit Builder(ModuleSpec Spec);
  void addImport(model::ModuleIdentity Import);
  void addResource(model::ResourceDeclaration Declaration);
  NominalTypeId addNominal(NominalSpec Spec);
  ConstructorId addConstructor(NominalTypeId Type, ConstructorSpec Spec);
  OperationId addOperation(OperationSpec Spec);
  GlobalId addGlobal(GlobalSpec Spec);
  FunctionId addFunction(FunctionSpec Spec);
  BlockId createBlock(FunctionId Function, BlockSpec Spec);
  ValueId addParameter(FunctionId Function, ValueSpec Spec);
  ValueId addBlockArgument(FunctionId Function, BlockId Block, ValueSpec Spec);
  ValueId emit(FunctionId Function, BlockId Block,
               InstructionPayload Payload, ValueSpec Result);
  void emitEffect(FunctionId Function, BlockId Block,
                  InstructionPayload Payload, SourceRange Range);
  void setTerminator(FunctionId Function, BlockId Block, Terminator End,
                     SourceRange Range);
  Module finish(ModulePhase Phase);
};
```

The constructor moves and owns every `ModuleSpec` field, and `finish` is
one-shot. Construction/insertion rejects null Sources, an invalid primary
Source or builtin TypeId, duplicate imports/nominal keys/constructor names or
tags/operation keys/global symbols, invalid referenced types/effects, and a
ConstructorId used under the wrong nominal. IDs are assigned only by checked
insertion; no API returns a reference into growable storage. Update the first
builder fixture to pass `ModuleSpec` and add exact case `Typed IR builder seeds
a complete module` plus malformed-seed verifier rows.

`addResource` validates the declaration's binder domain/arity, known enum
values, Linear-versus-AlwaysShareable callback rule, nonempty portable C
symbols, and disjointness from every nominal/resource key before staging it.
`finish` sorts resources by canonical qualified key; parser/remap/clone use
the same builder path. Tests reject missing, duplicate, out-of-domain,
nominal-colliding, and conflicting policy rows without publishing a partial
module.

The verifier computes predecessors and dominators itself, validates every ID
before dereference, and returns all deterministic issues. Definitions require
one Entry and at least one ranged block; Imported/NativeExtern functions have
no Entry or blocks, require the corresponding canonical/native symbol, and
may still be referenced by `DirectCallInst`. Exported definitions carry a
Public symbol identity. Native symbols are nonempty platform-independent C
identifiers; imported/exported symbols retain module identity through v2
remapping. Every block, instruction, and terminator has a valid source range
or an explicitly marked compiler-generated range with an origin range; no
debug-relevant location is synthesized later from LLVM state:

Fallible/result-bearing terminators never define predecessor-local SSA
values. A `ProducedBranchTarget` says that the runtime produces its ordered
prefix directly into the target block's first block arguments; `Arguments`
supplies the remaining target arguments from already-dominating predecessor
values. The prefix and explicit suffix must cover the target arguments exactly
and agree on type, ownership, nominal identity, and order. Thus every usable
result still has `ValueOrigin::BlockArgument`, exists only in its designated
successor, and cannot be referenced on a failure or sibling edge. All
predecessors of a join must supply the same block-argument contracts, whether
as a produced prefix or explicit arguments. Builder lookup, operand visitors,
canonical printer/parser, cloning/remapping, CFG snapshots, dominance and
ownership verification handle both target forms exhaustively. LLVM lowering
creates no hidden alloca: each produced prefix is the incoming value for that
successor block argument's PHI (or the direct value in a single-predecessor
block), followed by the explicit incoming arguments. Tests reject use on the
producer/failure/sibling edge, missing or reordered produced fields,
prefix/suffix arity and ownership mismatch, and inconsistent join incoming
contracts.

```cpp
struct VerificationIssue {
  SourceRange Range;
  VerificationRule Rule;
  std::string Message;
};
struct PassDiagnostic {
  SourceRange Range;
  std::string Pass;
  std::string Message;
};
using PassResult =
    std::expected<void, std::vector<PassDiagnostic>>;
enum class VerificationRule {
  InvalidId, MissingTerminator, DuplicateDefinition, DefinitionBeforeUse,
  Dominance, SuccessorArity, SuccessorType, SuccessorOwnership,
  ConditionType, ReturnType, CallSignature, Representation,
  OperationSignature, IllegalPhaseOperation
};
struct VerifyOptions {
  bool VerifyDominance = true;
  bool VerifyPhase = true;
};
using VerificationResult =
    std::expected<void, std::vector<VerificationIssue>>;

VerificationResult verifyModule(const Module &, VerifyOptions = {});
void verifyOrThrow(const Module &, VerifyOptions = {});
```

Define exhaustive traversal alongside the variants:

```cpp
void forEachOperand(const InstructionPayload &,
                    const std::function<void(ValueId)> &Visit);
void forEachOperand(const Terminator &,
                    const std::function<void(ValueId)> &Visit);
struct SuccessorView {
  EdgeId Edge;
  std::uint32_t Ordinal;
  BlockId Block;
  std::span<const EdgeResultContract> ProducedPrefix;
  std::span<const ValueId> Arguments;
};
void forEachSuccessor(const Terminator &,
                      const std::function<void(SuccessorView)> &Visit);
```

Ordinary `BranchTarget` values yield an empty `ProducedPrefix`; produced
targets yield both spans. `Edge` is copied from that exact target occurrence
and `Ordinal` is its zero-based position in the terminator's canonical
successor order, so two occurrences naming the same Block remain distinct.
The view is callback-lifetime-only. No CFG client downcasts a terminator to
recover successors.

Every later task that adds an instruction or terminator must extend these
visitors and their exhaustiveness tests in the same commit. Analyses may not
maintain private, incomplete opcode switches.

- [ ] **Step 5: Add a deterministic parser/printer and generated round trips**

The text format uses numeric IDs, explicit types/ownership/linkage/ranges, one
instruction per line, and explicit block arguments. Require this property:

```cpp
const auto First = printModule(Module);
const auto Parsed = parseModule(First, Module.Sources);
REQUIRE(Parsed.has_value());
CHECK(printModule(*Parsed) == First);
```

Generate small deterministic well-typed CFGs from seeds `0..255`; verify,
print, parse, verify again, and compare bytes. Malformed parser cases must
return located diagnostics rather than partial modules.

The exact text APIs are:

```cpp
std::string printModule(const Module &);
struct ParseError { SourceRange Range; std::string Message; };
using ParseModuleResult =
    std::expected<Module, std::vector<ParseError>>;
ParseModuleResult
parseModule(std::string_view Text,
            std::shared_ptr<const SourceManager> Sources);
```

`makeScalarModule`, `makeNonDominatingUseModule`, `hasRule`, and
`enumCount` in Step 1 are test-local helpers defined at the top of the named
test files; they construct only public `Builder` inputs and have no production
API status. Add seeded property generators for structural type tables, ABI
carrier round trips, ownership CFGs, pattern matrices, and nested cleanup
regions as their owning tasks introduce those domains.

Implement `scripts/run-focused-tests.py <test-executable> <doctest-filters...>`:
it splits every positive comma-separated `-tc` and `-ts` value, invokes
doctest's list/count mode once per individual pattern (preserving the other
suite/test filters), and requires a positive parseable matching-test count for
each before running once with the original combined arguments. Exclusion
patterns may reduce the combined result but do not need their own positive
match. Reject `-sc` because doctest cannot enumerate subcases without running
their parent; plans must run the complete named test case or give the desired
regression its own `TEST_CASE`. Register a Python contract using a tiny fake
doctest executable for zero, one, failing, one-good/one-zero comma lists,
suite lists, exclusions, and rejected subcase filters. Every focused command
from this task onward uses this helper, so one valid sibling cannot mask a
misspelled filter.

- [ ] **Step 6: Run the complete IR foundation gate**

```bash
cmake --preset x64-debug-linux
cmake --build --preset build-debug-linux --target tests -j2
python3 scripts/run-focused-tests.py ./out/build/x64-debug-linux/tests \
  -tc='Typed IR core*,Typed IR builder*,Typed IR verifier*,Typed IR text*,Typed IR property*'
git diff --check
```

Expected: every focused case passes and the printed form is byte-stable across
two parse/print cycles.

- [ ] **Step 7: Commit the SSA foundation**

```bash
git add include/yona/TypedIr src/TypedIr test/TypedIr \
  scripts/run-focused-tests.py test/CMake/focused_test_runner_contract.py \
  cmake/YonaComponents.cmake CMakeLists.txt
git add docs/superpowers/plans/2026-09-01-typed-ir-codegen-rewrite.md
git commit -m "feat: add verified ssa typed ir"
```

### Task 4: Preserve complete function clauses and enforce guard semantics

**Files:**

- Modify: `include/yona/Syntax/Ast.h`
- Modify: `src/Syntax/Ast.cpp`
- Modify: `src/Syntax/ParserImpl.h`
- Modify: `src/Syntax/ParserModule.cpp`
- Modify: `src/Syntax/ParserExpr.cpp`
- Create: `test/Syntax/FunctionClauseParserTest.cpp`
- Modify: `src/Semantics/TypeChecker.cpp`
- Modify: `src/Semantics/SemanticModel.cpp`
- Modify: `src/Semantics/TerminationAnalysis.cpp`
- Modify: `src/Semantics/ModuleFunctionDependencies.cpp`
- Modify: `src/Semantics/BorrowEscapeAnalysis.cpp`
- Modify: `src/Semantics/LinearityChecker.cpp`
- Modify: `src/Semantics/RefinementChecker.cpp`
- Modify: `src/Semantics/AcceleratorDiag.cpp`
- Modify: `src/TypedCore/Analyze.cpp`
- Modify: `test/Syntax/FormatParserTest.cpp`
- Modify: `test/Semantics/TerminationAnalysisTest.cpp`
- Modify: `test/Runtime/SequenceOperationsTest.cpp`
- Modify: `test/Semantics/TypeCheckerTest.cpp`
- Modify: `CMakeLists.txt`
- Modify: `docs/todo-list.md`
- Modify: `CHANGELOG.md`

**Interfaces:**

- Consumes: existing `PatternNode`, `FunctionBody`, and solved type inference.
- Produces: owning `FunctionExpr::Clauses`, where each `FunctionClause`
  preserves its patterns, borrow annotations, guarded alternatives, and range.
- Temporary oracle support: legacy public `patterns`, `param_borrow`, and
  `bodies` preserve the old view exactly: patterns/borrow flags project the
  first clause, while bodies flatten every clause's alternatives in source
  order. They are deleted in Task 17 and must never be read by the new
  pipeline.

- [ ] **Step 1: Write parser regressions for complete equations**

Parse this module and assert both equations retain their own patterns:

```cpp
const auto Parsed = parseModule(R"(
module Main
choose 0 right = right
choose left 0 = left
)");
REQUIRE(Parsed);
const auto *Function = Parsed->Module->functions.front();
REQUIRE(Function->clauses.size() == 2);
CHECK(printNode(Function->clauses[0]->patterns[0]) == "0");
CHECK(printNode(Function->clauses[1]->patterns[0]) == "left");
CHECK(printNode(Function->clauses[1]->patterns[1]) == "0");
```

Add another case for a single equation with two guarded alternatives and
verify both guards and bodies remain under the same clause.

- [ ] **Step 2: Write the three semantic red regressions**

Use the existing test helper that parses/checks source and inspects diagnostics:

```cpp
TEST_CASE("Guard and pattern typing rejects non-Bool case guards") {
  const auto Diagnostics = checkExpression("case 1 of value | 42 -> value end");
  CHECK(hasTypeMismatch(Diagnostics, "case guard", "Bool"));
}

TEST_CASE("Guard and pattern typing requires identical or-pattern bindings") {
  const auto Diagnostics = checkExpression(
      "case Some 1 of Some left | Some right -> left end");
  CHECK(hasDiagnostic(Diagnostics, "or-pattern alternatives must bind the same names"));
}

TEST_CASE("Guard and pattern typing rejects non-Bool generator guards") {
  const auto Diagnostics = checkExpression("[x for x = [1, 2], if x]");
  CHECK(hasTypeMismatch(Diagnostics, "generator condition", "Bool"));
}
```

Also cover the same or-pattern name with incompatible inferred types and a
valid identical-binding alternative.

- [ ] **Step 3: Run the focused tests and verify all four failures**

```bash
cmake --build --preset build-debug-linux --target tests -j2
python3 scripts/run-focused-tests.py ./out/build/x64-debug-linux/tests \
  -tc='Function clauses*,Guard and pattern typing*'
```

Expected: later-clause pattern assertions fail, and the three invalid programs
produce no required type diagnostic.

- [ ] **Step 4: Make each clause the sole owning syntax representation**

Add this record and ownership to `FunctionExpr`:

```cpp
struct FunctionClause final {
  SourceRange range;
  std::vector<PatternNode *> patterns;
  std::vector<bool> parameter_borrows;
  std::vector<FunctionBody *> alternatives;

  FunctionClause(SourceRange Range, std::vector<PatternNode *> Patterns,
                 std::vector<bool> ParameterBorrows,
                 std::vector<FunctionBody *> Alternatives);
  ~FunctionClause();
};

class FunctionExpr final : public ScopedNode {
public:
  const std::string name;
  std::vector<FunctionClause *> clauses;
  std::optional<compiler::types::Type> type_signature;
  std::string source_text;

  // Frozen-oracle projection; never owns and never used by Typed IR.
  std::vector<PatternNode *> patterns;
  std::vector<bool> param_borrow;
  std::vector<FunctionBody *> bodies;
};
```

Change `parse_function_clause` to return `ParsedFunctionClause`, append every
clause, and build the legacy pattern/borrow projection from
`clauses.front()` while flattening every clause's bodies into the legacy
`bodies` view without deleting later patterns. All syntax/semantic walkers
iterate clauses and their alternatives; no walker switches back to the
projection.

- [ ] **Step 5: Enforce guards and merge or-pattern environments**

In `infer_case` and both collection extractor paths, infer then unify guards:

```cpp
if (clause->guard) {
  auto *Guard = infer(clause->guard, clause_env, level);
  unifier_.unify(Guard, arena_.make_con(TyCon::Bool), clause->guard->Range,
                 "in case guard");
}

if (vce->condition) {
  auto *Condition = infer(vce->condition, env, level);
  unifier_.unify(Condition, arena_.make_con(TyCon::Bool),
                 vce->condition->Range, "in generator condition");
}
```

Infer each or-pattern alternative in a distinct child environment, compare
the exact key sets from `TypeEnv::locals()`, unify each corresponding scheme
body, then bind the resolved common types into the caller environment:

```cpp
std::vector<std::unordered_map<std::string, TypeScheme>> Alternatives;
for (const auto &Pattern : op->patterns) {
  auto AlternativeEnv = env->child();
  auto *AlternativeType = infer_pattern(Pattern.get(), AlternativeEnv, level);
  Alternatives.push_back(AlternativeEnv->locals());
  unifyAlternativeType(AlternativeType);
}
requireSameBindingNames(Alternatives, pat->Range);
for (const auto &[Name, Scheme] : Alternatives.front()) {
  for (std::size_t Index = 1; Index < Alternatives.size(); ++Index)
    unifier_.unify(Scheme.body, Alternatives[Index].at(Name).body, pat->Range,
                   "in or-pattern binding '" + Name + "'");
  env->bind(Name, unifier_.resolve(Scheme.body));
}
```

Apply the same Bool rule to guarded function alternatives and include every
guard/body effect in inference. Define dictionary-pattern lookup keys as a
typed literal or symbol only. Reject an identifier/binding, wildcard,
constructor, or compound pattern in key position with a ranged diagnostic;
values retain the full pattern vocabulary. This intentional greenfield
restriction makes dictionary matching independent of HAMT traversal order and
gives Task 6 a deterministic lookup/test operation. Add exact case `Guard and
pattern typing rejects binding dictionary keys`.

- [ ] **Step 6: Verify all walkers and semantics**

```bash
python3 scripts/run-focused-tests.py ./out/build/x64-debug-linux/tests \
  -tc='Function clauses*,Guard and pattern typing*,TypeChecker*,SemanticModel*'
ctest --preset unit-tests-linux --output-on-failure
git diff --check
```

Expected: clause and semantic regressions pass; legacy tests remain green.

- [ ] **Step 7: Record and commit the frontend correction**

Remove the completed case-guard, or-pattern-environment,
generator-condition, and discarded-function-clause entries from
`docs/todo-list.md`; add the exact language fix under `Unreleased`.

```bash
git add include/yona/Syntax/Ast.h src/Syntax src/Semantics src/TypedCore \
  test/Syntax test/Semantics test/Runtime/SequenceOperationsTest.cpp \
  CMakeLists.txt docs/todo-list.md CHANGELOG.md
git add docs/superpowers/plans/2026-09-01-typed-ir-codegen-rewrite.md
git commit -m "fix: preserve and type every function clause"
```

### Task 5: Lower semantic AST into scalar and structured-control Typed IR

**Files:**

- Create: `include/yona/TypedIr/AstLowering.h`
- Create: `src/TypedIr/AstLowering.cpp`
- Create: `include/yona/TypedIr/Diagnostics.h`
- Create: `test/Support/TypedIrTestUtil.h`
- Create: `test/TypedIr/AstLoweringTest.cpp`
- Modify: `include/yona/TypedIr/Instruction.h`
- Modify: `src/TypedIr/TypedIr.cpp`
- Modify: `src/TypedIr/Verifier.cpp`
- Modify: `src/TypedIr/Printer.cpp`
- Modify: `src/TypedIr/Parser.cpp`
- Modify: `cmake/YonaComponents.cmake`
- Modify: `CMakeLists.txt`

**Interfaces:**

- Consumes: `ast::ExprNode`/`ast::ModuleDecl`, immutable `SemanticModel`,
  `model::ModuleIdentity`, and Task 3's `Builder`.
- Produces: `lowerExpression` and `lowerModule`, returning a
  `ModulePhase::Canonical` module or ranged diagnostics.
- Isolation: every function owns a new `FunctionLoweringContext`; no binding,
  block, debug, task, effect, cleanup, or ownership state is copied from the
  enclosing function.

- [ ] **Step 1: Write printer-snapshot tests for scalar/control lowering**

Create one helper that parses, typechecks, constructs `SemanticModel`, lowers,
verifies, and prints. Required snapshots cover Unit/Bool/Int/Float, unary and
scalar arithmetic/comparison operations, identifier shadowing, `let`, short-circuit
`&&`/`||`, direct calls, and both `if` arms. A representative assertion is:

```cpp
TEST_CASE("Typed IR AST lowering uses a merge block argument for if") {
  const auto Text = lowerAndPrint("let flag = true in if flag then 1 else 2");
  CHECK(Text == R"(module expression phase canonical
fn @main() -> Int {
  ^entry:
    %flag: Bool trivial = const true
    cond_br %flag, ^then(), ^else()
  ^then:
    %one: Int trivial = const 1
    br ^merge(%one)
  ^else:
    %two: Int trivial = const 2
    br ^merge(%two)
  ^merge(%result: Int trivial):
    return %result
}
)");
}
```

Add a nested-lambda case that inspects the canonical module and proves the
inner function contains `LexicalRefInst{outerBinding}` rather than an outer
function `ValueId`.

- [ ] **Step 2: Run and confirm the missing lowerer**

```bash
cmake --build --preset build-debug-linux --target tests -j2
python3 scripts/run-focused-tests.py ./out/build/x64-debug-linux/tests -tc='Typed IR AST lowering*'
```

Expected: compilation fails because `AstLowering.h` is absent.

- [ ] **Step 3: Implement isolated expression/function contexts**

Expose:

```cpp
using LoweringResult =
    std::expected<Module, std::vector<LoweringDiagnostic>>;

LoweringResult lowerExpression(const ast::ExprNode &Expression,
                               const semantics::SemanticModel &Semantics,
                               const model::ModuleIdentity &Identity);
LoweringResult lowerModule(const ast::ModuleDecl &Module,
                           const semantics::SemanticModel &Semantics);
```

Both entry points seed `ModuleSpec::Types` with
`Semantics.sharedTypeArena()`. They never copy, move from, or rebuild that
arena, so every projected `TypeId` remains in-domain while the immutable
SemanticModel stays usable. A test retains the SemanticModel across lowering,
queries old facts from both views, interns a later closed IR type, and proves
all old IDs and stable references still validate.

Each function starts with exactly this mutable context:

```cpp
struct FunctionLoweringContext final {
  FunctionId Function;
  BlockId CurrentBlock;
  std::unordered_map<semantics::BindingId, ValueId,
                     semantics::BindingIdHasher> Bindings;
  std::vector<CleanupRegionId> CleanupStack;
};
```

Represent a nested function value without guessing captures:

```cpp
struct LexicalBindingValue {
  semantics::BindingId Binding;
  ValueId Value;
};
struct MakeFunctionInst {
  FunctionId Target;
  std::vector<LexicalBindingValue> AvailableBindings;
};
```

Add `MakeFunctionInst` to `InstructionPayload` in this task and extend the
Task 3 operand visitor to traverse every `AvailableBindings[].Value`.
The verifier checks target/function domains, complete binding uniqueness and
type/ownership, and the canonical printer/parser round-trips the target and
ordered binding/value pairs in this same milestone; no later task supplies a
missing variant arm.

`lowerModule` copies the SemanticModel's unified
`EffectOperationSignature` table into `Module::Operations`, assigning stable
module-local `OperationId`s only after sorting by canonical key/signature.
Every declaration stores the binder-aware declaration fingerprint. Each
`Perform`/handler reference becomes an `OperationUse` with type/effect
arguments in declaration-binder order; no lowering path invents a signature
or runtime fingerprint. Task 13 closes those uses into operation instances.

Resolve names only through `SemanticModel::bindingFor`; never key new lowering
by source spelling. Lower `let` as a context binding update. Lower `if` and
short-circuit operators as branches whose merge block carries the result as a
block argument. Collection/string `++`, `in`, and `--`, plus sequence `::` and
`:>`, are explicitly outside Task 5's binary subset: Task 11/15 consume their
persisted semantic resolutions, and no temporary scalar/CType lowering is
allowed. At this milestone, reject match, function-clause, generator,
`try`, and `handle` nodes with a source-ranged diagnostic; Task 6 extends this
same lowerer only after defining their durable control vocabulary. Do not
invoke legacy Codegen.

- [ ] **Step 4: Verify deterministic snapshots and function isolation**

```bash
python3 scripts/run-focused-tests.py ./out/build/x64-debug-linux/tests -tc='Typed IR AST lowering*'
python3 scripts/run-focused-tests.py ./out/build/x64-debug-linux/tests -tc='Typed IR verifier*,Semantic model structural type*'
git diff --check
```

Expected: all snapshots match, every result verifies, and no inner function
contains a foreign `ValueId`.

- [ ] **Step 5: Commit scalar canonical lowering**

```bash
git add include/yona/TypedIr/AstLowering.h \
  include/yona/TypedIr/Diagnostics.h include/yona/TypedIr/Instruction.h \
  src/TypedIr/AstLowering.cpp src/TypedIr/TypedIr.cpp \
  src/TypedIr/Verifier.cpp src/TypedIr/Printer.cpp src/TypedIr/Parser.cpp \
  test/Support/TypedIrTestUtil.h test/TypedIr/AstLoweringTest.cpp \
  cmake/YonaComponents.cmake CMakeLists.txt
git add docs/superpowers/plans/2026-09-01-typed-ir-codegen-rewrite.md
git commit -m "feat: lower semantic ast to typed ir"
```

### Task 6: Canonicalize all clauses and patterns into decision trees

**Files:**

- Create: `include/yona/TypedIr/Pattern.h`
- Create: `src/TypedIr/Pattern.cpp`
- Create: `include/yona/TypedIr/DecisionTree.h`
- Create: `include/yona/TypedIr/Control.h`
- Create: `include/yona/TypedIr/PatternCanonicalization.h`
- Create: `src/TypedIr/PatternCanonicalization.cpp`
- Create: `include/yona/TypedIr/Passes/ControlFlowLowering.h`
- Create: `src/TypedIr/Passes/ControlFlowLowering.cpp`
- Create: `include/yona/TypedIr/Derivation.h`
- Create: `src/TypedIr/Derivation.cpp`
- Create: `test/TypedIr/PatternCanonicalizationTest.cpp`
- Create: `test/TypedIr/ControlFlowLoweringTest.cpp`
- Create: `test/TypedIr/DerivationTest.cpp`
- Modify: `include/yona/TypedIr/Instruction.h`
- Modify: `include/yona/TypedIr/TypedIr.h`
- Modify: `include/yona/TypedIr/Builder.h`
- Modify: `include/yona/TypedIr/AstLowering.h`
- Modify: `src/TypedIr/Builder.cpp`
- Modify: `src/TypedIr/Verifier.cpp`
- Modify: `src/TypedIr/Printer.cpp`
- Modify: `src/TypedIr/Parser.cpp`
- Modify: `src/TypedIr/AstLowering.cpp`
- Modify: `include/yona/Semantics/PatternAnalysis.h`
- Modify: `src/Semantics/PatternAnalysis.cpp`
- Modify: `cmake/YonaComponents.cmake`
- Modify: `CMakeLists.txt`

**Interfaces:**

- Consumes: Task 4's complete clauses and every existing `PatternNode` family.
- Produces: typed `PatternArena`, `MatchRow`, deterministic decision nodes,
  exhaustiveness/redundancy facts, and ordinary CFG with ranged `MatchError`.
- Shared path: function equations, `case`, catch clauses, handler matching,
  and derived functions all use this compiler.

- [ ] **Step 1: Write the decision-tree red matrix**

Create table-driven tests for wildcard/binding, literals, symbols, tuples,
exact sequences, head-tail/tails-head/head-and-tail, dictionaries, records,
constructors, `as`, or-patterns, and typed/sum patterns. Assert first-match
order and guard placement:

```cpp
TEST_CASE("Typed IR pattern canonicalization binds before testing a guard") {
  const auto Tree = canonicalize(R"(
case Some 4 of
  Some value | value > 0 -> value
  None -> 0
end
)");
  CHECK(printDecisionTree(Tree) == R"(switch ctor $0
  Some($0.0) -> bind value = $0.0
                 guard value > 0 -> leaf clause0
                 false -> fail
  None -> leaf clause1
  default -> fail)");
}
```

Add redundancy/exhaustiveness tests and assert the failure leaf retains the
source range needed for `MatchError`.

Create top-level cases named exactly `Typed IR control flow lowering:
non-exhaustive match reaches only MatchError` and `Typed IR derivation: derive
trait bodies without source generation`; these are the sentinels
for the other two Task 6 focused prefixes.

- [ ] **Step 2: Run and verify the absent pattern compiler**

```bash
cmake --build --preset build-debug-linux --target tests -j2
python3 scripts/run-focused-tests.py ./out/build/x64-debug-linux/tests \
  -tc='Typed IR pattern canonicalization*,Typed IR control flow lowering*,Typed IR derivation*'
```

Expected: compilation fails for the new headers.

- [ ] **Step 3: Define typed match places, rows, and decision nodes**

Use these stable records:

```cpp
enum class SequenceIndexOrigin : std::uint8_t { Front = 0, Back = 1 };
struct TupleElementProjection { std::uint32_t Index; };
struct SequenceElementProjection {
  SequenceIndexOrigin Origin;
  std::uint32_t Index;
};
struct SequenceSliceProjection {
  std::uint32_t DropFront;
  std::uint32_t DropBack;
};
using DictionaryLookupKey = LiteralValue;
struct DictionaryValueProjection { DictionaryLookupKey Key; };
struct RecordFieldProjection { std::string Name; };
struct ConstructorFieldProjection {
  ConstructorId Constructor;
  std::uint32_t Index;
};
struct RuntimeTypePayloadProjection { model::TypeId Type; };
using Projection = std::variant<
    TupleElementProjection, SequenceElementProjection,
    SequenceSliceProjection, DictionaryValueProjection,
    RecordFieldProjection, ConstructorFieldProjection,
    RuntimeTypePayloadProjection>;

struct WildcardPattern {};
struct BindingPattern { semantics::BindingId Binding; };
struct LiteralPattern { LiteralValue Value; };
struct SymbolPattern { SymbolLiteral Value; };
struct TuplePattern { std::vector<PatternId> Elements; };
struct ExactSequencePattern { std::vector<PatternId> Elements; };
struct HeadTailPattern { PatternId Head; PatternId Tail; };
struct TailsHeadPattern { PatternId Initial; PatternId Last; };
struct HeadAndTailPattern {
  PatternId Head;
  PatternId Middle;
  PatternId Last;
};
struct DictionaryEntryPattern { DictionaryLookupKey Key; PatternId Value; };
struct DictionaryPattern { std::vector<DictionaryEntryPattern> Entries; };
struct RecordPattern {
  std::vector<std::pair<std::string, PatternId>> Fields;
};
struct ConstructorPattern {
  ConstructorId Constructor;
  std::vector<PatternId> Fields;
};
struct AsPattern { PatternId Pattern; semantics::BindingId Binding; };
struct OrPattern { std::vector<PatternId> Alternatives; };
struct TypePattern { model::TypeId Type; PatternId Pattern; };
using Pattern = std::variant<
    WildcardPattern, BindingPattern, LiteralPattern, SymbolPattern,
    TuplePattern, ExactSequencePattern, HeadTailPattern, TailsHeadPattern,
    HeadAndTailPattern, DictionaryPattern, RecordPattern,
    ConstructorPattern, AsPattern, OrPattern, TypePattern>;

class PatternArena final {
public:
  PatternId add(Pattern Pattern, SourceRange Range);
  const Pattern &at(PatternId Id) const;
  SourceRange range(PatternId Id) const;
};

struct MatchPlace {
  std::uint32_t InputIndex;
  std::vector<Projection> Projections;
  model::TypeId Type;
};
struct MatchInput {
  model::TypeId Type;
  OwnershipKind Ownership;
};
struct PatternBinding {
  semantics::BindingId Binding;
  MatchPlace Place;
  OwnershipKind Ownership;
};
struct MatchAlternativeId {
  MatchPlanId Plan;
  std::uint32_t Index;
  friend bool operator==(const MatchAlternativeId &,
                         const MatchAlternativeId &) = default;
};
struct MatchGuardPlan {
  BlockId Entry;
  BlockId Exit;
};
struct MatchAlternative {
  std::optional<MatchGuardPlan> Guard;
  BlockId BodyEntry;
  std::vector<semantics::BindingId> BindingOrder;
  SourceRange Range;
};
struct MatchRow {
  std::vector<PatternId> Patterns;
  std::vector<PatternBinding> Bindings;
  std::vector<MatchAlternativeId> Alternatives;
  SourceRange Range;
};
struct MatchPlan {
  MatchPlanId Id;
  FunctionId Function;
  std::vector<MatchInput> Inputs;
  std::vector<MatchRow> Rows;
  std::vector<MatchAlternative> Alternatives;
  std::optional<DecisionNodeId> Root;
  SourceRange Range;
};
struct MatchDispatch { MatchPlanId Plan; std::vector<ValueId> Inputs; };
struct DecisionSwitch {
  MatchPlanId Plan;
  DecisionNodeId Root;
  std::vector<ValueId> Inputs;
};
struct MatchGuardYield {
  MatchAlternativeId Alternative;
  ValueId Condition;
};

struct DecisionFail { SourceRange Range; };
struct DecisionLeaf { MatchAlternativeId Alternative; };
struct DecisionBind { PatternBinding Binding; DecisionNodeId Next; };
struct DecisionGuard {
  MatchAlternativeId Alternative;
  DecisionNodeId True;
  DecisionNodeId False;
};
enum class SequenceShapeKind : std::uint8_t {
  Empty = 0, ExactLength = 1, AtLeastLength = 2
};
struct LiteralEqualsTest { LiteralValue Value; };
struct SymbolEqualsTest { SymbolLiteral Value; };
struct ConstructorIsTest { ConstructorId Constructor; };
struct TupleArityTest { std::uint32_t Arity; };
struct SequenceShapeTest { SequenceShapeKind Kind; std::uint32_t Length; };
struct DictionaryContainsKeyTest { DictionaryLookupKey Key; };
struct RecordHasFieldTest { std::string Name; };
struct RuntimeTypeIsTest { model::TypeId Type; };
using PatternTest = std::variant<
    LiteralEqualsTest, SymbolEqualsTest, ConstructorIsTest, TupleArityTest,
    SequenceShapeTest, DictionaryContainsKeyTest, RecordHasFieldTest,
    RuntimeTypeIsTest>;
struct DecisionTest {
  MatchPlace Place;
  PatternTest Test;
  DecisionNodeId True;
  DecisionNodeId False;
};
struct DecisionCase { PatternTest Test; DecisionNodeId Next; };
struct DecisionSwitchNode {
  MatchPlace Place;
  std::vector<DecisionCase> Cases;
  DecisionNodeId Default;
};
using DecisionNode = std::variant<DecisionFail, DecisionLeaf, DecisionBind,
                                  DecisionGuard, DecisionTest,
                                  DecisionSwitchNode>;

class DecisionArena final {
public:
  DecisionNodeId add(DecisionNode Node, SourceRange Range);
  const DecisionNode &at(DecisionNodeId Id) const;
  SourceRange range(DecisionNodeId Id) const;
};

enum class ProjectionOwnership : std::uint8_t {
  Borrow = 0, Retain = 1, Take = 2
};
struct PatternTestInst { ValueId Input; PatternTest Test; };
struct PatternProjectInst {
  ValueId Input;
  Projection Step;
  ProjectionOwnership Ownership;
};
struct TryRegion {
  ControlRegionId Id;
  FunctionId Function;
  std::optional<ControlRegionId> Parent;
  BlockId BodyEntry;
  BlockId CatchDispatch;
  MatchPlanId CatchPlan;
  std::optional<DecisionNodeId> CatchRoot;
  BlockId Continuation;
  model::TypeId ProtectedResultType;
  model::TypeId ResultType;
  model::EffectRowId ProtectedEffects;
  model::EffectRowId ResidualEffects;
  std::optional<FunctionId> PreparedProtected;
  std::optional<FunctionId> PreparedCatch;
  std::optional<FunctionId> PreparedSuccess;
  std::optional<FunctionId> PreparedBoundary;
  std::optional<model::TypeId> TryStateType;
  SourceRange Range;
};
struct HandlerClausePlan {
  OperationReference Operation;
  MatchPlanId ArgumentMatch;
  semantics::BindingId ResumeBinding;
  model::TypeId ResumeType;
  model::EffectRowId Effects;
  std::optional<FunctionId> PreparedFunction;
  std::optional<FunctionId> PreparedResume;
  SourceRange Range;
};
struct HandleRegion {
  ControlRegionId Id;
  FunctionId Function;
  std::optional<ControlRegionId> Parent;
  BlockId BodyEntry;
  std::vector<HandlerClausePlan> Clauses;
  BlockId SuccessEntry;
  BlockId Continuation;
  model::TypeId BodyResultType;
  model::TypeId ResultType;
  model::EffectRowId BodyEffects;
  model::EffectRowId HandledEffects;
  model::EffectRowId ResidualEffects;
  std::optional<FunctionId> PreparedBody;
  std::optional<FunctionId> PreparedSuccess;
  std::optional<FunctionId> PreparedEntry;
  std::optional<FunctionId> PreparedDispatch;
  std::optional<FunctionId> PreparedBoundary;
  std::optional<model::TypeId> RouterStateType;
  SourceRange Range;
};
using ControlRegion = std::variant<TryRegion, HandleRegion>;

using DecisionRootResult =
    std::expected<DecisionNodeId, std::vector<PassDiagnostic>>;
struct RaiseMatchErrorFailure {};
struct RaiseValueFailure { ValueId Exception; };
struct BranchFailure { BranchTarget Target; };
using MatchFailure = std::variant<RaiseMatchErrorFailure,
                                  RaiseValueFailure,
                                  BranchFailure>;
DecisionRootResult canonicalizeMatchPlan(Module &, MatchPlanId);
PassResult lowerDecisionRoot(Module &, FunctionId, BlockId DispatchBlock,
                             MatchPlanId, DecisionNodeId,
                             std::span<const ValueId> Inputs,
                             const MatchFailure &Failure);
PassResult advanceClosedModuleToGenericPrepared(Module &);
PassResult runPatternCanonicalization(Module &);
```

Task 6 adds one shared checked `deriveProjectionValueSpec` primitive used by
all pattern and aggregate projection builders. A structurally Trivial field
always produces Trivial with no provenance. `Retain` produces Owned with no
provenance and is legal only for a statically Shareable managed field.
`Borrow` of an Owned input produces
`BorrowProvenance{OwnedValue, Root = Input}`. For a rootless actual
Borrow-parameter input it produces
`BorrowProvenance{BorrowParameter, Root = Input}`; for an already-derived
Borrowed input it propagates that input's ultimate kind/root unchanged,
including rootless `StaticLifetime`, rooted `ScopedUniqueLoan`, and the
callee-local rooted/rootless forms of `ScopedUniqueLoanParameter`. No other
kind/root combination is constructible. The verifier recomputes this rule
from the input/result type and projection mode, requires the ultimate root to
dominate and remain live and unmutated for the complete projected use range,
and rejects a hand-built result with different ownership or provenance.
`Take` is forbidden for every projection except
`RuntimeTypePayloadProjection` from an Owned Sum on the proven-true edge. It
atomically transfers the actual `{Type, Word}` carrier to a value with the
selected alternative's canonical `resultOwnershipFor`—Trivial for scalar or
process-lifetime alternatives and Owned for managed alternatives—and clears
the Owned Sum source; a Borrowed Sum cannot be taken. The operation is
allocation-free, while the checked runtime wrapper
still rejects a malformed carrier before mutation. For a Sum input,
`RuntimeTypeIsTest` compares the carrier's actual descriptor
with the requested normalized alternative and
`RuntimeTypePayloadProjection` is legal only on its proven true edge. It
preserves the actual descriptor and uses the same Borrow/Retain rule above;
the source Sum owner remains live through every Borrow, while Retain invokes
the actual alternative's clone contract. The decision compiler never reads a
numeric tag or projects a tuple field. Tests cover scalar and managed
alternatives, exact Trivial-versus-Owned produced prefixes,
nested-source-sum normalization, retained payload escape, and
release of the unselected/failed scrutinee exactly once. Add a linear Resource
and continuation alternative taken successfully, nested-pattern failure after
take, borrowed-Sum take rejection, and exactly-once teardown.

Task 6 extends `Module` with module-owned `PatternArena Patterns`,
`std::vector<MatchPlan> MatchPlans`, `DecisionArena Decisions`, and
`std::vector<ControlRegion> ControlRegions`. It adds checked
`createMatchPlan`, `addMatchAlternative`, `addMatchRow`, and ID-only lookup
methods to `Builder`; no match/control record borrows an AST node or vector
element. AST lowering copies each handler clause's solved resume FunctionType
and body effect row plus the handled body's result type, whole handle result
type, complete solved body row, and solved residual row into the records above.
For every `try`, this Task 6 extension copies the solver-owned protected result
type, whole try result type, protected effect row, and residual effect row into
its now-defined `TryRegion`; it never infers them later from CFG reachability.
Printer/parser, generic-fragment remapping, and the control-region verifier
preserve and cross-check all four facts against the protected/catch/
continuation blocks.
A resume type is exactly
`OperationResult -> HandleResult`, with the solver-owned latent residual row;
it is not the raw `OperationResult -> BodyResult` continuation suffix.
`PreparedFunction`/`PreparedResume`/`PreparedBody`/`PreparedSuccess`/
`PreparedEntry`/`PreparedDispatch`/`PreparedBoundary` and `RouterStateType`
remain empty until Task 13. A plan's inputs
are typed/owned symbolic slots; concrete ValueIds are
supplied only by a dispatch/lowering call, so the same compiler works for a
case scrutinee, cursor element, caught exception, or generated handler
arguments. AST lowering gives every source match a persistent plan, ends its
dispatch block with `MatchDispatch{Plan, Inputs}`, and creates guard/body entry block
arguments in the alternative's exact `BindingOrder`. Pattern
canonicalization calls `canonicalizeMatchPlan` exactly once per module-owned
plan, stores `Plan.Root`, and replaces that terminator with
`DecisionSwitch{Plan, Root, Inputs}`. CFG lowering calls the phase-neutral
`lowerDecisionRoot`, resolves every leaf only through
`Plan.Alternatives[Alternative.Index]` and passes projected bindings to the
guard/body block arguments. Task 6 adds `MatchDispatch`, `DecisionSwitch`, and
`MatchGuardYield` to `Terminator`; AST lowering ends each guard's designated
exit with `MatchGuardYield{Alternative, Condition}`, and control-flow lowering
replaces it with a Bool `CondBranch` to that alternative's body or its unique
false-decision continuation. Canonicalization emits exactly one shared
`DecisionGuard` per guarded alternative. Catch rows live in
`TryRegion::CatchPlan`; they are never duplicated in an unowned clause vector.
AST lowering creates `TryRegion::CatchDispatch` immediately with exactly one
`ExceptionValue` block argument and a
`MatchDispatch{CatchPlan, {ExceptionArgument}}` terminator. Task 6 therefore
canonicalizes and CFG-lowers catches with every ordinary concrete dispatch,
passing `RaiseValueFailure{ExceptionArgument}` so an unmatched catch re-raises
that exact value. Ordinary function equations and `case` expressions pass
`RaiseMatchErrorFailure{}`. Task 12 only has to branch a Raised edge to this
durable block and pass the whole exception value.

`canonicalizeMatchPlan` owns the deterministic matrix algorithm but never
changes `ModulePhase`; `lowerDecisionRoot` owns projection/test/guard/body CFG
materialization, verifies that each supplied concrete input matches the plan
slot and every `MatchFailure` operand/target belongs to the same function, and
likewise never changes the phase. The whole-module pattern and control-flow
passes are enumerating wrappers over these APIs. Generator bindings store a
MatchPlanId and call `lowerDecisionRoot` with `RaiseMatchErrorFailure{}` when
their cursor item ValueId exists. Handler clauses store an argument
MatchPlanId; Task 13 remaps its blocks/FunctionId while extracting the
handler body, moves the argument plan into the generated handler-dispatch
function, and gives each success leaf a local block that invokes the prepared
body callable. It then lowers the plan with dispatch-local operation-argument
ValueIds and a `BranchFailure` to the next candidate or final unhandled-
request block. Neither later task rebuilds a matrix, interprets PatternIds
privately, or emits a cross-function branch.

`advanceClosedModuleToGenericPrepared` is the temporary and final no-generic
fast path: it accepts Canonical, proves that no open function, generic call,
or generic function value is reachable. Task 11 extends it to invoke the same
`runKeyOperationsPreparation` transaction for already-resolved local key
targets before changing the phase; Task 16 invokes the same candidate-
materialization extension point. It never bypasses either generated-function
freeze input. Task 15
reuses the same output phase after real local/imported specialization.
`runPatternCanonicalization` accepts only `GenericPrepared` and produces
`PatternCanonical`. The Canonical/GenericPrepared verifier permits
`MatchDispatch` plus `MatchGuardYield` and requires an empty decision arena;
PatternCanonical, `AsyncPlanned`, and `GeneratorLowered` permit
`DecisionSwitch` plus
`MatchGuardYield`. `ControlFlow` forbids every
`MatchDispatch`/`DecisionSwitch` and permits `MatchGuardYield` only in detached
guard blocks owned exclusively by a `HandleRegion` clause plan. Task 13's
effect-preparation pass consumes those blocks while outlining the handler;
`EffectPrepared` and later
phases permit none of the three. In every
phase it validates plan/function ownership, row/input arity, plan-local
alternative IDs, concrete dispatch input values and guard/body/entry/exit blocks,
guard Bool results, binding order/type/ownership, ranges, and reachability of
every row and alternative. AST lowering also records the innermost region ID
on every block; all generator and handler patterns are already canonical roots
before their delayed CFG materialization.

Parser, printer, operand/successor visitors, and verifier round trip every
arena/plan/region and every `PatternTestInst`/`PatternProjectInst`. Literal
text preserves Byte versus Char versus Int, validates Unicode scalar values
and UTF-8, length-escapes control/NUL bytes, and prints Float by its exact
IEEE-754 bits so NaNs and signed zero round trip. Dictionary-pattern keys are
restricted by Task 4 to typed literals or symbols, making lookup deterministic
and eliminating unordered backtracking. Test selection may use a hash only to
find candidates: language equality always compares the complete variant tag
and payload, so collisions cannot select a row. This is the canonical source
of catch/handler scope; Tasks 12-13 never reconstruct regions from CFG shape
or source text.

The specialization algorithm selects the leftmost refutable column,
partitions rows without reordering them, expands or-patterns only after their
binding sets have passed Task 4's semantic rule, and emits `DecisionFail` when
no row remains.

- [ ] **Step 4: Lower decisions to CFG and make no-match explicit**

`runPatternCanonicalization` accepts `GenericPrepared` and produces
`PatternCanonical`. At this milestone `ControlFlowLowering` accepts
`PatternCanonical` only when the generator verifier proves that no generator
operation exists; Task 11 inserts generator lowering before it. It changes
the phase to `ControlFlow`, replaces every `DecisionSwitch` terminator with
ordinary CFG. It materializes and caches one `PatternProjectInst` per
`MatchPlace` prefix, emits a typed `PatternTestInst` before each `CondBranch`,
and rewrites the alternative guard exit using that leaf's true-body and
false-decision targets. Detached handler-plan guard blocks are the sole
exception: they have no concrete inputs yet, remain explicitly owned by their
`HandleRegion`, and are skipped until Task 13 remaps them into the generated
handler and calls `lowerDecisionRoot`. The verifier derives every projected/result type from
the structural input type, checks indices/field names/constructors and explicit
projection ownership, recomputes `deriveProjectionValueSpec`, and rejects a
64-bit hash used as equality. Tests cover Owned, actual-parameter,
already-derived parameter, static, and scoped-unique-loan projection roots,
plus premature root release/mutation and forged provenance. Task 8
lowers scalar/symbol/string test primitives; Task 11 supplies the aggregate,
sequence, dictionary, record, constructor test/projection runtime lowering.
Define the payload before constructing it:

```cpp
struct MakeMatchErrorInst { SourceRange Range; };
```

For `RaiseMatchErrorFailure`, the failure block is equivalent to:

```cpp
const auto Exception = emit(
    Function, FailureBlock, MakeMatchErrorInst{Range},
    ownedValue(Module.Builtins.ExceptionValue, Range, "match.error"));
setTerminator(Function, FailureBlock, Raise{Exception}, Range);
```

For `RaiseValueFailure{Exception}`, it emits no allocation and terminates with
`Raise{Exception}`. For `BranchFailure{Target}`, it emits no allocation and
branches to that exact checked target. All three paths pass through the same
ownership/cleanup planning as any other outgoing edge; the lowering never
silently substitutes a null, zero, fresh exception, or success value.

`MakeMatchErrorInst` is added to `InstructionPayload` here and lowered through
the replacement runtime exception descriptor in Task 12; it does not depend
on the aggregate implementation introduced in Task 11. Its canonical text
codec length-encodes the range, the source-file remapper rewrites its file ID
without changing offsets, and the phase verifier admits the raw payload only
from `ControlFlow` until Task 11 wraps it in `CheckedRuntimeOp`; the checked
form then survives through `LlvmReady`. Parser/printer/remap/phase tests round
trip a non-default file/range and reject a foreign source domain.

There is no zero/null branch argument and no result PHI incoming edge from the
failure block.
Add exact regressions `Typed IR pattern canonicalization persists match rows
and leaf targets`, `Typed IR verifier rejects a match leaf from another plan`,
`Typed IR text: round trips String pattern literals`, `Typed IR pattern
canonicalization compares full strings after a hash collision`, and `Typed IR
control flow lowering emits every concrete pattern projection`. Add
`Typed IR symbol patterns compare canonical bytes across storage and hash
collisions` and `Typed IR dictionary symbol keys use descriptor equality`;
neither test may compare a producer-local integer or descriptor pointer.
Add exact policy regressions proving a non-exhaustive case and generator raise
ranged `MatchError`, an unmatched catch re-raises the original nominal
exception value without allocation, and a failed handler-clause pattern
branches to its supplied next-clause target rather than raising `MatchError`.

- [ ] **Step 5: Synthesize derivations without source generation**

Implement:

```cpp
enum class DerivedTrait { Show, Eq, Ord, Hash };

std::expected<std::vector<FunctionId>, DerivationDiagnostic>
deriveNominal(Module &Module, NominalTypeId Type,
              std::span<const DerivedTrait> Traits);
```

Generate Show/Eq/Ord/Hash bodies directly from nominal metadata using the
same typed patterns and decision compiler. Tests assert the module contains no
source string or reparse request and that recursive constructors call the
structurally selected trait implementation.
At this milestone the API is a reusable body-construction primitive exercised
with explicit nominal/trait arguments; it does not scan or consume
`ProjectedInterfaceSeed::Derivations`. Task 15's `runDerivations`, after its
trait resolver and mutable interface seed exist, is the sole source-integrated
caller and atomically installs the returned functions, generic seeds, and
instance metadata.

- [ ] **Step 6: Verify the canonical pattern pipeline**

```bash
python3 scripts/run-focused-tests.py ./out/build/x64-debug-linux/tests \
  -tc='Typed IR pattern canonicalization*,Typed IR control flow lowering*,Typed IR derivation*'
python3 scripts/run-focused-tests.py ./out/build/x64-debug-linux/tests -tc='*pattern*,*case*,*derive*'
git diff --check
```

Expected: all new decision trees are deterministic, all lowered CFGs verify,
and legacy pattern tests remain green.

- [ ] **Step 7: Commit canonical patterns and derivation**

```bash
git add include/yona/TypedIr src/TypedIr include/yona/Semantics/PatternAnalysis.h \
  src/Semantics/PatternAnalysis.cpp test/TypedIr \
  cmake/YonaComponents.cmake CMakeLists.txt
git add docs/superpowers/plans/2026-09-01-typed-ir-codegen-rewrite.md
git commit -m "feat: canonicalize typed patterns and clauses"
```

### Task 7: Add the universal ABI value and control-outcome substrate

**Files:**

- Create: `include/yona/Runtime/Core/Abi.h`
- Create: `src/Runtime/Core/Abi.c`
- Create: `include/yona/Runtime/Core/Outcome.h`
- Create: `src/Runtime/Core/Outcome.c`
- Create: `include/yona/Runtime/Core/Native.h`
- Create: `src/Runtime/Core/Native.c`
- Modify: `src/Runtime/Core/Internal.h`
- Modify: `src/Runtime/Core/Runtime.c`
- Create: `test/Runtime/AbiValueTest.cpp`
- Create: `test/Runtime/ControlOutcomeTest.cpp`
- Create: `test/Runtime/NativeEntryTest.cpp`
- Modify: `cmake/YonaComponents.cmake`
- Modify: `CMakeLists.txt`

**Interfaces:**

- Consumes: existing thread-safe `YonaRuntimeRetain`/`YonaRuntimeRelease`.
- Produces: `YonaAbiWord`, immutable `YonaAbiTypeDescriptor`, owned
  `YonaAbiValue`, explicit `YonaExecutionContext`, and movable/releasable
  `YonaControlOutcome`, plus one descriptor-checked synchronous native-leaf
  entry ABI.
- Coexistence: these are final replacement symbols. They do not call or adapt
  the legacy `YonaTypeDescriptor`, closure, exception, or async APIs.

- [ ] **Step 1: Write red carrier/outcome ownership tests**

Cover Float bit preservation, Bool normalization, unmanaged integers, managed
pointer retain/release, whole nominal exception movement, and exactly-once
outcome release:

```cpp
TEST_CASE("Runtime ABI Float carrier preserves every bit") {
  const double Input = -0.0;
  YonaAbiValue Value;
  YonaRuntimeAbiEncodeFloat(Input, &Value);
  double Output;
  REQUIRE(YonaRuntimeAbiDecodeFloat(&Value, &Output));
  CHECK(std::bit_cast<std::uint64_t>(Output) ==
        std::bit_cast<std::uint64_t>(Input));
}

TEST_CASE("Runtime outcome move clears the source owner") {
  RcProbe Probe;
  auto Value = Probe.ownedAbiValue();
  YonaControlOutcome Source;
  YonaControlOutcome Destination;
  YonaRuntimeOutcomeInitEmpty(&Source);
  YonaRuntimeOutcomeInitEmpty(&Destination);
  REQUIRE(YonaRuntimeOutcomeInitSuccessMove(&Source, &Value));
  YonaRuntimeOutcomeMove(&Destination, &Source);
  CHECK(Source.Kind == YONA_OUTCOME_EMPTY);
  YonaRuntimeOutcomeRelease(&Destination);
  CHECK(Probe.releaseCount() == 1);
}
```

Also move/release Raised, Performed, and Cancelled outcomes; a cancelled
outcome uses the canonical Unit descriptor. Force equal fingerprints on
structurally different recursive descriptors and require full-equivalence
rejection. The collision matrix must include same-shaped records whose field
labels differ, identical-looking nominal applications whose fully qualified
nominal identities differ, and effect rows whose fully qualified operation
identities differ. Also create two closed specializations of the same generic
operation, force their runtime hashes equal, place each in an otherwise
identical effect row, and require row inequivalence through the nested full
descriptor comparison. Create two symbol descriptors with forced equal
fingerprints and different UTF-8 spellings and require inequivalence; identical
spellings from distinct storage must compare equal. Cover every nonallocating `YonaAbiFailureCode`, plus exact
mapping and numeric equality of Trivial/Owned model results to the separate C
result enum; reject discriminant 2 and every larger value.
Reject unknown ABI versions and nonzero reserved fields for symbol,
effect-row, and operation descriptors before following any descriptor pointer.
Exercise the never-cancelled execution context, an acquire-reading test probe,
and rejection of null/unknown-version/nonzero-reserved contexts without
calling an untrusted probe.

- [ ] **Step 2: Run and confirm missing runtime ABI headers**

```bash
cmake --build --preset build-debug-linux --target tests -j2
python3 scripts/run-focused-tests.py ./out/build/x64-debug-linux/tests -tc='Runtime ABI*,Runtime outcome*,Runtime native entry*'
```

Expected: compilation fails because `Abi.h` and `Outcome.h` do not exist.

- [ ] **Step 3: Define the stable C ABI records**

`Abi.h` must contain fixed-width, C-compatible records and compile-time size
assertions on all supported 64-bit targets:

```c
#define YONA_RUNTIME_ABI_VERSION 2u

typedef uint64_t YonaAbiWord;

typedef uint32_t YonaAbiValueKind;
enum {
  YONA_ABI_UNIT = 0u, YONA_ABI_BOOL = 1u, YONA_ABI_BYTE = 2u,
  YONA_ABI_CHAR = 3u, YONA_ABI_INT = 4u, YONA_ABI_FLOAT = 5u,
  YONA_ABI_SYMBOL = 6u, YONA_ABI_STRING = 7u,
  YONA_ABI_CALLABLE = 8u, YONA_ABI_CONTINUATION = 9u,
  YONA_ABI_AGGREGATE = 10u, YONA_ABI_SEQUENCE = 11u,
  YONA_ABI_SET = 12u, YONA_ABI_DICTIONARY = 13u,
  YONA_ABI_ARRAY = 14u, YONA_ABI_CHANNEL = 15u,
  YONA_ABI_PROMISE = 16u, YONA_ABI_CURSOR = 17u,
  YONA_ABI_CURSOR_STEP = 18u, YONA_ABI_TASK_GROUP = 19u,
  YONA_ABI_RESOURCE = 20u, YONA_ABI_NOMINAL = 21u,
  YONA_ABI_EFFECT_REQUEST = 22u, YONA_ABI_EXCEPTION_VALUE = 23u,
  YONA_ABI_OWNED_SLOT_STATE = 24u,
  YONA_ABI_EXECUTION_CONTEXT = 25u,
  YONA_ABI_CONTROL_OUTCOME_STORAGE = 26u,
  YONA_ABI_SUM = 27u
};

typedef uint32_t YonaAbiParameterOwnership;
enum {
  YONA_ABI_PARAMETER_TRIVIAL = 0u,
  YONA_ABI_PARAMETER_BORROW = 1u,
  YONA_ABI_PARAMETER_CONSUME = 2u
};

typedef uint32_t YonaAbiResultOwnership;
enum {
  YONA_ABI_RESULT_TRIVIAL = 0u,
  YONA_ABI_RESULT_OWNED = 1u
};

typedef uint32_t YonaAbiTypeFlags;
enum {
  YONA_ABI_TYPE_MANAGED = 1u << 0,
  YONA_ABI_TYPE_ALWAYS_SHAREABLE = 1u << 1,
  YONA_ABI_TYPE_EXCEPTION_NOMINAL = 1u << 2
};

typedef struct YonaAbiTypeDescriptor YonaAbiTypeDescriptor;
struct YonaAbiTypeDescriptor {
  uint32_t AbiVersion;
  YonaAbiValueKind Kind;
  YonaAbiTypeFlags Flags;
  uint32_t WordCount;
  uint64_t StructuralFingerprint;
  uint64_t NominalFingerprint;
  const uint8_t *CanonicalBytes;
  uint64_t CanonicalByteCount;
  const char *DisplayName;
  const YonaAbiTypeDescriptor *const *Children;
  uint64_t ChildCount;
  bool (*IsShareable)(YonaAbiWord);
  bool (*TryRetain)(YonaAbiWord);
  void (*Release)(YonaAbiWord);
  void (*Format)(YonaAbiWord,
                 void (*)(const char *, uint64_t, void *), void *);
};

typedef struct {
  const YonaAbiTypeDescriptor *Type;
  YonaAbiWord Word;
} YonaAbiValue;

void YonaRuntimeAbiValueInitEmpty(YonaAbiValue *Value);
bool YonaRuntimeAbiValueIsEmpty(const YonaAbiValue *Value);
void YonaRuntimeAbiValueMove(YonaAbiValue *Destination,
                             YonaAbiValue *Source);
bool YonaRuntimeAbiValueClone(YonaAbiValue *Destination,
                              const YonaAbiValue *Source);
bool YonaRuntimeAbiValueTryRetain(const YonaAbiValue *BorrowedSource,
                                  YonaAbiValue *EmptyOutput);
bool YonaRuntimeAbiValueIsShareable(const YonaAbiValue *Value);
void YonaRuntimeAbiValueRelease(YonaAbiValue *Value);
bool YonaRuntimeAbiTypeEquivalent(const YonaAbiTypeDescriptor *Left,
                                  const YonaAbiTypeDescriptor *Right);
bool YonaRuntimeAbiValueConforms(
    const YonaAbiTypeDescriptor *Expected,
    const YonaAbiValue *Actual);

typedef struct {
  uint32_t AbiVersion;
  uint32_t Reserved;
  uint64_t StructuralFingerprint;
  const uint8_t *CanonicalUtf8;
  uint64_t ByteCount;
} YonaAbiSymbolDescriptor;
bool YonaRuntimeAbiSymbolEquivalent(const YonaAbiSymbolDescriptor *Left,
                                    const YonaAbiSymbolDescriptor *Right);

typedef struct YonaAbiEffectRowDescriptor YonaAbiEffectRowDescriptor;
typedef struct YonaEffectOperationDescriptor YonaEffectOperationDescriptor;
struct YonaAbiEffectRowDescriptor {
  uint32_t AbiVersion;
  uint32_t Reserved;
  uint64_t StructuralFingerprint;
  const uint8_t *CanonicalBytes;
  uint64_t CanonicalByteCount;
  const YonaEffectOperationDescriptor *const *Operations;
  uint64_t OperationCount;
  uint32_t MayRaise;
  uint32_t MayCancel;
};

struct YonaEffectOperationDescriptor {
  uint32_t AbiVersion;
  uint32_t Reserved;
  uint64_t RuntimeFingerprint;
  const uint8_t *CanonicalBytes;
  uint64_t CanonicalByteCount;
  const char *DisplayName;
  const YonaAbiTypeDescriptor *const *ArgumentTypes;
  const YonaAbiParameterOwnership *ArgumentOwnerships;
  uint64_t ArgumentCount;
  const YonaAbiTypeDescriptor *ResultType;
  YonaAbiResultOwnership ResultOwnership;
  const YonaAbiEffectRowDescriptor *Effects;
};

bool YonaRuntimeAbiEffectRowEquivalent(
    const YonaAbiEffectRowDescriptor *Left,
    const YonaAbiEffectRowDescriptor *Right);
bool YonaRuntimeAbiEffectRowSubset(
    const YonaAbiEffectRowDescriptor *Subset,
    const YonaAbiEffectRowDescriptor *Superset);
bool YonaRuntimeEffectOperationEquivalent(
    const YonaEffectOperationDescriptor *Left,
    const YonaEffectOperationDescriptor *Right);
```

Descriptor emission uses this exhaustive mapping; `M` means `MANAGED` and
`AS` means `ALWAYS_SHAREABLE`. “Composite” installs the ordinary RC release
callback and an instance-aware `TryRetain` that succeeds only when the frozen
instance contains no linear child. “Linear” installs the release callback and
an always-false `TryRetain`. A dash means both callbacks are null.

| Structural type/form | ABI kind | Flags / callbacks |
|---|---|---|
| Unit | `UNIT` | none; word must be zero |
| Bool | `BOOL` | none; word is exactly 0 or 1 |
| Byte | `BYTE` | none; high 56 bits are zero |
| Char | `CHAR` | none; Unicode scalar, high 32 bits zero, no surrogate |
| Int | `INT` | none; exact two's-complement 64 bits |
| Float | `FLOAT` | none; exact IEEE-754 binary64 bits |
| Symbol | `SYMBOL` | none; immutable process-lifetime descriptor pointer |
| String | `STRING` | M+AS; string retain/release |
| Sum | `SUM` | constraint-only closed alternative set; never stored in `YonaAbiValue.Type`, callbacks null |
| Tuple or Record | `AGGREGATE` | M; composite |
| Sequence | `SEQUENCE` | M; composite |
| Set | `SET` | M; composite |
| Dictionary | `DICTIONARY` | M; composite |
| Nominal ADT | `NOMINAL` | M+`EXCEPTION_NOMINAL`; composite using the selected constructor layout |
| Array | `ARRAY` | M; composite; mutation remains unique-or-copy |
| Channel | `CHANNEL` | M+AS; channel retain/release |
| Promise | `PROMISE` | M; contract-sensitive outcome-task try-retain/release; AS only for a Keep-safe closed Promise contract |
| Cursor | `CURSOR` | M; linear cursor release |
| CursorStep | `CURSOR_STEP` | M; linear step release |
| OutcomeTaskGroup | `TASK_GROUP` | M; linear to generated code, group release |
| Resource | `RESOURCE` or `CHANNEL` | policy comes from its validated `ResourceDeclaration`: Linear is M with null TryRetain and the exact release/finalizer; AlwaysShareable is M+AS with the exact TryRetain and release; ChannelSender/ChannelReceiver select `CHANNEL` and GenericResource selects `RESOURCE` |
| OwnedSlotState | `OWNED_SLOT_STATE` | M; linear descriptor drop/release |
| Function, except Continuation convention | `CALLABLE` | M; instance-sensitive callable retain/release |
| Function with Continuation convention | `CONTINUATION` | M; linear continuation release |
| AbiOpaque EffectRequest | `EFFECT_REQUEST` | M; linear request release |
| AbiOpaque ExceptionValue | `EXCEPTION_VALUE` | constraint-only open existential; never stored in `YonaAbiValue.Type`, callbacks null |
| AbiOpaque ExecutionContext | `EXECUTION_CONTEXT` | unmanaged Borrow only; callbacks null |
| AbiOpaque CallableInvocationEnvironment | none | internal hidden Borrow-only address; never emitted as a public descriptor or `YonaAbiValue` child |
| AbiOpaque ContinuationBoundaryContext | none | internal hidden Borrow-only nullable address; never emitted as a public descriptor or `YonaAbiValue` child |
| AbiOpaque ControlOutcome | `CONTROL_OUTCOME_STORAGE` | no ordinary value; storage-only verifier rule |
| TypeParameter or any unknown form | none | rejected when runtime-reachable |

DirectYona, ClosureEntry, EffectOperation, AsyncAdapter, NativeExtern, and
ExportedC FunctionTypes all map to `CALLABLE`; first-class emission requires
the generated universal adapter associated with that exact convention.
Continuation alone maps to `CONTINUATION`. Canonical children, function
convention, ownership, mutability, and nominal identity remain in the full
canonical bytes, so equal ABI kind never implies equal type. Flags must match
the table exactly, reserved bits are zero, `MANAGED` requires non-null release,
and only an AS row may use an infallible retain. The descriptor emitter rejects
`CallableInvocationEnvironment` and `ContinuationBoundaryContext` anywhere in
a descriptor child graph; only their documented generated hidden parameters
may carry them. Add a table-driven ABI test
for every row and every Function convention, including the non-emitting hidden
environment row, plus invalid Byte/Bool/Char words, illegal callbacks/flags,
unknown kinds/reserved bits, and a same-kind unequal-canonical-bytes collision.

For `ResourceType`, representation selection takes the verified
`typed_ir::Module` and performs an exact key lookup in `Module::Resources`;
generic argument arity is checked against that row before descriptor
instantiation. The emitter obtains ABI kind, AS flag, and exact callback
symbols only from the row. It has no default-linear fallback, source-name
switch, interface-catalog lookup, or manifest singleton. The same check runs
after TIRF decode and specialization, so a fragment cannot smuggle a resource
type without its closed policy declaration.

`Sum` is an unboxed closed union constraint. Its descriptor children are the
exact flattened, canonical-order alternatives and its canonical bytes retain
that complete set. `YonaRuntimeAbiValueConforms(ExpectedSum, Actual)` accepts
exactly when `Actual.Type` is fully equivalent to one child; the Sum marker is
never legal as `Actual.Type`, and conformance never widens one Sum to another.
Clone, release, formatting, callable staging, and pattern payload access always
dispatch through the actual alternative descriptor. Forced equal hashes,
nested/duplicate source sums, different alternative sets, marker-as-actual,
and malformed child graphs are explicit ABI tests.

Every runtime boundary that compares a concrete `YonaAbiValue` with a static
expected type calls this same `YonaRuntimeAbiValueConforms`: aggregate fields,
sequence elements, set/dictionary keys and values, callable captures/
arguments/results, effect and continuation payloads, outcomes, Promises/tasks,
channels, and generated adapters. It performs full equivalence normally, the
closed child-membership rule for an expected Sum, and the nominal-exception
rule below for expected ExceptionValue. Descriptor-to-descriptor identity,
interning, and deduplication remain exact and never use conformance. Coverage
includes Sum inside an aggregate/sequence plus Promise and effect round trips
with both scalar and managed alternatives.

`ExceptionValue` is an unboxed open existential. Its static descriptor is used
only as an expected constraint in signatures and has no carrier callbacks. An
SSA value with static `Builtins.ExceptionValue` uses `AbiValue`
representation; at runtime its `{Type, Word}` always holds the actual closed
nominal exception descriptor (flagged `EXCEPTION_NOMINAL`) and payload word.
Yona has no separate exception declaration: every ordinary source
`NominalType` descriptor carries `EXCEPTION_NOMINAL`, because any ADT value may
be raised. ResourceType and the other distinct ABI families never acquire that
flag. The flag is therefore derived from structural kind, participates in
descriptor validation/equivalence, and is not another semantic/v2 source bit.
`YonaRuntimeAbiValueConforms` accepts full descriptor equivalence normally,
or accepts an actual flagged nominal when Expected is the ExceptionValue
marker; no other widening exists. Clone/release/format always dispatch through
the actual descriptor. Callable adapters, outcomes, catch routing, and
cross-module ABI validation use conformance at the existential boundary and
exact equivalence everywhere else. The verifier rejects storing the marker in
`YonaAbiValue.Type`, a deliberately malformed synthetic nominal missing the
required flag under the existential, or a marker as an ordinary managed
pointer. Tests raise nullary/scalar/heap/multifield
nominals through direct and universal calls, cross a module boundary, catch,
rethrow, clone when Shareable, and release exactly once.

The complete immutable operation-descriptor layout and its equivalence
primitive belong to the Task 7 ABI substrate even though effect requests are
introduced in Task 13. `Abi.c` implements both operation and row equivalence,
so Task 7's forced-collision tests compile and link without a future runtime
component. Task 13 consumes this record; it does not redeclare or redefine it.

Cloneability is an instance property. `YonaRuntimeAbiValueTryRetain` requires
an initially Empty, byte-disjoint output, validates the source's actual
descriptor, copies a Trivial value directly, and otherwise checks
`IsShareable` then calls the actual descriptor's `TryRetain` exactly once. A
null/false callback leaves the output Empty and the source unchanged; true
copies the source's exact `{Type, Word}` into the output. In particular, an
`AbiValue` whose static type is `Sum` or `ExceptionValue` dispatches through
the concrete alternative/nominal descriptor stored in `Source.Type`, never
through the static marker descriptor. `YonaRuntimeAbiValueClone` first rejects
`Destination == Source` unchanged; only distinct storage is released before
delegating to this helper. The
`ALWAYS_SHAREABLE` flag is only a verified fast-path promise that `TryRetain`
cannot fail for a structurally valid instance; absence of that flag does not
by itself reject cloning. Task 7 tests only Trivial values and synthetic managed
probe objects whose descriptor callbacks read a configurable instance bit;
clone success and unchanged-source failure are therefore implementable without
future callable/collection APIs. Task 9 binds this contract to ordinary,
partial, and recursive callables; Task 11 binds it to composites. Continuations
always reject retain. No staging/apply path decides cloneability from a static
type flag.

Every managed heap object stores an atomic instance `Shareable` bit adjacent
to its RC header; this task changes `Core/Internal.h` and the canonical
allocator/header initialization in `Core/Runtime.c`, then exercises acquire
reads and retain-time rechecks with the synthetic probe object. Linear probes
return false; AS probes return true without object inspection. Static flags
alone never decide an instance. Task 9 defines how callable construction sets
the bit. Task 11 defines staged composite computation, path-copy/in-place
publication, nested reads, concurrent retain/update ordering, and restoration
after replacing the final linear child; those tests live with the APIs that can
actually construct and update such values.

A Symbol ABI word is a pointer to an immutable process-lifetime
`YonaAbiSymbolDescriptor`, not a per-module dense integer. LLVM emits one
descriptor per canonical UTF-8 spelling in a module; separately compiled
modules remain equal because `YonaRuntimeAbiSymbolEquivalent` uses the hash
only as a prefilter and compares byte count/content. Symbol values are Trivial
and require no runtime interner, allocation, retain, or release. The
replacement Eq/Hash/Show adapters use this descriptor contract; legacy i64
symbol helpers remain oracle-only until Task 17.

`Outcome.h` defines an empty state so move operations are total:

```c
typedef bool (*YonaCancellationProbe)(const void *BorrowedState);
typedef struct {
  uint32_t AbiVersion;
  uint32_t Reserved;
  const void *BorrowedState;
  YonaCancellationProbe Probe;
} YonaExecutionContext;

const YonaExecutionContext *YonaRuntimeNeverCancelledContext(void);
bool YonaRuntimeExecutionCancellationRequested(
    const YonaExecutionContext *BorrowedContext);

typedef uint32_t YonaControlOutcomeKind;
enum {
  YONA_OUTCOME_EMPTY = 0u,
  YONA_OUTCOME_SUCCESS = 1u,
  YONA_OUTCOME_RAISED = 2u,
  YONA_OUTCOME_PERFORMED = 3u,
  YONA_OUTCOME_CANCELLED = 4u
};

typedef struct {
  YonaControlOutcomeKind Kind;
  uint32_t Reserved;
  YonaAbiValue Payload;
} YonaControlOutcome;

void YonaRuntimeOutcomeInitEmpty(YonaControlOutcome *Outcome);
bool YonaRuntimeOutcomeInitSuccessMove(YonaControlOutcome *EmptyOutcome,
                                       YonaAbiValue *OwnedValue);
bool YonaRuntimeOutcomeInitRaisedMove(YonaControlOutcome *EmptyOutcome,
                                      YonaAbiValue *OwnedValue);
bool YonaRuntimeOutcomeInitPerformedMove(YonaControlOutcome *EmptyOutcome,
                                         YonaAbiValue *OwnedValue);
bool YonaRuntimeOutcomeInitCancelled(YonaControlOutcome *EmptyOutcome);
void YonaRuntimeOutcomeMove(YonaControlOutcome *Destination,
                            YonaControlOutcome *Source);
bool YonaRuntimeOutcomeClone(YonaControlOutcome *Destination,
                             const YonaControlOutcome *Source);
void YonaRuntimeOutcomeRelease(YonaControlOutcome *Outcome);
void YonaRuntimeReportUnhandled(const YonaControlOutcome *Outcome);

typedef uint32_t YonaAbiFailureCode;
enum {
  YONA_ABI_FAILURE_OUT_OF_MEMORY = 1u,
  YONA_ABI_FAILURE_DESCRIPTOR_MISMATCH = 2u,
  YONA_ABI_FAILURE_NULL_CALLABLE = 3u,
  YONA_ABI_FAILURE_NON_CALLABLE_RESULT = 4u,
  YONA_ABI_FAILURE_INVALID_OPERATION = 5u,
  YONA_ABI_FAILURE_SUBMISSION = 6u,
  YONA_ABI_FAILURE_ARRAY_CONTRACT = 7u,
  YONA_ABI_FAILURE_ARITHMETIC_CONTRACT = 8u
};
bool YonaRuntimeOutcomeInitAbiFailure(YonaControlOutcome *EmptyOutcome,
                                      YonaAbiFailureCode Code);
```

Initializers require an Empty destination and clear every moved source.
`Move` first rejects a source/destination self-alias unchanged, then releases
a non-Empty destination and clears its distinct source. `Clone` likewise
checks `Destination == Source` before releasing anything and returns false
with that one value unchanged; for distinct storage it releases the
destination, then delegates payload copying to
`YonaRuntimeAbiValueTryRetain`. A failed retain returns `false`, leaves the
destination Empty, and leaves the source unchanged. This makes
linear-resource cloning impossible without confusing the structural type
with one particular instance's shareability.
Release clears before invoking the release callback so recursive diagnostics
cannot double-release. Null inputs and source/destination aliasing are API
contract violations asserted in Debug builds and rejected without mutation in
Release builds. Runtime ABI tests exercise self-move/self-clone for both
`YonaAbiValue` and `YonaControlOutcome`, including a managed source whose
release callback would detect an accidental precheck release.

`YonaExecutionContext` is the explicit hidden execution carrier for generated
cancellation points; it is never thread-local. Version/reserved fields are
validated before following `BorrowedState` or `Probe`. The process-lifetime
never-cancelled context has a null state and a static false probe. An async
worker instead supplies a stack context whose borrowed state is its retained
task and whose probe performs an acquire load of that task's cancellation
flag; the context never outlives the universal invocation. Every nested
outcome-capable direct or universal call forwards the same non-null pointer.

Use C11/C++-portable assertion macros to require `sizeof(YonaAbiValue) == 16`,
`alignof(YonaAbiValue) == 8`, `sizeof(YonaControlOutcome) == 24`, and the exact
size/alignment/`offsetof` values for `YonaExecutionContext` and every public
field on supported 64-bit targets. Public
counts/tags are fixed-width; `NominalFingerprint`, not `DisplayName`, is
runtime nominal identity.
Structural/nominal fingerprints use the same versioned canonical-byte FNV-1a
scheme as operation declarations and are always confirmed against the full
descriptor when a collision could affect dispatch.
Every emitted descriptor points at immutable process-lifetime canonical bytes
from Task 2's isolated structural encoding. Those bytes include record field
labels, fully qualified nominal identities, child structure, operation FQNs,
ownership, and control-effect facts. `YonaRuntimeAbiTypeEquivalent` and
effect-row equivalence use fingerprints only as a fast rejection, then compare
canonical byte counts/content; pointer, display-name, and fingerprint equality
alone never establish identity.
Runtime effect-row descriptors are always closed. Their operation array holds
immutable descriptors for the closed operation instances in canonical
key/signature order; no declaration fingerprint or open-row flag crosses the
C ABI. Every symbol/effect-row/operation descriptor requires
`AbiVersion == YONA_RUNTIME_ABI_VERSION` and zero reserved fields before any
pointer is dereferenced. Row equivalence first compares its own canonical bytes/facts, then
requires the same operation count and
`YonaRuntimeEffectOperationEquivalent` for every corresponding descriptor.
`YonaRuntimeAbiEffectRowSubset` performs a canonical merge walk and requires
every fully equivalent operation in the first row to occur in the second plus
the same implication for `MayRaise`/`MayCancel`; it never compares only keys
or fingerprints.
The operation hash is only that nested comparison's prefilter, so two
specializations of the same generic operation with a forced equal hash remain
distinguishable by their complete closed bytes. Descriptor arrays and entries
have process lifetime. Operation equivalence is nonrecursive over descriptor
pointers: its canonical closed-graph bytes already contain the residual row,
so Debug integrity checks use an explicit visited-pair set when walking cyclic
row/operation descriptor SCCs.
`WordCount` is `0` only for Unit and `1` for every current carrier; aggregates
are descriptor-managed references, not inline multiword values. Reject any
other count at ABI version 2.
An Empty `YonaAbiValue` is exactly `{NULL, 0}`. Value move first releases a
non-Empty distinct destination; clone releases the destination and rejects a
non-Shareable source by leaving the destination Empty; release clears before
calling the descriptor. These primitives are the only way runtime containers,
callables, effects, and tasks move carrier ownership.
`YonaRuntimeOutcomeInitAbiFailure` uses a process-lifetime static nominal
descriptor and stores the fixed failure code in its trivial word. It performs
no allocation, so OOM, null-callable, and descriptor-validation paths can
always produce a complete Raised outcome.

`Native.h` fixes the ordinary synchronous native-leaf boundary instead of
letting every stdlib C function invent a platform-sensitive signature:

```c
typedef struct {
  const YonaAbiTypeDescriptor *ExpectedType;
  YonaAbiParameterOwnership Ownership;
  uint32_t Reserved;
  union {
    const YonaAbiValue *BorrowedValue; /* Trivial or Borrow */
    YonaAbiValue *OwnedValue;          /* Consume */
  } Storage;
} YonaAbiNativeArgument;

typedef struct {
  uint32_t AbiVersion;
  uint32_t Reserved;
  uint64_t StructuralFingerprint;
  const uint8_t *CanonicalBytes;
  uint64_t CanonicalByteCount;
  const YonaAbiTypeDescriptor *const *ParameterTypes;
  const YonaAbiParameterOwnership *ParameterOwnerships;
  uint64_t ParameterCount;
  const YonaAbiTypeDescriptor *ResultType;
  YonaAbiResultOwnership ResultOwnership;
  const YonaAbiEffectRowDescriptor *Effects;
} YonaAbiNativeFunctionDescriptor;

typedef bool (*YonaAbiCheckedDirectNativeEntryV2)(
    const YonaAbiNativeFunctionDescriptor *Descriptor,
    YonaAbiNativeArgument *Arguments, uint64_t ArgumentCount,
    YonaAbiValue *EmptyResult,
    YonaControlOutcome *EmptyFailure);
```

This ABI is only for a source-semantically direct private native leaf. Its
effect row must be closed and empty; a fallible language operation therefore
returns an ordinary explicit `Result` value. A source operation that can
raise, cancel, perform, or suspend uses its dedicated checked Outcome opcode
and exact runtime API from Tasks 13-14 instead. The only bypass is the closed
`StableExternal` allowlist in Task 15: nine unary libm functions with literal
`double(double)` C signatures. Hot array, iterator, key-query, and String
storage primitives use their dedicated checked intrinsic ABIs instead of this
generic leaf boundary.

The descriptor is the complete closed FunctionType projected through Task 2;
the runtime validates ABI version, reserved fields, canonical bytes, argument
count/order, every ownership discriminant and concrete carrier before calling
leaf code. All argument records and their referenced value slots, result, and
failure storage must be pairwise byte-disjoint. Trivial/Borrow slots are
immutable. Consume slots remain owned until the leaf has validated and staged
all allocations. `false` is pre-commit: every argument and the Empty result
remain unchanged, and exactly one reserved nonallocating diagnostic may be
written to the failure slot. `true` is committed: every Consume slot is
cleared exactly once, exactly one result matching `ResultType` and
`ResultOwnership` is published, and failure stays Empty. No leaf may encode a
source exception as `false` or retain a Borrow beyond the call. The runtime
test calls synthetic zero/one/many-argument leaves through the typedef with
scalar/managed/Consume values, descriptor collisions, overlap, OOM, and
postcommit ownership probes. Task 15's closed stdlib manifest and Task 16's C
ABI conformance test assign every surviving CheckedDirect leaf symbol to this
exact function-pointer type.

- [ ] **Step 4: Implement scalar encoding and managed descriptor operations**

Use `memcpy` for Float carriers and normalize Bool to `0`/`1`:

```c
void YonaRuntimeAbiEncodeFloat(double Value, YonaAbiValue *Output) {
  Output->Type = &YonaRuntimeFloatAbiType;
  memcpy(&Output->Word, &Value, sizeof Value);
}

bool YonaRuntimeAbiDecodeFloat(const YonaAbiValue *Value, double *Output) {
  if (Value == NULL || Output == NULL ||
      Value->Type != &YonaRuntimeFloatAbiType)
    return false;
  memcpy(Output, &Value->Word, sizeof *Output);
  return true;
}
```

Always-shareable managed descriptors use a `TryRetain` wrapper around
`YonaRuntimeRetain` plus `YonaRuntimeRelease`; instance-sensitive managed
descriptors use their own fallible retain callback. Trivial descriptors leave
both callbacks null. `YonaRuntimeReportUnhandled` formats
through the descriptor instead of interpreting payload bits.

- [ ] **Step 5: Run the runtime ABI gate under sanitizers**

```bash
python3 scripts/run-focused-tests.py ./out/build/x64-debug-linux/tests -tc='Runtime ABI*,Runtime outcome*,Runtime native entry*'
python3 scripts/quality.py --build-dir out/build/x64-debug-linux \
  --preset x64-debug-linux --jobs 2 sanitize
git diff --check
```

Expected: all carrier/outcome tests pass and the sanitizer gate reports no
leak, double release, alignment, or invalid-read failure.

- [ ] **Step 6: Commit the substrate**

```bash
git add include/yona/Runtime/Core/Abi.h src/Runtime/Core/Abi.c \
  include/yona/Runtime/Core/Outcome.h src/Runtime/Core/Outcome.c \
  include/yona/Runtime/Core/Native.h src/Runtime/Core/Native.c \
  src/Runtime/Core/Internal.h src/Runtime/Core/Runtime.c \
  test/Runtime/AbiValueTest.cpp test/Runtime/ControlOutcomeTest.cpp \
  test/Runtime/NativeEntryTest.cpp \
  cmake/YonaComponents.cmake CMakeLists.txt
git add docs/superpowers/plans/2026-09-01-typed-ir-codegen-rewrite.md
git commit -m "feat: add universal value and outcome abi"
```

### Task 8: Build isolated LLVM lowering and a test-only execution path

**Files:**

- Create: `include/yona/TypedIr/Pipeline.h`
- Create: `src/TypedIr/Pipeline.cpp`
- Create: `include/yona/TypedIr/Passes/RepresentationSelection.h`
- Create: `src/TypedIr/Passes/RepresentationSelection.cpp`
- Create: `include/yona/Codegen/Llvm/LoweringOptions.h`
- Create: `include/yona/Codegen/Llvm/ModuleLowerer.h`
- Create: `include/yona/Codegen/Llvm/FunctionLowerer.h`
- Create: `include/yona/Codegen/Llvm/BlockLowerer.h`
- Create: `include/yona/Codegen/Llvm/TypeLowering.h`
- Create: `src/Codegen/Llvm/ModuleLowerer.cpp`
- Create: `src/Codegen/Llvm/FunctionLowerer.cpp`
- Create: `src/Codegen/Llvm/BlockLowerer.cpp`
- Create: `src/Codegen/Llvm/TypeLowering.cpp`
- Create: `src/Codegen/Llvm/Finalization.cpp`
- Create: `src/Codegen/Llvm/Optimizer.cpp`
- Create: `src/Codegen/Llvm/ObjectEmitter.cpp`
- Create: `test/Support/TypedIrExecution.h`
- Create: `test/Support/TypedIrExecution.cpp`
- Create: `test/Codegen/LlvmLoweringTest.cpp`
- Create: `test/TypedIr/RepresentationSelectionTest.cpp`
- Create: `test/Codegen/TypedIrExecutionTest.cpp`
- Create: `test/Fixtures/TypedIr/Scalars/arithmetic.yona`
- Create: `test/Fixtures/TypedIr/Scalars/arithmetic.expected`
- Create: `test/Fixtures/TypedIr/Scalars/control_flow.yona`
- Create: `test/Fixtures/TypedIr/Scalars/control_flow.expected`
- Create: `test/Fixtures/TypedIr/Scalars/symbol_literal.yona`
- Create: `test/Fixtures/TypedIr/Scalars/symbol_literal.expected`
- Modify: `include/yona/TypedIr/Instruction.h`
- Modify: `include/yona/TypedIr/TypedIr.h`
- Modify: `include/yona/TypedIr/Builder.h`
- Modify: `src/TypedIr/TypedIr.cpp`
- Modify: `src/TypedIr/Builder.cpp`
- Modify: `src/TypedIr/Verifier.cpp`
- Modify: `src/TypedIr/Printer.cpp`
- Modify: `src/TypedIr/Parser.cpp`
- Modify: `test/TypedIr/BuilderTest.cpp`
- Modify: `test/TypedIr/VerifierTest.cpp`
- Modify: `test/TypedIr/PrinterParserTest.cpp`
- Modify: `cmake/YonaComponents.cmake`
- Modify: `CMakeLists.txt`

**Interfaces:**

- Consumes: only `ModulePhase::LlvmReady` Typed IR and LLVM target options.
- Produces: `runRepresentationSelection`, `runTypedIrPipeline`,
  `runLlvmReadinessVerification`,
  `LlvmModuleLowerer::lower`, object emission, and `executeTypedIrForTest`;
  none is wired to production tools yet.
- Isolation: `LlvmFunctionLowerer` owns the only `ValueId -> llvm::Value *`
  map; `LlvmBlockLowerer` owns the only active insertion point.

- [ ] **Step 1: Write red scalar/CFG lowering tests at O0-O3**

Build verified scalar modules directly and assert LLVM types, PHIs, direct
call signatures, and refusal of malformed/high-level IR:

```cpp
TEST_CASE("LLVM lowering verifies scalar CFG at every optimization level") {
  const auto Input = lowerForTest(
      "let flag = true in if flag then 40 + 2 else 0");
  for (int Level = 0; Level <= 3; ++Level) {
    const auto Result = lowerToLlvm(Input, {.OptimizationLevel = Level});
    REQUIRE(Result.has_value());
    CHECK_FALSE(llvm::verifyModule(*Result->Module, &llvm::errs()));
    CHECK(irText(*Result->Module).find("phi i64") != std::string::npos);
  }
}
```

Add a verifier test proving LLVM lowering rejects modules not in
`ModulePhase::LlvmReady` before allocating LLVM functions.
Add top-level sentinel `Typed IR execution: scalar CFG agrees at O0 through
O3`, which executes the same verified module and makes the execution prefix
independently reachable. Add exact cases `Typed IR text: Symbol literal keeps
canonical UTF-8 spelling` and `Typed IR execution: Symbol literal and pattern
agree across modules`; the latter links two separately emitted test objects
whose equal spellings reside at different addresses, plus a forced-hash
collision pair.

- [ ] **Step 2: Run and confirm the new lowerer is absent**

```bash
cmake --build --preset build-debug-linux --target tests -j2
python3 scripts/run-focused-tests.py ./out/build/x64-debug-linux/tests -tc='LLVM lowering*,Typed IR execution*'
```

Expected: compile failure for `Codegen/Llvm/ModuleLowerer.h`.

- [ ] **Step 3: Implement explicit phase transitions for the scalar subset**

Expose:

```cpp
struct PipelineOptions {
  bool EnableAccelerator = true;
  bool StrictAccelerator = false;
  bool CollectPhaseTrace = false;
};
struct PipelineTraceEntry {
  ModulePhase Phase;
  std::vector<FunctionId> FunctionIds;
  std::vector<CallableDescriptorId> CallableIds;
  std::uint64_t FunctionsWithRuntimeEffects;
  std::uint64_t CallablesWithRuntimeEffects;
};
struct PipelineOutput {
  Module Ir;
  std::vector<PipelineTraceEntry> Trace;
};
using PipelineResult =
    std::expected<PipelineOutput, std::vector<PipelineDiagnostic>>;

PipelineResult runTypedIrPipeline(Module GenericPrepared,
                                  const PipelineOptions &Options);
PassResult runLlvmReadinessVerification(Module &CleanupLowered);
```

When tracing is disabled, `Trace` is empty and the pipeline keeps no
intermediate module copies. When enabled for tests/diagnostics, append one
entry only after each phase verifier succeeds; IDs are stored in numeric order
and the two counts report nonempty runtime-row slots. The trace owns only these
small identity snapshots, never pointers into or copies of a mutable Module.
Production compilation consumes `PipelineOutput::Ir` and ignores the trace.

`runLlvmReadinessVerification` is the sole transition from `CleanupLowered`
to `LlvmReady`. It first runs, without mutation, the complete closed-type,
domain/generation, phase-allowlist, function/block/value/edge SSA and
dominance, representation, ownership-poststate, and cleanup-completeness
verifiers. Any diagnostic returns failure with canonical bytes, IDs, arenas,
and phase unchanged. Only after all checks succeed does it infallibly change
the phase; that phase field is private to named passes, and the pass then runs
the `LlvmReady` classifier before returning. Its successful structural diff is
therefore exactly the phase value. `LlvmModuleLowerer::lower` independently
reruns `verifyModule(Input, LlvmReady)` before allocating LLVM state. Tests
reject direct phase relabeling, a text module whose tag says LlvmReady while
its body satisfies only CleanupLowered, and every individual failed final
check; each failure is byte-for-byte unchanged.

For operations already canonical at this milestone, advance through every
phase in the authoritative order from Global Constraints while running
`verifyModule` after each transition. A not-yet-implemented pass may be a
verifier-proven no-op only for this scalar subset. Reject remaining
`LexicalRefInst`, `MatchDispatch`, `DecisionSwitch`, generator operations,
Raise/Perform/Resume, cleanup regions, or unresolved ownership before
`LlvmReady`; later tasks add the passes that eliminate them.

Run `runRepresentationSelection(Module &)` after control-outcome lowering and
immediately before the ownership passes, transitioning from
`ControlOutcomeLowered` to `RepresentationSelected`. The mapping is total for
all closed structural types from the start: Unit -> `NoCarrier`, Bool -> `I1`,
Byte -> `I8`, Char -> `I32`, Int -> `I64`, Float -> `F64`, Symbol ->
`UnmanagedPointer`, String,
function/continuation, tuple, sequence, set, dictionary, record,
nominal, array, channel, promise/task, Cursor, CursorStep, OutcomeTaskGroup,
OwnedSlotState, resource, and EffectRequest -> `ManagedPointer`, the
Sum and the existential ExceptionValue -> `AbiValue`, and internal raw ABI addresses ->
`UnmanagedPointer`. `AbiOpaqueKind::ExecutionContext` and
`CallableInvocationEnvironment` and
`ContinuationBoundaryContext` are the only named internal raw addresses.
All map exactly to a Borrowed `UnmanagedPointer`, may appear only as their
documented hidden dominating function parameter, and have no retain/release,
owning use, escape, store, capture, return, source-level constructor, public
ABI type descriptor, or `YonaAbiValue` carrier. A callable
environment parameter is present on every universal adapter, on every
RecursiveMember direct entry even when its external capture vector is empty,
and on an ordinary generated closure entry whenever its descriptor has
environment storage. Its optional borrowed recursive-group view is non-null
and descriptor-matched only for a RecursiveMember and is null for an ordinary
generated root or runtime partial. Direct-call
environment propagation remains restricted to verified recursive intra-SCC
calls. The continuation-boundary pointer is a separate hidden call channel,
never a callable-environment field; Tasks 9 and 13 install and verify its
exact forwarding on every Yona direct/generated call, including DirectReturn.
`ControlOutcome` is storage, never an ordinary SSA value;
the verifier rejects it outside the dedicated lowering operations. A type
parameter/open row is a hard error only when reachable from an executable
Function, Value, operation instance, or emitted descriptor; unused generic
declaration types may remain in the shared TypeTable after Task 15 extracts
their bodies. Any unknown opaque kind is a hard error. Add one
table-driven `RepresentationSelectionTest` row for every structural variant
and builtin opaque kind, including Cursor/CursorStep linear non-Shareable
rows. Later tasks add producers/consumers for these
representations but never alter this mapping or teach LLVM to infer it.

`InjectSumInst` is legal from Canonical through `LlvmReady` and is the only
sum injection. Its declared `Sum` must be the instruction result type and the
operand type must be one exact alternative. A Trivial operand is copied; an
Owned operand is transferred into the result's dynamic `YonaAbiValue` and may
not be used afterward. The result is `Owned` under the uniform Sum result
contract even when its selected alternative is Trivial. LLVM writes the
selected alternative's immutable descriptor plus exact carrier bits and never
allocates a tag tuple. Parser/printer, remapping, ownership, representation,
generic-fragment, and interface round-trip tests cover every alternative and
reject an unlisted type or a Sum marker used as the dynamic descriptor.

Before ownership lowering, `InjectSumInst` may also consume a Borrowed
alternative only when semantic type/provenance proves duplication admissible
(normally a statically `ALWAYS_SHAREABLE` managed type). Ownership lowering
splits before the injection, inserts `RetainInst` and therefore
`TryRetainRuntime`, and feeds only its Owned Success prefix to the injection;
false cleanup-traps with the original source/root live. A non-Shareable Borrow
is rejected instead of being silently moved or boxed. From `OwnershipLowered`
onward every Sum injection operand is Trivial or Owned. Tests cover a managed
Borrow parameter injected successfully, checked-retain failure, and static
rejection of a linear alternative.

Signed division and remainder are never emitted as unchecked LLVM
`sdiv`/`srem` on dynamic operands. Task 8 adds the terminator:

```cpp
enum class SignedDivRemKind : std::uint8_t { Divide = 0, Remainder = 1 };
struct CheckedSignedDivRem {
  SignedDivRemKind Kind;
  ValueId Left;
  ValueId Right;
  ProducedBranchTarget Success; // exactly one Trivial Int
  RuntimeFailureDisposition Failure; // arithmetic contract cleanup + trap
};
```

`runRepresentationSelection` splits every signed Int divide/remainder
`BinaryInst` into this form and remaps the former result to its Success block
argument. For a zero divisor LLVM creates a stack `YonaControlOutcome`, calls
`YonaRuntimeOutcomeInitEmpty`, then the nonallocating
`YonaRuntimeOutcomeInitAbiFailure(...,
YONA_ABI_FAILURE_ARITHMETIC_CONTRACT)`, requires that initializer to succeed,
calls `YonaRuntimeReportUnhandled`, releases the outcome, and enters the
terminator's edge-specific cleanup block before trapping. The diagnostic is
never an SSA value or source `Raised` outcome, and no live program owner is
released until the cleanup block. ABI and IR-shape tests prove the exact
init/report/release/cleanup/trap order. The one overflowing
two's-complement pair is defined explicitly: `INT64_MIN / -1 == INT64_MIN`
and `INT64_MIN % -1 == 0`; LLVM selects those constants without executing
`sdiv`/`srem`. Every other nonzero pair uses the matching signed instruction.
`RepresentationSelected` and later phases reject the raw two opcodes and
preserve this terminator through cleanup/LLVM with exhaustive successor,
operand, text/remap, ownership, and phase handling. O0-O3 tests cover zero,
the overflow pair, negative quotient/remainder truncation toward zero, and
neighboring values. Float `<`, `<=`, `>`, `>=`, and `==` use ordered
predicates; `!=` is the logical negation of ordered equality, with explicit
NaN/signed-zero tests.
Add `CheckedSignedDivRem` to `Terminator` and `SuccessorView` in this task;
Builder assigns distinct EdgeIds to Success/Failure and clone/remap preserves
them exactly. No later task supplies a missing variant arm.

Literal lowering is complete here: Byte/Char/Int/Float preserve their exact
carrier bits; Symbol lowers to a pointer to a process-lifetime static
`YonaAbiSymbolDescriptor`; and each evaluated String literal allocates an
Owned managed string with `YonaRuntimeAllocateStringWithLength` from the
escaped UTF-8 payload—never an LLVM-global pointer masquerading as managed storage.
`PatternTestInst` lowers Unit/Bool/Byte/Char/Int/Float/Symbol directly and
String by length/content equality. Symbol calls
`YonaRuntimeAbiSymbolEquivalent`; an optional hash may choose a collision
bucket but never decides equality. Task 11 adds the remaining aggregate/
collection test and projection cases before enabling them in the pipeline.

- [ ] **Step 4: Implement bounded LLVM contexts and finalization**

Public lowering is:

```cpp
struct LoweringOptions {
  int OptimizationLevel = 0;
  bool DebugInfo = false;
  std::string TargetTriple;
  std::string Cpu;
  std::string Features;
};

class LlvmModuleLowerer final {
public:
  LlvmModuleLowerer(llvm::LLVMContext &Context, LoweringOptions Options);
  std::expected<std::unique_ptr<llvm::Module>, LoweringError>
  lower(const typed_ir::Module &Input);
private:
  llvm::LLVMContext &Context_;
  LoweringOptions Options_;
};
```

This task also defines the ABI-shaped raw native operation that Task 11 later
normalizes; a CheckedDirect leaf is never an ordinary `DirectCallInst`:

```cpp
using NativeFunctionDescriptorId =
    StrongId<struct NativeFunctionDescriptorIdTag>;
struct NativeFunctionDescriptorPlan {
  NativeFunctionDescriptorId Id;
  FunctionId Declaration;
  model::TypeId FunctionType;
  std::vector<model::TypeId> ParameterTypes;
  std::vector<model::ParameterOwnership> ParameterOwnerships;
  model::TypeId ResultType;
  model::ResultOwnership ResultOwnership;
  model::EffectRowId Effects;
};
struct CheckedDirectNativeCallInst {
  NativeFunctionDescriptorId Descriptor;
  FunctionId Callee;
  std::vector<ValueId> Arguments;
};
```

`Module::NativeFunctionDescriptors` is a deterministic structural arena.
AST lowering emits this form only for a direct saturated declaration whose
authenticated route is CheckedDirectV2; identifier, partial, over-applied,
first-class, import/export, or ordinary DirectCall use is rejected.
StableExternal remains an ordinary exact typed `DirectCallInst`. A descriptor
may stay open only inside a canonical generic fragment and is remapped during
specialization; executable normalization requires it closed. The logical
NativeExtern Function remains declaration-only, but its CheckedDirect route
prevents LLVM from declaring the C symbol under the logical Yona signature.

Task 11 adds `CheckedDirectNativeCallInst` to ordinary
`InstructionPayload`/`CheckedRuntimePayload` and
`runRuntimeFailureNormalization` turns it into the common
`CheckedRuntimeOp`: Success produces exactly the declared result and observes
all Consume slots cleared; false follows `TrapCompilerFailure` with every
argument owner and the result unchanged, releasing a reserved diagnostic when
present. There is no postcommit failure state. ThreadPool universal adapters
contain this same checked form in their generated body, so worker execution
cannot bypass the ABI check. Parser/printer, clone/remap, phase, operand,
ownership, descriptor, zero/one/N arity, false/true Consume, and forged-
DirectCall tests are exhaustive.

The algorithm is fixed:

```text
verify Typed IR at LlvmReady
predeclare every LLVM function from its structural signature
map Definition/Imported/NativeExtern linkage and visibility before any body;
declare a CheckedDirect C symbol only with YonaAbiCheckedDirectNativeEntryV2
precreate every LLVM basic block
create PHIs for non-Unit block arguments
lower each function with a fresh LlvmFunctionLowerer
attach incoming PHI values while lowering branch edges
finalize debug/declarations once
verify LLVM
optimize at requested O-level
verify LLVM again
```

Map Unit to no carrier/void, Bool to `i1`, Int to `i64`, and Float to
`double`. `LlvmModuleLowerer` emits and owns the canonical static Symbol
descriptor pool used by literals and pattern tests; it never assigns dense
per-module symbol IDs. Lower only `CheckedSignedDivRem` for signed division/
remainder and use the exact Float comparison contract above.
No lowering API accepts an arbitrary `llvm::Value *` from another function.
Definitions lower their optional Entry/body; Imported functions become
declarations under their canonical mangled target, NativeExtern uses its exact
C symbol/calling convention. Private Definitions receive LLVM internal
linkage; Module and Public Definitions receive external linkage with hidden
and default visibility respectively. Imported Yona declarations receive
external linkage and the visibility recorded by their v2 declaration;
NativeExtern declarations receive external/default linkage unless their
explicit platform contract says otherwise. The object/export matrix checks
this mapping on ELF, Mach-O, and COFF, including Module-visible trait-support
symbols promoted before generic preparation.
The verifier has already rejected a declaration with blocks or a definition
without an Entry, so LLVM never guesses linkage from a name string.

`LlvmModuleLowerer` emits one private constant
`YonaAbiNativeFunctionDescriptor` per plan ID in canonical order and validates
the FunctionId/signature/route before any body. `LlvmBlockLowerer` materializes
the exact `YonaAbiNativeArgument` array and empty result/failure slots, calls
the manifest-authenticated symbol through only the generic prototype, branches
on its bool, and converts the successful `YonaAbiValue` to the stored result
representation. It never bitcasts a logical native signature or synthesizes a
failure as a Yona Result/Raised value.

- [ ] **Step 5: Implement the shared execution harness**

Expose one test helper:

```cpp
struct ExecutionExpectation {
  std::string Stdout;
  std::map<std::string, std::int64_t> ExpectedLiveAllocations;
};

typed_ir::Module lowerForTest(std::string_view Source);
struct LoweredLlvmModule {
  std::unique_ptr<llvm::LLVMContext> Context;
  std::unique_ptr<llvm::Module> Module;
};
std::expected<LoweredLlvmModule, LoweringError>
lowerToLlvm(const typed_ir::Module &Input, const LoweringOptions &Options);
std::string irText(const llvm::Module &Module);

void executeTypedIrForTest(std::string_view Source,
                           const ExecutionExpectation &Expectation,
                           std::array<int, 4> OptimizationLevels =
                               {0, 1, 2, 3});
```

It must parse, analyze, lower, run all passes, verify LLVM before/after
optimization, emit/link in a temporary directory, execute with
`YONA_ALLOC_STATS=1`, and compare stdout plus per-tag live allocations.

- [ ] **Step 6: Run scalar execution and full legacy gates**

```bash
python3 scripts/run-focused-tests.py ./out/build/x64-debug-linux/tests -tc='LLVM lowering*,Typed IR execution*'
ctest --preset unit-tests-linux --output-on-failure
git diff --check
```

Expected: scalar and Symbol fixtures execute correctly at O0-O3 and every legacy test
continues to use the frozen backend.

- [ ] **Step 7: Commit the test-only LLVM vertical slice**

```bash
git add include/yona/TypedIr/Pipeline.h src/TypedIr/Pipeline.cpp \
  include/yona/TypedIr/Passes/RepresentationSelection.h \
  src/TypedIr/Passes/RepresentationSelection.cpp \
  include/yona/TypedIr/Instruction.h include/yona/TypedIr/TypedIr.h \
  include/yona/TypedIr/Builder.h src/TypedIr/TypedIr.cpp \
  src/TypedIr/Builder.cpp src/TypedIr/Verifier.cpp src/TypedIr/Printer.cpp \
  src/TypedIr/Parser.cpp test/TypedIr/BuilderTest.cpp \
  test/TypedIr/VerifierTest.cpp test/TypedIr/PrinterParserTest.cpp \
  include/yona/Codegen/Llvm src/Codegen/Llvm \
  test/Support/TypedIrExecution.h test/Support/TypedIrExecution.cpp \
  test/Codegen/LlvmLoweringTest.cpp test/Codegen/TypedIrExecutionTest.cpp \
  test/TypedIr/RepresentationSelectionTest.cpp \
  test/Fixtures/TypedIr cmake/YonaComponents.cmake CMakeLists.txt
git add docs/superpowers/plans/2026-09-01-typed-ir-codegen-rewrite.md
git commit -m "feat: execute verified typed ir through llvm"
```

### Task 9: Unify closures, currying, and dynamic calls behind descriptors

**Files:**

- Create: `include/yona/TypedIr/Callable.h`
- Create: `include/yona/TypedIr/Analysis/FreeVariables.h`
- Create: `src/TypedIr/Analysis/FreeVariables.cpp`
- Create: `include/yona/TypedIr/Passes/ClosureConversion.h`
- Create: `src/TypedIr/Passes/ClosureConversion.cpp`
- Create: `include/yona/TypedIr/Verification/CallableVerifier.h`
- Create: `src/TypedIr/Verification/CallableVerifier.cpp`
- Create: `include/yona/Runtime/Core/Callable.h`
- Create: `src/Runtime/Core/Callable.c`
- Create: `include/yona/Runtime/Core/AbiArgument.h`
- Create: `src/Runtime/Core/AbiArgument.c`
- Create: `include/yona/Codegen/Llvm/CallableLowering.h`
- Create: `src/Codegen/Llvm/CallableLowering.cpp`
- Create: `test/TypedIr/CallableDescriptorTest.cpp`
- Create: `test/TypedIr/CallableVerifierTest.cpp`
- Create: `test/TypedIr/ClosureConversionTest.cpp`
- Create: `test/Runtime/CallableTest.cpp`
- Create: `test/Codegen/CallableLoweringTest.cpp`
- Modify: `include/yona/TypedIr/Instruction.h`
- Modify: `include/yona/TypedIr/TypedIr.h`
- Modify: `src/TypedIr/TypedIr.cpp`
- Modify: `src/TypedIr/Builder.cpp`
- Modify: `src/TypedIr/Verifier.cpp`
- Modify: `src/TypedIr/Printer.cpp`
- Modify: `src/TypedIr/Parser.cpp`
- Modify: `src/TypedIr/AstLowering.cpp`
- Modify: `src/TypedIr/Pipeline.cpp`
- Modify: `include/yona/Codegen/Llvm/ModuleLowerer.h`
- Modify: `src/Codegen/Llvm/ModuleLowerer.cpp`
- Modify: `src/Codegen/Llvm/FunctionLowerer.cpp`
- Modify: `src/Codegen/Llvm/BlockLowerer.cpp`
- Modify: `cmake/YonaComponents.cmake`
- Modify: `CMakeLists.txt`

**Interfaces:**

- Consumes: final-pipeline `ModulePhase::EffectPrepared` `LexicalRefInst` and
  `MakeFunctionInst` plus Task 7's ABI values/outcomes; the focused
  no-effect milestone may consume `ControlFlow` under its absence verifier.
- Produces: callable descriptor table, exhaustive IR-based free-variable
  analysis, closure conversion, runtime exact/under application, source
  over-application normalized to explicit typed call stages, and generated
  universal LLVM adapters.
- Invariant: closure-converted functions contain no `LexicalRefInst` and no
  outer function `ValueId`; dynamic calls contain no raw function pointer.

- [ ] **Step 1: Write descriptor and free-variable red tests**

Require deterministic capture order by `BindingId`. Construct a canonical
module directly with lexical references in nested control, resource, record,
field, and generator blocks so this analysis test does not depend on later
lowering tasks; Task 16 supplies source-level matrix coverage:

```cpp
TEST_CASE("Closure conversion captures every lexical reference exactly once") {
  const auto Module = makeLexicalReferenceModule(
      {{"try", "outer"}, {"with", "open"}, {"record", "outer"},
       {"field", "outer"}, {"generator", "outer"}});
  const auto Analysis = analyzeFreeVariables(Module, findFunction(Module, "f"));
  CHECK(bindingNames(Module, Analysis) == std::vector<std::string>{"open", "outer"});
}
```

Descriptor tests cover more than 64 parameter ownership entries; verifier
tests reject wrong capture type/order, adapter/signature disagreement, foreign
values, raw-native dynamic calls, and a remaining lexical reference. The
latent-wrapper regression is exact test `Typed IR callables: verifier rejects
a partial wrapper with a foreign outer ValueId` in
`CallableVerifierTest.cpp`.
Name the generated-adapter sentinel exactly `Callable lowering agrees for
captured native and universal entries`; the closure and runtime sentinels are
the exact cases shown in this task. Full generated-program execution is
deliberately deferred: Task 10 adds the captured-Float and recursive closure
sentinels after real ownership/cleanup lowering exists, and Task 11 adds the
aggregate-currying sentinel after aggregate construction exists.

- [ ] **Step 2: Write runtime currying and ABI-matrix red tests**

Use small test universal entries for Unit, Bool, Int, and Float plus managed
`RcProbe` carriers that do not depend on source-level aggregate lowering.
Cover nullary, exact, repeated under-application, dynamic arity, precommit
rejection of a C ABI over-application, wrong argument descriptors,
partial-allocation failure, capture destruction, and borrow/consume masks.
Add compiler tests showing source over-application—including a zero-arity
stage—is emitted as a sequence of exact calls with separately typed
intermediate results; Task 13 adds the corresponding Performed/Raised/Cancelled
continuation cases. Add exact synchronous use
of a non-Shareable Borrow argument, precommit rejection of that same argument
by the public C stage plus compile-time rejection of its static type under
partial application and async/effect escape, a reusable Shareable
callable whose apply owner is retained by ownership lowering, and a callable
with a linear capture whose sole owner moves into a partial. Also store a
cloneable String in a Consume capture/prefix and prove that the resulting
callable is still linear because invocation takes the slot. Verify exact,
under-, and rejected-overapplication owner counts independently:

```cpp
TEST_CASE("Runtime callable applies a runtime-selected arity incrementally") {
  auto Callable = makeIntCallable(/*arity=*/3, addThreeEntry);
  auto First = applySuccess(Callable, {abiInt(10)});
  REQUIRE(First.Type->Kind == YONA_ABI_CALLABLE);
  auto Second = applySuccess(asCallable(First), {abiInt(20)});
  REQUIRE(Second.Type->Kind == YONA_ABI_CALLABLE);
  CHECK(decodeInt(applySuccess(asCallable(Second), {abiInt(12)})) == 42);
}
```

- [ ] **Step 3: Run and observe missing descriptors/runtime application**

```bash
cmake --build --preset build-debug-linux --target tests -j2
python3 scripts/run-focused-tests.py ./out/build/x64-debug-linux/tests \
  -tc='Typed IR callables*,Closure conversion*,Runtime callable*,Callable lowering*'
```

Expected: compile failures for `CallableDescriptor` and `YonaCallableRef`.

- [ ] **Step 4: Define callable signatures and creation/application IR**

Add:

```cpp
struct CallableSignature {
  std::vector<model::TypeId> ParameterTypes;
  std::vector<model::ParameterOwnership> ParameterOwnerships;
  model::TypeId ResultType;
  model::ResultOwnership ResultContract;
  model::EffectRowId SemanticEffects;
  std::optional<RuntimeEffectRowId> RuntimeEffects;
  model::CallingConvention Convention;
};
struct EnvironmentField {
  std::uint32_t Index;
  semantics::BindingId Binding;
  model::TypeId Type;
  OwnershipKind CarrierOwnership;
  model::ParameterOwnership Access;
};
enum class CallableStorageKind : std::uint8_t {
  GeneratedEnvironment = 0, RecursiveMember = 1
};
struct RecursiveMemberIdentity {
  RecursiveClosureGroupDescriptorId Group;
  std::uint32_t MemberIndex;
};
struct CallableSuffixPlan {
  std::uint64_t AppliedPrefixCount;
  model::TypeId ValueType;
  std::optional<CallableSignature> RemainingCallable;
};
struct CallableDescriptor {
  CallableDescriptorId Id;
  CallableSignature Signature;
  BoundaryRequirementSetId AmbientBoundaryRequirements;
  CallableStorageKind StorageKind;
  std::optional<RecursiveMemberIdentity> RecursiveMember;
  FunctionId NativeFunction;
  FunctionId UniversalAdapter;
  std::vector<EnvironmentField> Environment;
  std::vector<CallableSuffixPlan> Suffixes;
};
struct RecursiveFunctionMember {
  semantics::BindingId Binding;
  FunctionId Function;
};
struct BindRecursiveFunctions {
  std::vector<RecursiveFunctionMember> Members; // BindingId order
  std::vector<LexicalBindingValue> AvailableBindings; // external only
  ProducedBranchTarget Body; // one Owned callable prefix per member
};
struct RecursiveClosureMember {
  std::uint32_t Index;
  semantics::BindingId Binding;
  FunctionId Function;
  CallableDescriptorId Callable;
};
struct RecursiveClosureGroupDescriptor {
  RecursiveClosureGroupDescriptorId Id;
  std::vector<EnvironmentField> Captures; // external union, BindingId order
  std::vector<RecursiveClosureMember> Members;
};
```

`RuntimeEffects` is empty when closure conversion first freezes descriptors
at `ClosureConverted`; Task 13's immediately following operation-
instantiation pass fills every slot before runtime descriptor emission. In
Task 9's isolated no-operation milestone, `SemanticEffects` must be the
canonical closed empty row and lowering may use Task 7's static empty ABI
descriptor. From `OperationInstantiated` onward, every callable signature
must carry the exact runtime row and the backend ignores `SemanticEffects`
except for verifier cross-checks.

Compiler `CallableDescriptor`s describe only generated roots and recursive
members, so `NativeFunction`, `UniversalAdapter`, and lexical Environment
fields are always meaningful. RuntimePartial is deliberately not a compiler
storage kind. Each root's complete `CallableSuffixPlan` table instead gives
codegen enough closed structural data to emit immutable synthetic
`YONA_CALLABLE_RUNTIME_PARTIAL` C descriptors whose entry is the fixed
`YonaRuntimePartialUniversalEntry` and whose root/prefix environment slots have
synthetic (non-lexical) origins. Operation instantiation fills the root
signature row once; every derived suffix reuses it. The callable verifier
validates these suffix plans, while runtime descriptor validation covers the
derived C graph.

Extend `Module` with `std::vector<CallableDescriptor> Callables` and expose
ID-based add/lookup methods; descriptor insertion never returns a reference
that later insertion can invalidate.

Keep Task 5's `MakeFunctionInst` and Task 3's `DirectCallInst` as the
pre-conversion/direct forms. Extend instruction payloads with:

```cpp
struct MakeClosureInst {
  CallableDescriptorId Descriptor;
  std::vector<ValueId> Captures;
};
struct CaptureBorrowInst { ValueId Environment; std::uint32_t FieldIndex; };
struct CaptureTakeInst { ValueId Environment; std::uint32_t FieldIndex; };
struct RecursiveMemberBorrowInst {
  ValueId Environment;
  RecursiveClosureGroupDescriptorId Group;
  std::uint32_t MemberIndex;
};
struct RetainInst { ValueId Source; };
struct ApplyCallableInst {
  ValueId Callable;
  std::vector<ValueId> Arguments;
  std::optional<ValueId> BoundaryContext;
};
struct CreateRecursiveClosureGroup {
  RecursiveClosureGroupDescriptorId Descriptor;
  std::vector<ValueId> Captures;
  ProducedBranchTarget Success; // one Owned callable prefix per member
  RuntimeFailureDisposition Failure;
};
```

AST lowering records the currently visible binding/value pairs in
`MakeFunctionInst`; it still does not decide which are free. Using resolved
binding references, it additionally computes maximal cyclic SCCs of local
function binders (a singleton is cyclic only with a self-edge) and emits one
`BindRecursiveFunctions` terminator per SCC. Member bindings never appear in
`AvailableBindings`; its produced prefix binds every member simultaneously in
deterministic BindingId order, avoiding illegal forward SSA. Ordinary acyclic
function values remain `MakeFunctionInst`.

`Module::RecursiveClosureGroups` is a checked ID arena. Add both group forms
to `Terminator`/`SuccessorView` and `RecursiveMemberBorrowInst` to
`InstructionPayload`; `RetainInst` is also declared here so closure conversion
can turn an escaping borrowed recursive member into an Owned value before its
`ClosureConverted` verifier runs. Task 10 generalizes and lowers the same op.
Parser/printer, operand, FunctionId/ValueId remap,
free-variable, phase, ownership, and cleanup visitors are exhaustive. Task 3's
optional `CallableEnvironmentParameter` is filled for every universal adapter,
every RecursiveMember direct entry regardless of external capture count, and
every ordinary generated closure entry whose descriptor has environment
storage. A continuation-frame direct entry follows that same storage rule; a
zero-capture frame does not acquire a fake callable environment. Task 13
instead predeclares the distinct `BoundaryContextParameter` on every
non-NativeExtern Yona Definition/Imported direct entry and every OutcomeRouter,
except the fixed-ABI KeyHashAdapter/KeyEqualsAdapter roots; closure conversion
preserves that slot while every universal adapter accepts
the corresponding explicit ABI argument. Ordinary roots receive the
invocation view with a null recursive-group member and a separate null
boundary-context argument.
`DirectCallInst::CallableEnvironment` remains empty outside verified recursive
intra-SCC calls. Both use the non-source-nameable Borrow-only
`AbiOpaqueKind::CallableInvocationEnvironment` and lower to unmanaged pointers,
never ordinary `YonaAbiValue` carriers or descriptor children.

- [ ] **Step 5: Implement IR-based closure conversion**

Expose:

```cpp
struct FreeVariable {
  semantics::BindingId Binding;
  model::TypeId Type;
  OwnershipKind Ownership;
};
using FreeVariableSet = std::vector<FreeVariable>;

FreeVariableSet analyzeFreeVariables(const Module &, FunctionId);
PassResult runClosureConversion(Module &);
VerificationResult verifyCallables(const Module &);
```

`analyzeFreeVariables` is phase-neutral from Canonical through
`EffectPrepared`:
it scans the selected function plus every function-owned match, control,
generator, and cleanup arena record through exhaustive visitors, but never
walks into a different function body. Scan all instructions/terminators
generically through the IR operand visitor, not an AST switch. Sort unique free
bindings by `BindingId`; generate an
explicit environment parameter and `CaptureBorrowInst`/`CaptureTakeInst`s; replace
`MakeFunctionInst` with `MakeClosureInst`; generate one universal adapter
function per descriptor, including its hidden dominating
`ExecutionContextParameter` even before Task 12 classifies non-adapter
functions; then reject any residual free lexical reference. All
new descriptor signatures and adapter functions carry their exact closed
`SemanticEffects` and an empty `RuntimeEffects` slot. The final pipeline calls
this pass on `EffectPrepared` and produces `ClosureConverted`, so every
continuation, handler, handler-entry/dispatch, and prepared resume function
created by Task 13 goes
through the same conversion before operation instantiation freezes the
function set and fills runtime rows. During this earlier milestone it may
accept `ControlFlow` only when the effect verifier proves that `Perform`,
`Resume`, and handler operations are absent.

For `BindRecursiveFunctions`, closure conversion analyzes the whole maximal
SCC transactionally. It removes member bindings from every member free set,
unions only external free bindings in BindingId order, and gives every member
the same group environment. Trivial captures use Trivial access; every managed
capture must have statically proven Shareable storage and uses Borrow access.
A Consume access or linear/unknown-shareability external capture is the ranged
`RecursiveCaptureNotShareable` error. The pass rewrites outer references to
`CaptureBorrowInst`, first-class self/sibling references to
`RecursiveMemberBorrowInst`, and direct intra-SCC calls to the member FunctionId
with the exact current `CallableEnvironment`. Such calls borrow the environment
and allocate/retain nothing. An owned first-class escape is immediately
materialized as the Task 9-declared `RetainInst` over the borrowed member;
because recursive groups admit only Shareable captures, this retain is
statically valid and Task 10 later lowers it with every other ownership op.
`CaptureTakeInst` is forbidden
in a recursive member and the environment-view pointer cannot escape or itself
be captured. Finally the pass creates all member callable descriptors plus one
`RecursiveClosureGroupDescriptor`, replaces the pre-conversion group with one
`CreateRecursiveClosureGroup`, and publishes the module only if every member
and use verifies.

Every environment field has an explicit access contract. `CaptureBorrowInst`
leaves the slot intact and produces only a Borrow tied to the environment
owner. `CaptureTakeInst` atomically moves and clears one Consume slot and is
legal only in a descriptor proven non-Shareable and an adapter invocation that
owns the unique callable instance. The callable verifier proves each Consume
field is taken at most once and every path releases or takes it; a reusable
descriptor contains no Consume field. `ApplyMove` clears the caller's callable
slot before invoking such an adapter, checks the instance reference count is
one, and passes an invocation view over its mutable environment array with a
null recursive-group pointer. Shareable instances may have
multiple references, but their adapters can only borrow fields. This is how a
one-shot resume or closure captures and later consumes a linear resource; no
adapter const-casts an immutable environment. Tests cover a closure that
returns/consumes a captured linear resource, reuse of an ordinary capture, and
a one-shot resume whose continuation field is empty after its first call.
Recursive tests are exact cases `Closure conversion forms one capture-sharing
group for self recursion`, `Closure conversion forms one capture-sharing group
for mutual recursion`, `Typed IR callables: verifier rejects a linear
recursive-group capture`, `Typed IR callables: verifier rejects a member-owned
group cycle`, and `Callable lowering passes one borrowed environment through
direct mutual calls`.

- [ ] **Step 6: Implement the final runtime callable ABI**

`Callable.h` defines:

```c
typedef struct YonaCallable *YonaCallableRef;
typedef struct YonaRecursiveCallableGroup YonaRecursiveCallableGroup;
typedef struct YonaContinuationBoundaryContext
    YonaContinuationBoundaryContext;
typedef struct YonaCallableInvocationEnvironment {
  YonaAbiValue *Values;
  uint64_t ValueCount;
  const YonaRecursiveCallableGroup *BorrowedRecursiveGroup;
} YonaCallableInvocationEnvironment;
typedef void (*YonaUniversalEntry)(
    const YonaCallableInvocationEnvironment *BorrowedEnvironment,
    YonaAbiValue *Arguments,
    uint64_t ArgumentCount,
    const YonaContinuationBoundaryContext *BorrowedBoundaryContext,
    const YonaExecutionContext *BorrowedContext,
    YonaControlOutcome *Outcome);
void YonaRuntimePartialUniversalEntry(
    const YonaCallableInvocationEnvironment *BorrowedEnvironment,
    YonaAbiValue *Arguments,
    uint64_t ArgumentCount,
    const YonaContinuationBoundaryContext *BorrowedBoundaryContext,
    const YonaExecutionContext *BorrowedContext,
    YonaControlOutcome *Outcome);

typedef uint32_t YonaAbiCallingConvention;
enum {
  YONA_ABI_CALL_DIRECT_YONA = 0u,
  YONA_ABI_CALL_CLOSURE_ENTRY = 1u,
  YONA_ABI_CALL_CONTINUATION = 2u,
  YONA_ABI_CALL_EFFECT_OPERATION = 3u,
  YONA_ABI_CALL_ASYNC_ADAPTER = 4u,
  YONA_ABI_CALL_NATIVE_EXTERN = 5u,
  YONA_ABI_CALL_EXPORTED_C = 6u
};
typedef uint32_t YonaCallableStorageKind;
enum {
  YONA_CALLABLE_GENERATED_ENVIRONMENT = 0u,
  YONA_CALLABLE_RUNTIME_PARTIAL = 1u,
  YONA_CALLABLE_RECURSIVE_MEMBER = 2u
};

typedef struct YonaCallableDescriptor YonaCallableDescriptor;
typedef struct YonaRecursiveCallableGroupDescriptor
    YonaRecursiveCallableGroupDescriptor;
typedef struct YonaCallableSuffixDescriptor {
  uint32_t AbiVersion;
  uint32_t Reserved;
  uint64_t AppliedPrefixCount;
  const YonaAbiTypeDescriptor *ValueType;
  const YonaCallableDescriptor *PartialCallable;
} YonaCallableSuffixDescriptor;

struct YonaCallableDescriptor {
  uint32_t AbiVersion;
  YonaAbiCallingConvention Convention;
  YonaCallableStorageKind StorageKind;
  uint32_t ReservedStorage;
  uint64_t Arity;
  uint64_t AppliedPrefixBase;
  const YonaAbiTypeDescriptor *const *ParameterTypes;
  const YonaAbiParameterOwnership *ParameterOwnerships;
  const YonaAbiTypeDescriptor *ResultType;
  YonaAbiResultOwnership ResultOwnership;
  uint32_t Reserved;
  const YonaAbiEffectRowDescriptor *EffectRow;
  const YonaAbiTypeDescriptor *const *EnvironmentTypes;
  const YonaAbiParameterOwnership *EnvironmentOwnerships;
  uint64_t EnvironmentCount;
  const YonaCallableSuffixDescriptor *Suffixes;
  uint64_t SuffixCount;
  const YonaRecursiveCallableGroupDescriptor *RecursiveGroup;
  uint64_t RecursiveMemberIndex;
  YonaUniversalEntry UniversalEntry;
};

typedef struct YonaRecursiveCallableMemberDescriptor {
  uint32_t AbiVersion;
  uint32_t Reserved;
  uint64_t MemberIndex;
  const YonaCallableDescriptor *Callable;
} YonaRecursiveCallableMemberDescriptor;
struct YonaRecursiveCallableGroupDescriptor {
  uint32_t AbiVersion;
  uint32_t Reserved;
  uint64_t CaptureCount;
  const YonaAbiTypeDescriptor *const *CaptureTypes;
  const YonaAbiParameterOwnership *CaptureAccesses;
  uint64_t MemberCount;
  const YonaRecursiveCallableMemberDescriptor *Members;
};

typedef struct YonaAbiArgument {
  YonaAbiParameterOwnership Ownership;
  uint32_t Reserved;
  union {
    const YonaAbiValue *Borrowed;
    YonaAbiValue *Owned;
  } Slot;
} YonaAbiArgument;
typedef struct YonaAbiArgumentStage *YonaAbiArgumentStageRef;

bool YonaRuntimeCallableCreateMove(const YonaCallableDescriptor *Descriptor,
                                   YonaAbiValue *OwnedCaptures,
                                   uint64_t CaptureCount,
                                   YonaCallableRef *EmptyOutput,
                                   YonaControlOutcome *EmptyFailure);
bool YonaRuntimeRecursiveCallableGroupCreateMove(
    const YonaRecursiveCallableGroupDescriptor *Descriptor,
    YonaAbiValue *OwnedCaptures,
    uint64_t CaptureCount,
    YonaCallableRef *EmptyMemberOutputs,
    uint64_t MemberOutputCount,
    YonaControlOutcome *EmptyFailure);
YonaCallableRef YonaRuntimeRecursiveCallableMemberBorrow(
    const YonaCallableInvocationEnvironment *BorrowedEnvironment,
    uint64_t MemberIndex);
bool YonaRuntimeCallableTryRetain(YonaCallableRef Callable);
void YonaRuntimeCallableRelease(YonaCallableRef Callable);
uint64_t YonaRuntimeCallableRemainingArity(YonaCallableRef Callable);
bool YonaRuntimeCallableApplyMove(YonaCallableRef *OwnedCallable,
                                  YonaAbiArgument *Arguments,
                                  uint64_t ArgumentCount,
                                  const YonaContinuationBoundaryContext
                                      *BorrowedBoundaryContext,
                                  const YonaExecutionContext *BorrowedContext,
                                  YonaControlOutcome *Outcome);
bool YonaRuntimeAbiArgumentStageCreate(
    const YonaAbiTypeDescriptor *const *ParameterTypes,
    const YonaAbiParameterOwnership *Ownerships,
    uint64_t ParameterCount,
    YonaAbiArgument *Inputs,
    YonaAbiArgumentStageRef *EmptyStage,
    YonaControlOutcome *EmptyFailure);
void YonaRuntimeAbiArgumentStageCommitMove(YonaAbiArgumentStageRef Stage);
void YonaRuntimeAbiArgumentStageRelease(YonaAbiArgumentStageRef Stage);
YonaAbiValue *
YonaRuntimeAbiArgumentStageValues(YonaAbiArgumentStageRef CommittedStage);
uint64_t
YonaRuntimeAbiArgumentStageCount(YonaAbiArgumentStageRef Stage);
```

`YonaAbiArgument` and the generic escape-preparation API live in
`AbiArgument.h`; callable, effect-request, and async submission code all use
this one implementation. Add exact 64-bit size/offset assertions for
`YonaCallableInvocationEnvironment`, `YonaCallableDescriptor`, both recursive
group descriptor records, and `YonaAbiArgument`; require
`AbiVersion == YONA_RUNTIME_ABI_VERSION`, known calling convention,
`Reserved == 0`, and descriptor-vector lengths implied by Arity/environment
count before allocation.
The seven public convention constants are deliberately numeric-identical to
`model::CallingConvention`; descriptor emission uses an exhaustive switch with
no default and static assertions for every value. DirectYona, NativeExtern,
and ExportedC values are legal only when their generated universal adapter is
the callable entry; the runtime never calls the native/direct symbol through a
cast. Tests emit and validate all seven conventions and reject 7/reserved
values.
`SuffixCount` is exactly `Arity + 1`; a source/root descriptor has
`AppliedPrefixBase == 0`. Local entry `j` describes the value after
`AppliedPrefixBase + j` original arguments: its version/reserved fields are
valid, `AppliedPrefixCount == AppliedPrefixBase + j`, and `ValueType` is the
exact canonical remaining FunctionType for `j < Arity` or the final result
type for `j == Arity`. `PartialCallable` is non-null exactly for `j < Arity`,
has arity `Arity - j`, `AppliedPrefixBase` equal to that entry's absolute
prefix, parameter type/ownership pointers equal to the corresponding suffix,
the same final result/effect/convention, and a table view beginning at that
entry; local entry zero points back to the enclosing descriptor.
The final entry has no partial descriptor even when the result is callable:
source over-application is already a later explicit call stage whose static
callee type is that final `ValueType` and whose runtime value is validated
against its own descriptor. The public Apply ABI never follows a returned
callable or accepts arguments beyond the current remaining arity. All
tables and descriptors have immutable static module lifetime; the runtime
never synthesizes a FunctionType or callable descriptor.
Every nonzero-prefix `PartialCallable` has
`StorageKind == YONA_CALLABLE_RUNTIME_PARTIAL` and
`UniversalEntry == YonaRuntimePartialUniversalEntry`, plus a flattened environment of the root
callable owner followed by original parameters `0..AppliedPrefixBase-1`.
`EnvironmentTypes` are that exact root FunctionType then those parameter
types; every environment access is Consume because the trampoline receives an
owned invocation copy/move, and `EnvironmentCount == 1 + AppliedPrefixBase`.
The runtime partial object stores those fields once. When the instance is
Shareable, Apply clones/retains them into a temporary invocation environment;
when linear and uniquely owned, Apply moves/clears them. The trampoline appends
the new argument-stage values and calls ApplyMove on the owned root callable.
Underapplying an existing partial flattens it transactionally by cloning or
moving its existing root/prefix fields and appending the new prefix; it never
nests a partial object. A stored Consume parameter makes the instance linear,
while all-Trivial/Shareable stored fields permit reuse despite the descriptor's
Consume invocation view. Generated-environment descriptors keep the ordinary
rule that any Consume capture is linear. Descriptor validation recursively
checks this exact storage-kind/layout/trampoline relationship. Generated and
RuntimePartial descriptors require `RecursiveGroup == NULL` and
`RecursiveMemberIndex == 0`; a RecursiveMember requires a non-null group and
an in-range index whose member record points back to that exact callable.
Validation is an iterative tri-color walk over the callable/suffix/group/member
namespace, memoized by descriptor pointer plus expected root/applied-prefix/
group context. Type and effect descriptor edges are leaves of this walk and
are delegated to Task 7's independent cycle-safe structural validator and
equivalence rules; legal runtime-row/operation SCCs therefore never become
callable gray edges. Within the callable namespace, the only accepted back-
edges are suffix entry zero to its exact enclosing descriptor and a recursive
member's exact group/member round trip. Any other gray edge, context mismatch,
or same pointer under incompatible expectations is rejected. Successfully
black callable nodes are memoized for the whole public API call, so malicious
callable cycles terminate and shared descriptor DAGs are linear-time. Forced
self/group-valid, legal cyclic-row, and foreign-callable-cycle tests cover the
separate paths.
Every universal invocation requires a valid non-null execution context and
forwards it unchanged through nested universal/direct outcome-capable calls;
ordinary synchronous roots use `YonaRuntimeNeverCancelledContext()`.
The explicit boundary-context argument to `YonaRuntimeCallableApplyMove` is
null only for a true ordinary root. Every in-module nested callable
application forwards the caller's exact hidden parameter. Task 13's
continuation-resume loop supplies a non-null Chain view of the remaining
chain, while its immediate state-loan frame invocation supplies a Direct view.
Apply passes that pointer
as the distinct `BorrowedBoundaryContext` universal-entry argument; every
adapter/partial trampoline forwards the same explicit argument to its nested
root apply independently of environment storage. Invocation-environment
layout therefore remains exactly values/count/optional recursive group. No
callable stores, retains, or exposes a boundary context, and descriptor
validation permits a frame to query it only through Task 13's closed
boundary-projection records. ABI tests assert the exact invocation-environment
size/offsets and universal/partial argument order on every target.

A recursive group is one allocation containing an atomic group owner count,
the shared owned outer captures, and embedded member shells. A shell stores
only descriptor/group/index and has no independent owner count; the group has
no owning edge to its shells and captures never contain a member binding, so
no RC cycle exists. Group creation starts with exactly one lease for each
published member output. Retaining any member increments the group count;
releasing it decrements the count, and the last release drops every shared
capture once and frees the block. Apply transfers the consumed member owner
into a local invocation lease, clears the caller slot, and passes a
`YonaCallableInvocationEnvironment` whose Values are the group's shared
captures and whose `BorrowedRecursiveGroup` is that group; it releases the
lease after the adapter returns. Direct intra-SCC calls pass the same borrowed
view. `YonaRuntimeRecursiveCallableMemberBorrow` is allocation-free and legal
only for the statically verified current group/member index. Underapplication
moves the member owner into the flattened RuntimePartial root field, so the
partial keeps the group alive with no special back-edge.

The group descriptor has a nonempty contiguous member list and exact shared
capture vector. Every member callable has
`StorageKind == YONA_CALLABLE_RECURSIVE_MEMBER`, root prefix zero, matching
group/index, and exactly the group's no-Consume environment types/accesses;
its convention is exactly `ClosureEntry`/
`YONA_ABI_CALL_CLOSURE_ENTRY`. Continuation, effect-operation, async-adapter,
native-extern, and exported-C conventions cannot be recursive group members
because their retain/control contracts differ. Suffix partial descriptors
remain RuntimePartial. All member Bindings,
Functions, descriptors, and indices are unique and deterministic. Ordinary
`CreateMove` rejects RecursiveMember and externally supplied RuntimePartial
descriptors; group creation is the only recursive-member constructor and
underapplication is the only runtime-partial constructor.

Group creation validates every descriptor, actual nontrivial capture's
`YonaRuntimeAbiValueIsShareable`, count, initially null member output, and
requires the complete capture-array range, member-output-array range, and
failure slot to be pairwise byte-disjoint, then allocates and initializes
the whole block before commit. Structural storage rejection writes nothing.
Descriptor/shareability/OOM failure leaves captures unchanged, every output
null, and writes the reserved diagnostic. One infallible success commit moves/
clears all captures, publishes every member, and leaves failure Empty.
`CreateRecursiveClosureGroup::Failure` is always `TrapCompilerFailure`.

The callable verifier proves the source graph is genuinely strongly connected
(including a self-edge for a singleton), the external capture union is exact,
no member is a capture, all member/group/descriptor IDs and layouts agree, and
the success prefix has one Owned exact FunctionType per member in index order.
It rejects a recursive borrow outside the current group/environment, a direct
member call without the exact environment, a recursive environment take, a
borrowed member used by an owning/Consume/escape sink instead of the result of
an explicit `RetainInst`, and residual
`BindRecursiveFunctions`/lexical references at `ClosureConverted`. Use stable
diagnostics `RecursiveGroupNotStronglyConnected`,
`RecursiveCaptureNotShareable`, `RecursiveMemberEnvironmentMismatch`, and
`RecursiveMemberEscapeWithoutRetain`. The original
`RecursiveMemberBorrowInst` result may feed only Borrow sinks or the Source of
`RetainInst` within that invocation; every owning, Consume, returned, captured,
or otherwise escaping operand must be the retain result. Ordinary ownership
verification then requires exactly one sink for each retained owner, so an
unrelated dominating retain cannot legalize escape of the borrow.

Runtime/codegen coverage includes exact cases `Runtime callable recursive group
creation is transactional on allocation failure`, `Runtime callable first-class
self escape retains its group`, `Runtime callable partial recursive member keeps
its group alive`, `Runtime callable recursive group releases shared captures
exactly once`, and `Typed IR execution: mutually recursive closures share an
outer capture`. Add zero-external-capture self recursion and mutual recursion;
each member still has its hidden invocation-environment parameter and borrows
itself/its peer through the group view. The self/mutual hot path must show one group allocation and zero
per-hop callable allocations or retains; forced descriptor/shareability/OOM
failure leaves all captures and member outputs unchanged. Compiler and runtime
descriptor tests reject a recursive member with every non-ClosureEntry
convention.

`CreateMove` validates the complete capture descriptor vector, allocates an
immutable descriptor-driven `YonaAbiValue[]` environment, and clears inputs
only after the allocation and validation succeed. On failure it returns
`false`, leaves all inputs unchanged, keeps `*EmptyOutput` null, and writes the
exact nonallocating descriptor-mismatch or OOM outcome to `EmptyFailure`.
It also computes and freezes one instance `Shareable` bit: true only when
an ordinary generated environment has no Consume field and every owned Borrow
field can be cloned. A RuntimePartial instead recomputes the bit over its
stored root callable and flattened prefix: every field must be Trivial or
Shareable, and any source Consume parameter forces false even if its carrier
type is otherwise cloneable. RecursiveMember instances are Shareable because
Task 9 admits only Trivial/Shareable group captures. `TryRetain` increments
the callable reference count only when this bit is true and otherwise returns
false without mutation; it is the callback used by the callable ABI value's
structural descriptor. Ordinary generated code that needs two owners must call
the same fallible primitive before either branch commits.
`EmptyOutput` must be non-null and initially null; `EmptyFailure` must be
non-null and Empty. The whole capture-array range is byte-disjoint from both
output slots; its fixed-size elements are distinct owner slots by construction.
Different elements may refer to the same heap object only when that instance
is Shareable and each slot owns a valid reference. Structural storage rejection
leaves all storage unchanged, including `EmptyFailure`. Null arrays are valid
only with a zero count. Success clears every capture, publishes one callable, and
leaves `EmptyFailure` Empty.
All argument/capture/result validation uses Task 7's full cycle-safe
descriptor/effect-row equivalence after fingerprint prefiltering; pointer or
fingerprint equality alone is never a semantic match.

The descriptor is authoritative for every argument: each record's ownership
tag must match its parameter and `Reserved` must be zero. Trivial and Borrow
records point to immutable values and are never cleared. An exact synchronous
stage copies Trivial words and borrows a Borrow value only until the generated
adapter returns. A Consume record points to one distinct mutable owner slot;
that slot moves and clears only when the stage commits. No exact-call adapter
retains or releases a Borrow input.

`YonaRuntimeCallableApplyMove` consumes its callable owner when the first
stage commits. Ownership lowering retains a Shareable callable first when the
source remains live, and transfers the sole owner for a callable with a
non-Shareable capture. Under-application therefore creates an immutable
partial that owns the moved base callable, copies Trivial values, clones Borrow
values only when their descriptors are Shareable, and moves Consume values.
A partial produced after `k` arguments takes its Success carrier type and
exact callable descriptor from local suffix entry `k` (absolute prefix
`AppliedPrefixBase + k`); repeated partial application indexes the selected
descriptor's rebased table, so no applied-prefix state is reconstructed from
argument counts alone.
A supplied non-Shareable Borrow fails before any mutation in the public C
underapplication path. Generated partial/effect/async escape is legal only when
the Borrow parameter's structural type is statically `ALWAYS_SHAREABLE`; the
preparation verifier rejects every instance-sensitive type before emitting a
stage, because runtime instance state cannot decide source-visible correctness.
The public Apply
ABI accepts at most the current remaining arity; a larger `ArgumentCount` is a
precommit structural failure with callable, arguments, and Empty outcome
unchanged. Canonical AST lowering instead splits source over-application into
one explicit exact `DirectCallInst`/`ApplyCallableInst` per statically known
FunctionType layer. Each intermediate result is an ordinary typed SSA value,
so its own Success/Raised/Performed/Cancelled routing and the still-unconsumed
later arguments remain visible to effect, ownership, and cleanup passes.
Compiler-side descriptors retain
`NativeFunction` as a
typed `FunctionId`, but the public C descriptor has no native-entry object
pointer. Generated adapters directly call their predeclared typed LLVM
function and alone encode/decode parameters/results.

For every structurally valid call, apply returns `true` and has one
deterministic callee-owns post-state: on every return it has
cleared `*OwnedCallable` and every Consume slot, either by moving them into a
partial/direct stage or by releasing them on null, validation, descriptor,
non-Shareable-Borrow, allocation, or control
failure. Trivial and Borrow inputs are never mutated. All validation and
fallible Borrow clones precede movement; if that staging fails, apply releases
the callable and Consume owners instead of partially committing them. Every failure produces a
typed C diagnostic outcome. The builtin `AbiFailure` descriptor is reserved,
not source-nameable, and cannot appear in a semantic exception row. Generated
apply lowering checks for it before ordinary outcome routing, releases the
diagnostic, runs caller cleanup, and traps; a callee-produced declared Raised
outcome remains ordinary source control. `Outcome` must be non-null and Empty; apply never destroys
arbitrary uninitialized storage. A null callable value produces the
nonallocating null-callable failure while still clearing/releasing every
Consume argument.
Before reading a carrier or mutating an owner, apply validates its structural
storage contract. The callable-owner slot, outcome, argument-record array, and
every mutable Consume slot are pairwise byte-range distinct; each Consume slot
is also disjoint from every Trivial/Borrow source value. Immutable
Trivial/Borrow sources may alias one another, and distinct owner slots may
refer to the same Shareable heap object. Null required storage, non-Empty
outcome, invalid execution-context/version/reserved fields, invalid record
storage, or overlap returns `false` without calling the context probe, reading
a carrier, writing the outcome, or mutating any owner. Generated lowering proves
this contract statically and treats false as an internal ABI violation,
releasing the still-owned callable/Consume values through its cleanup edge;
runtime API tests exercise the unchanged-on-false boundary explicitly.

The argument-stage API is the sole partial/async/effect escape algorithm. Stage
creation validates the complete descriptor and input vectors, copies Trivial
values, clones Borrow only when Shareable, allocates all storage, and records
the Consume slots without moving them. On false it releases all staging,
leaves every input unchanged, keeps `*EmptyStage` null, and writes the exact
nonallocating typed failure to the initially Empty failure outcome. The caller
may now stage its enclosing partial/request/task object and queue insertion;
failure before publication releases the uncommitted stage and still leaves
Consume inputs untouched. Immediately before the enclosing object's single
publication point, `ArgumentStageCommitMove` infallibly moves/clears every
recorded Consume slot into the preallocated stage. A committed stage owns one
carrier per parameter and is the storage retained by the enclosing object;
release drops every carrier still present. Values are exposed only on a
committed stage.

Null arrays are legal only for zero count. Every mutable Consume slot,
callable-owner slot, outcome slot, stage-output slot, and enclosing handle
output must occupy a distinct, nonoverlapping byte range; distinct owners of
the same Shareable object remain legal. `YonaRuntimeCallableApplyMove` uses a
stage for underapplication but preserves its always-consume contract by
releasing callable/Consume owners if staging fails. Effect/task creation uses
the same stage but can preserve its bool-returning all-or-nothing contract by
discarding it before commit. A non-Shareable Borrow is therefore valid for
exact synchronous apply. Generated partial, async, and effect escape accept
only statically `ALWAYS_SHAREABLE` Borrow types; public C callers with another
type receive deterministic precommit rejection before mutation.

Adapter result handling follows the separate result contract exactly:
Trivial copies its word and Owned moves its carrier into Success. A forged or
decoded third result-ownership value is rejected before invocation as
`YONA_ABI_FAILURE_DESCRIPTOR_MISMATCH`; no adapter needs to infer the lifetime
of a borrowed result. All validation/null/OOM/non-callable
failures use Task 7's reserved nonallocating ABI-failure diagnostic; there is
no silent null or status-only C path, and generated code never forwards that
diagnostic as a language Raised outcome. Add pure-function forced-OOM partial,
null callable, descriptor mismatch, non-Shareable partial Borrow, and
overapplication-rejection tests that prove unchanged precommit owners followed by
the invariant trap, alongside a declared callee Raised case that still
propagates normally. Add repeated one-argument partials over an arity-three
callable and reject a missing, short, corrupt-version, wrong-prefix,
wrong-remaining-type, or wrong-final-result suffix table before any owner
moves.

- [ ] **Step 7: Lower descriptors and universal adapters to LLVM**

Emit direct entries with structural native signatures. Emit universal
adapters with the exact C signature above; load/decode each carrier by its
descriptor, call the direct entry, and store a complete outcome. The mutable
carrier array is owned by the enclosing argument stage: Trivial and Borrow
arguments are only read for the duration of the call, while Consume moves and
clears its carrier slot before entering the direct callee. `ApplyCallableInst`
is a consuming sink for its callable operand; ownership lowering inserts a
retain only for a still-live Shareable callable and rejects reuse of a
non-Shareable callable after transfer.

`LlvmBlockLowerer` owns the exhaustive dispatch for `MakeClosureInst`,
`CaptureBorrowInst`, `CaptureTakeInst`, `RecursiveMemberBorrowInst`,
`CreateRecursiveClosureGroup`, and `ApplyCallableInst` and delegates their descriptor/
adapter details to `CallableLowering`; `LlvmFunctionLowerer` installs the
invocation-environment parameter, passes it unchanged on direct intra-group
calls, and generates adapter blocks. No callable instruction
survives by relying on `LlvmModuleLowerer` alone. The focused lowering test
asserts each opcode reaches its assigned dispatch and leaves no placeholder
call or unlowered metadata.

Run descriptor/runtime/adapter unit tests without bypassing the not-yet-built
ownership suffix:

```bash
python3 scripts/run-focused-tests.py ./out/build/x64-debug-linux/tests \
  -tc='Typed IR callables*,Closure conversion*,Runtime callable*,Callable lowering*'
git diff --check
```

Expected: generated adapter IR verifies for Unit/Bool/Int/Float; direct runtime
`RcProbe` cases prove managed capture/argument cleanup; dynamic
under-application succeeds, ABI over-application rejects unchanged, and source
over-application is an explicit typed call chain without
function-pointer/object-pointer bitcasts. No Task 9 test advances an Owned callable module to `LlvmReady` by
phase relabeling.

- [ ] **Step 8: Commit the callable boundary**

```bash
git add include/yona/TypedIr src/TypedIr include/yona/Runtime/Core/Callable.h \
  src/Runtime/Core/Callable.c include/yona/Runtime/Core/AbiArgument.h \
  src/Runtime/Core/AbiArgument.c include/yona/Codegen/Llvm/CallableLowering.h \
  src/Codegen/Llvm/CallableLowering.cpp include/yona/Codegen/Llvm/ModuleLowerer.h \
  src/Codegen/Llvm/ModuleLowerer.cpp \
  src/Codegen/Llvm/FunctionLowerer.cpp src/Codegen/Llvm/BlockLowerer.cpp \
  test/TypedIr test/Runtime/CallableTest.cpp \
  test/Codegen/CallableLoweringTest.cpp cmake/YonaComponents.cmake CMakeLists.txt
git add docs/superpowers/plans/2026-09-01-typed-ir-codegen-rewrite.md
git commit -m "feat: unify first class callable abi"
```

### Task 10: Make ownership, cleanup, and escape whole-CFG proofs

**Files:**

- Create: `include/yona/TypedIr/Ownership.h`
- Create: `include/yona/TypedIr/Cleanup.h`
- Create: `include/yona/Runtime/Core/OwnedSlotState.h`
- Create: `src/Runtime/Core/OwnedSlotState.c`
- Create: `include/yona/TypedIr/Analysis/OwnershipAnalysis.h`
- Create: `src/TypedIr/Analysis/OwnershipAnalysis.cpp`
- Create: `include/yona/TypedIr/Analysis/EscapeAnalysis.h`
- Create: `src/TypedIr/Analysis/EscapeAnalysis.cpp`
- Create: `include/yona/TypedIr/Passes/OwnershipLowering.h`
- Create: `src/TypedIr/Passes/OwnershipLowering.cpp`
- Create: `include/yona/TypedIr/Passes/AsyncPreparation.h`
- Create: `src/TypedIr/Passes/AsyncPreparation.cpp`
- Create: `include/yona/TypedIr/Passes/CleanupPreparation.h`
- Create: `src/TypedIr/Passes/CleanupPreparation.cpp`
- Create: `include/yona/TypedIr/Passes/CleanupLowering.h`
- Create: `src/TypedIr/Passes/CleanupLowering.cpp`
- Create: `include/yona/TypedIr/Passes/ArenaPlacement.h`
- Create: `src/TypedIr/Passes/ArenaPlacement.cpp`
- Create: `include/yona/TypedIr/Verification/OwnershipVerifier.h`
- Create: `src/TypedIr/Verification/OwnershipVerifier.cpp`
- Create: `include/yona/TypedIr/Verification/CleanupVerifier.h`
- Create: `src/TypedIr/Verification/CleanupVerifier.cpp`
- Create: `include/yona/TypedIr/Verification/EscapeVerifier.h`
- Create: `src/TypedIr/Verification/EscapeVerifier.cpp`
- Create: `test/TypedIr/OwnershipAnalysisTest.cpp`
- Create: `test/TypedIr/OwnershipLoweringTest.cpp`
- Create: `test/TypedIr/OwnershipVerifierTest.cpp`
- Create: `test/TypedIr/CleanupLoweringTest.cpp`
- Create: `test/TypedIr/NoAsyncPreparationTest.cpp`
- Create: `test/TypedIr/CleanupPreparationTest.cpp`
- Create: `test/TypedIr/CleanupVerifierTest.cpp`
- Create: `test/TypedIr/EscapeAnalysisTest.cpp`
- Create: `test/TypedIr/EscapeVerifierTest.cpp`
- Create: `test/Runtime/OwnedSlotStateTest.cpp`
- Modify: `test/Codegen/TypedIrExecutionTest.cpp`
- Modify: `include/yona/TypedIr/Instruction.h`
- Modify: `include/yona/TypedIr/TypedIr.h`
- Modify: `include/yona/TypedIr/Builder.h`
- Modify: `src/TypedIr/Builder.cpp`
- Modify: `src/TypedIr/Verifier.cpp`
- Modify: `src/TypedIr/Printer.cpp`
- Modify: `src/TypedIr/Parser.cpp`
- Modify: `test/TypedIr/PrinterParserTest.cpp`
- Modify: `src/TypedIr/Pipeline.cpp`
- Modify: `src/Codegen/Llvm/FunctionLowerer.cpp`
- Modify: `src/Codegen/Llvm/BlockLowerer.cpp`
- Modify: `cmake/YonaComponents.cmake`
- Modify: `CMakeLists.txt`

**Interfaces:**

- Consumes: `RepresentationSelected` explicit CFG and structural
  call/aggregate sink contracts; at this milestone all skipped operation
  families are verifier-proven absent.
- Produces: immutable fixed-point `OwnershipAnalysisResult` and
  `EscapeAnalysisResult`; checked `TryRetainRuntime` terminators plus explicit
  Transfer/Release instructions;
  the verified no-async `ControlFlow -> AsyncPrepared` fast path; prepared
  owned-slot cleanup descriptors/drop entries; explicit edge-local cleanup
  blocks; verifiers; a deliberately disabled arena pass.
- Replaces at cutover: mutable Codegen transfer maps, delayed drops,
  `tco_cleanup_done_`, legacy `EscapeAnalysis`, and `LastUseAnalysis`.

- [ ] **Step 1: Write red CFG ownership and cleanup matrices**

Cover straight lines, asymmetric branches, owned block arguments, loops and
backedges, borrow-to-owning conversion, aggregate children, direct returns,
more than sixteen live owners, double consume, missing consume, and use after
transfer. Include a mixed base/tail-recursive CFG. Task 12 extends this matrix
with Raised and other explicit outcome edges before enabling those paths in
the shared pipeline. A core case is:

```cpp
TEST_CASE("Typed IR ownership: lowering balances each asymmetric branch") {
  auto Module = makeOwnedDiamond(/*consumeOnThen=*/true,
                                 /*returnOnElse=*/true);
  REQUIRE(runOwnershipLowering(Module).has_value());
  REQUIRE(verifyOwnership(Module).has_value());
  CHECK(countOnEdge<TransferInst>(Module, "then", "merge") == 1);
  CHECK(countOnEdge<ReleaseInst>(Module, "else", "return") == 1);
}
```

Escape cases must include `xs -> Some xs -> return`, nested tuple/ADT/sequence
containment, exact-sequence and typed-pattern payloads, closure capture,
unknown native storage, async handoff, control payloads, aliases, and a proven
local nonescape.
Cleanup cases include a static finalizer, a finalizer closure capturing an
outer value, exact signature/effect/ownership rejection, a foreign or
non-dominating finalizer ValueId, and one resource exit along every CFG edge;
closure-conversion tests prove the captured environment reaches the cleanup
action without storing a bare descriptor.
Create top-level sentinels `Typed IR cleanup: edge blocks execute each action
once` and `Typed IR escape: propagates through nested owned aggregates` in
addition to the ownership case above.
Now that the real ownership/cleanup suffix exists, add execution cases `Typed
IR execution: closure returns a captured Float` and `Typed IR execution:
mutually recursive closures share an outer capture` at O0-O3. They must pass
through `runOwnershipLowering` and `runCleanupLowering`; the recursive hot path
still has one group allocation and zero per-hop retains/allocations.

- [ ] **Step 2: Run and confirm missing analyses/verifiers**

```bash
cmake --build --preset build-debug-linux --target tests -j2
python3 scripts/run-focused-tests.py ./out/build/x64-debug-linux/tests \
  -tc='Typed IR ownership*,Typed IR cleanup*,Typed IR escape*'
```

Expected: compile failures for the analysis/pass headers.

- [ ] **Step 3: Define ownership operations and cleanup regions**

Task 9 already declared `RetainInst` as a pre-ownership intent for
recursive-member escapes. Add the remaining explicit operations and make Task
10's ownership verifier/lowering own all three uniformly:

```cpp
struct TransferInst { ValueId Source; };
struct ReleaseInst { ValueId Source; };
struct TryRetainRuntime {
  ValueId Source;
  ProducedBranchTarget Success; // exactly one Owned same-Type result
  RuntimeFailureDisposition Failure;
};

struct ReleaseValue { ValueId Value; };
struct InvokeFinalizer {
  ValueId Resource;
  ValueId Callable;
};
struct ReleaseResource {
  ValueId Resource;
  model::NominalTypeKey Declaration;
};
using CleanupAction =
    std::variant<ReleaseValue, ReleaseResource, InvokeFinalizer>;
struct CleanupRegion {
  CleanupRegionId Id;
  std::optional<CleanupRegionId> Parent;
  std::vector<CleanupAction> Actions;
};
```

`RetainInst` is an ordinary result-producing instruction only through
`RepresentationSelected`; it denotes a requested second owner but does not
assert that the selected instance is shareable. `runOwnershipLowering`
eliminates every source-written or analysis-inserted `RetainInst` by splitting
its block at that point and replacing it with `TryRetainRuntime`. Its Success
prefix is exactly one Owned value with the source's TypeId and selected
representation, and all former uses of the instruction result are remapped to
that block argument. Ordinary generated `RetainInst` is legal only where
source semantics/provenance proves duplication admissible (normally a
statically `ALWAYS_SHAREABLE` type); an instance-dependent false is a checked
compiler-invariant edge, never new source control. The source and, for a
Borrow, its backing owner remain live on both edges. A false retain produces
no owner and follows `TrapCompilerFailure::CleanupThenTrap` with every live
owner available for cleanup; it produces no source or diagnostic outcome.
`OwnershipLowered` and every later phase reject residual
`RetainInst`; `TryRetainRuntime` survives `CleanupLowered` and `LlvmReady` and
participates in the terminator, successor, operand, parser/printer, clone,
FunctionId/ValueId-remap, phase, ownership, and cleanup visitors.
Concretely, add `TryRetainRuntime` to the closed `Terminator` variant and to
`SuccessorView`. Builder assigns pairwise-distinct deterministic Success and
Failure `EdgeId`s in that ordinal order; clone/remap preserves them. Only the
Success edge may carry its one produced prefix, and the verifier rejects any
Failure prefix or same-ID/same-storage edge alias even when both destinations
are the same block.

For `AbiValue`, LLVM calls
`YonaRuntimeAbiValueConforms` against the emitted descriptor for
`Source.Type`; false follows the same unchanged Failure edge. Only after that
check does it call
`YonaRuntimeAbiValueTryRetain(BorrowedSource, EmptyOutput)` exactly once and
use its initialized output on Success; this dispatches Sum and
ExceptionValue carriers through their actual runtime descriptor. For a
selected managed pointer, LLVM calls the exact descriptor
`TryRetain(Word)` once and, only on true, forwards the same pointer as the
produced owner. False produces no value; both paths branch to the stored
successors, and Trivial values never reach either one. Tests cover
always-shareable, linear, and
instance-sensitive values, actual alternatives and a nonmember of a static
Sum, actual nominal and nonexception carriers under ExceptionValue, wrong
exact types, a false retain with an unrelated live owner, and
round-trip/remap of both the intent and checked terminator.

Extend `Function` with `std::vector<CleanupRegion> CleanupRegions`, extend
every `Block` with `std::optional<CleanupRegionId> CleanupRegion`, and add
`Builder::createCleanupRegion`, `Builder::enterCleanupRegion`, and
`Builder::registerCleanupAction`. The region assigned to a block is its
innermost active region; parent links determine every edge's exited region
set.

`InvokeFinalizer::Callable` is an ordinary function-typed SSA value, not a
descriptor without an environment. Canonical lowering materializes the
resolved finalizer as `MakeFunctionInst` (or, before generic preparation, the
trait-method value defined in Task 15) after successful acquisition and
registers both owning operands. Generic preparation resolves trait methods;
effect preparation may rehome the record; the single closure-conversion
visitor scans cleanup actions and rewrites the callable's defining value to
the appropriate `MakeClosureInst` while preserving its ValueId. From
`ClosureConverted` onward the verifier requires that value to name a complete
callable representation with its environment and exact `Consume Resource ->
Unit` DirectReturn signature whose closed effect row is empty (`MayRaise ==
false`, `MayCancel == false`). Implicit resource finalizers are deliberately
infallible/no-effect destructors; an operation that can fail exposes an
explicit `close : Resource -> Result ...` API instead of serving as `with`'s
finalizer. It rejects a bare/foreign descriptor, a
non-dominating callable or resource, wrong effects/ownership, and any cleanup
record referring to another function. Cleanup edge planning threads both
ValueIds explicitly, so captured and static finalizers obey the same SSA and
ownership rules.

`ReleaseResource` is the separate compiler-resource cleanup action. Its
operand must be one Owned ResourceType whose exact key resolves to a Linear or
AlwaysShareable row in `Module::Resources`; its declaration field must match
that key. Cleanup lowering moves the owner to a private slot, clears the SSA
obligation, and calls the descriptor's infallible `Release` exactly once. It
has no callable, trait lookup, allocation, or source outcome and cannot be
used for a nominal value. `ReleaseValue` remains ordinary ownership cleanup;
`InvokeFinalizer` remains a semantically selected source callable. Verifier,
printer/parser, remap, closure scanning, edge planning, and all-outcome tests
distinguish all three alternatives.

Establish the async phase boundary before cleanup even though Task 14 adds the
first async forms. `Passes/AsyncPreparation.h` exposes:

```cpp
PassResult runAsyncPreparation(Module &);
```

At this milestone it accepts `ControlFlow` only when the module contains no
Promise-demand pseudo-instruction, task/group operation, async plan, or async
linkage. It transactionally verifies that empty async vocabulary and changes
only the phase to `AsyncPrepared`. Task 14 extends this same function and file,
retains the empty fast path, and adds real preparation. This makes every
intermediate task compile and gives Task 13 a real, verifier-backed input
phase instead of a test-only phase mutation.

Declare the cleanup phase boundary in `Passes/CleanupPreparation.h`:

```cpp
PassResult runCleanupPreparation(Module &);
```

`runCleanupPreparation` consumes `AsyncPrepared`, produces `CleanupPrepared`,
and predeclares one private
`FunctionEntryKind::CleanupDrop` entry and one
`CleanupObligationDescriptorPlan` per structurally distinct active cleanup
suffix. The entry receives an owned slot state plus the never-cancelled
context, takes each still-armed resource/finalizer field, and executes
finalizers in reverse acquisition order. It is verifier-proven infallible,
nonperforming, and noncancelling and returns only to the runtime drop callback;
it has no lexical captures beyond its typed state fields. It participates in
the function freeze, and cleanup lowering never synthesizes a call or function
after effect normalization.

Use exact prepared carriers:

```cpp
using CleanupObligationDescriptorId =
    StrongId<struct CleanupObligationDescriptorIdTag>;
struct CleanupObligationActionRecipe {
  enum class Kind : std::uint8_t {
    ReleaseField = 0, ReleaseResourceField = 1, InvokeFinalizer = 2
  };
  Kind ActionKind;
  std::uint32_t ResourceField;
  std::optional<std::uint32_t> CallableField;
};
struct CleanupObligationDescriptorPlan {
  CleanupObligationDescriptorId Id;
  model::TypeId StateType; // OwnedSlotStateKind::CleanupObligation
  FunctionId DropFunction;
  std::vector<CleanupObligationActionRecipe> Actions;
};
struct PackCleanupObligation {
  CleanupObligationDescriptorId Descriptor;
  std::vector<ValueId> Owners;
  ProducedBranchTarget Success; // one Owned obligation prefix
  RuntimeFailureDisposition Failure;
};
struct TakeCleanupObligation {
  CleanupObligationDescriptorId Descriptor;
  ValueId Obligation;
  ProducedBranchTarget Success; // descriptor-ordered restored-owner prefix
  RuntimeFailureDisposition Failure;
};
```

Both are prepared terminators because state allocation and defensive runtime
validation can fail. Pack's Success begins with its one declared Owned
obligation block argument and
all source owners are cleared; its mandatory `TrapCompilerFailure` leaves
every owner unchanged, releases the C diagnostic outcome, runs cleanup, and
traps. Take's Success consumes/clears the obligation and its produced prefix
yields each owner in descriptor order; its verifier-proven normal path is
allocation-free and infallible, while a false runtime result denotes carrier
corruption and takes the same trap disposition without partially taking a
field. Both internal operations use `TrapCompilerFailure`. If
an armed obligation is released because a request, continuation, or
resume callable is abandoned, its state descriptor invokes the predeclared
drop entry exactly once. Because implicit finalizers have no operation/raise/
cancel row, destruction needs no outcome replacement or new continuation.
Double take/run and partial restoration are unrepresentable.
Descriptor actions contain only state-field ordinals. The source Pack's
`Owners` maps its function-local `CleanupAction` operands into those fields;
the generated DropFunction refers only to its own state parameter and recipes.
No module-owned plan or generated function retains a foreign source `ValueId`.

`OwnedSlotState.h` defines the shared runtime primitive here, before either
cleanup lowering or Task 13 can consume it:

```c
typedef struct {
  uint32_t AbiVersion;
  uint32_t Reserved;
  const YonaAbiTypeDescriptor *CarrierType;
  const YonaAbiTypeDescriptor *const *FieldTypes;
  const YonaAbiParameterOwnership *FieldOwnerships;
  uint64_t FieldCount;
  uint64_t DropIdentityFingerprint;
  const uint8_t *DropIdentityCanonicalBytes;
  uint64_t DropIdentityCanonicalByteCount;
  void (*DropMove)(YonaAbiValue *OwnedState,
                   const YonaExecutionContext *BorrowedContext);
} YonaOwnedSlotStateDescriptor;
bool YonaRuntimeOwnedSlotStateCreateMove(
    const YonaOwnedSlotStateDescriptor *Descriptor,
    YonaAbiArgument *Captures, uint64_t CaptureCount,
    YonaAbiValue *EmptyOutput, YonaControlOutcome *EmptyFailure);
bool YonaRuntimeOwnedSlotStateFieldBorrowed(
    const YonaAbiValue *BorrowedState, uint64_t Index,
    const YonaAbiValue **EmptyBorrowedField,
    YonaControlOutcome *EmptyFailure);
bool YonaRuntimeOwnedSlotStateFieldTakeMove(
    YonaAbiValue *BorrowedUniqueState, uint64_t Index,
    YonaAbiValue *EmptyField, YonaControlOutcome *EmptyFailure);
bool YonaRuntimeOwnedSlotStateTakeAllMove(
    YonaAbiValue *OwnedState, YonaAbiValue *EmptyFields,
    uint64_t FieldCount, YonaControlOutcome *EmptyFailure);
```

The descriptor gives the canonical `OwnedSlotStateType`, ordered field
types/ownerships, identity bytes, and mandatory infallible `DropMove` entry;
create stages all fields before moving, field Borrow/Take uses checked
ordinals, and final release calls `DropMove` once. `TakeAllMove` is the sole
lowering of `TakeCleanupObligation`: it validates the complete state, count,
every initially Empty pairwise-disjoint output, and failure slot before one
infallible commit clears the state and moves all fields. False leaves state and
every field unchanged; forced validation failure at each ordinal proves no
partial restoration. Cleanup obligations use a
non-null generated drop entry; handler/try drop entries perform their declared
ordinary remaining-field release. No source-visible aggregate or generic mutable container gains
this capability.

The generic `YonaAbiTypeDescriptor::Release` slot has the distinct
`void(YonaAbiWord)` prototype and therefore never points at `DropMove` by cast.
It points at the private C adapter `ownedSlotStateAbiRelease`. Every state
object header stores its exact immutable `YonaOwnedSlotStateDescriptor *`;
the adapter obtains that pointer, reconstructs one owned
`YonaAbiValue{Descriptor->CarrierType, OwnedWord}`, and calls the descriptor's
`DropMove` with the canonical never-cancelled execution context exactly once.
`DropMove` clears the reconstructed value, releases every still-present field,
and destroys the state shell; the adapter performs no second release/free.
Descriptor validation proves the header/carrier/extended descriptor agree
before publication, and no function-pointer reinterpret cast is permitted.
Runtime tests release zero-field and all three state kinds through the generic
ABI callback as well as their direct drop path and require identical
exactly-once field/shell counts under normal, partially taken, and abandoned
states.

Printer/parser, operand/remap visitors, phase legality, ownership analysis,
closure/function-set freeze, representation selection, and LLVM lowering cover
both obligation terminators and the descriptor arena. Forced-OOM pack,
wrong-descriptor take, normal resume, request abandonment, and resume-callable
abandonment tests prove unchanged failure inputs and exactly-once finalization.
`Pipeline.cpp` invokes async preparation and then this pass exactly once before
effect preparation; `Verifier.cpp` dispatches both phase verifiers.
`NoAsyncPreparationTest.cpp` proves rejection of every nonempty async form and
an exact `ControlFlow -> AsyncPrepared` transition;
`CleanupPreparationTest.cpp` proves wrong input phase, transactional failure,
deterministic descriptor interning, generated-function legality, and an exact
`AsyncPrepared -> CleanupPrepared` transition.

A `RetainInst` intent produces a new Owned value only on
`TryRetainRuntime::Success`; Failure produces no value and leaves the source
and any backing owner live. `Transfer` consumes one Owned value and
produces a new Owned carrier; the source is dead, while the result follows the
ordinary Owned rule of any number of borrow observations followed by exactly
one consuming sink. `Release` consumes one Owned value. Every call,
aggregate constructor, collection insertion, capture, block argument, return,
and outcome payload declares Borrow/Consume on each operand.

- [ ] **Step 4: Implement fixed-point ownership and lowering**

Expose immutable results:

```cpp
enum class OwnerAvailability : std::uint8_t {
  NotOwned = 0, Live = 1, Consumed = 2
};
class OwnershipState final {
public:
  explicit OwnershipState(std::vector<OwnerAvailability> ValuesById);
  [[nodiscard]] OwnerAvailability at(ValueId Value) const;
  [[nodiscard]] std::span<const OwnerAvailability> values() const noexcept;
  friend bool operator==(const OwnershipState &,
                         const OwnershipState &) = default;
private:
  std::vector<OwnerAvailability> ValuesById_;
};
enum class EdgeOwnerAction : std::uint8_t {
  ForwardBorrow = 0, TransferOwner = 1, ReleaseOwner = 2
};
struct EdgeOwnershipFact {
  ValueId Source;
  EdgeOwnerAction Action;
  std::optional<ValueId> BackingOwner;
  std::optional<ValueId> SuccessorArgument;
  std::optional<ValueId> SuccessorBackingOwnerArgument;
};
struct EdgeOwnershipState {
  OwnershipState SuccessorEntry;
  std::vector<EdgeOwnershipFact> Facts;
};
struct PlannedCleanupBlock {
  CleanupRegionId Region;
  std::vector<ValueId> ThreadedOperands;
  std::vector<CleanupAction> Actions;
};
using CleanupEdgePlan = std::vector<PlannedCleanupBlock>;
struct OwnershipAnalysisResult {
  const OwnershipState &entry(BlockId) const;
  const OwnershipState &exit(BlockId) const;
  const EdgeOwnershipState &edge(EdgeId) const;
  const CleanupEdgePlan &cleanupEdge(EdgeId) const;
};

OwnershipAnalysisResult analyzeOwnership(const Module &, FunctionId);
PassResult runOwnershipLowering(Module &);
VerificationResult verifyOwnership(const Module &);
```

`ValuesById_` has exactly `Function.Values.size()` entries and is indexed only
after checking the ValueId's `FunctionLocalDomain`: Trivial/Borrowed
values are `NotOwned`; an Owned definition is `Live` until its unique sink and
`Consumed` afterward. Edge facts are sorted by source ID. `SuccessorArgument`
is present only for borrow/transfer into a block argument and names that newly
defined argument; release has no successor. `BackingOwner` is required only
for `OwnedValue`/caller-local `ScopedUniqueLoan` and must equal its
`Value::Borrow::Root`; `BorrowParameter`, `StaticLifetime`, and
`ScopedUniqueLoanParameter` have no caller Owned owner field. A derived
unique-loan-parameter view is instead tied to its local actual parameter and
cannot cross a general CFG/call boundary. Forwarding an owner-rooted borrow also records the successor's
corresponding owner argument when the root does not already dominate the
successor. A join fed by borrows rooted in
different predecessor owners must therefore take two block arguments—one
explicit Owned owner and one Borrowed view whose provenance names that owner
argument. It may not erase the distinction into a provenance-free borrow.
The same edge may transfer that owner and forward its borrow only when both
arrive together and the successor borrow is dominated by the new owner
argument; releasing or otherwise consuming an owner while forwarding a borrow
from it is rejected. Static borrows carry neither owner field.
A join is legal only when the
rewritten predecessor facts yield the declared successor-entry state, so
asymmetric Live/Consumed paths must be reconciled by an edge transfer/release
rather than a lossy lattice join.

Every successor occurrence owns a distinct function-local `EdgeId`, stored in
its `BranchTarget`/`ProducedBranchTarget`; it is not inferred from the
destination block. Builder assigns IDs in deterministic source-terminator and
successor-ordinal order, `SuccessorView` returns `{EdgeId, ordinal, target}` and
the verifier proves each ID occurs exactly once under its source terminator.
Ownership and cleanup maps key only by that ID. Parser/printer/remappers,
edge-splitting, block rewrites, and every terminator-specific successor visitor
preserve or explicitly replace it. Tests give a CondBranch two different edges
to the same block and a Switch several same-target cases with different
arguments/releases; facts and cleanup blocks must remain distinct.

`runOwnershipLowering` consumes `RepresentationSelected` and produces
`OwnershipLowered`; `runCleanupLowering` consumes that phase and produces
`CleanupLowered`; the pipeline then calls Task 8's
`runLlvmReadinessVerification` and no other code can publish `LlvmReady`.

`LlvmBlockLowerer` has explicit cases for the surviving `TryRetainRuntime`,
`TransferInst`, and `ReleaseInst`: checked retain invokes the selected
exact-signature adapter or `YonaRuntimeAbiValueTryRetain` and branches,
transfer forwards the physical carrier and invalidates only the Typed IR
source, and release invokes the selected descriptor callback (or
`YonaRuntimeAbiValueRelease` for an AbiValue carrier). Trivial values never
receive these operations. `LlvmFunctionLowerer` supplies the descriptor/value
mapping; neither lowerer recomputes ownership. Add a focused LLVM test
containing all three surviving operations and assert the expected checked
retain/release calls, both retain successors, and absence of any
unhandled-opcode path.

Iterate blocks to a fixed point in reverse postorder, joining per-value states
at block arguments. Split edges when predecessor-specific retain/release work
differs. Verify one consume per Owned definition on every complete path,
including every loop backedge; never use a fixed owner-count array.
Add regressions for a branch-local projection whose owner is released on the
sibling edge, a join receiving equivalent borrows from two different owners,
an explicit owner-plus-borrow phi that succeeds, and rejection of a returned
projection or forwarded borrow whose backing owner is moved/released on the
same edge.

- [ ] **Step 5: Implement containment-aware escape propagation**

Build alias edges and owner-to-child containment edges. Seed escape roots with
return, closure capture, global/unknown/native storage, async handoff, and
control-outcome payloads; propagate to a fixed point. `runArenaPlacement`
returns the input unchanged and an explicit `DisabledUntilOwnershipParity`
decision. `verifyEscape` rejects any manually arena-marked value that reaches
an escape root or owns a child with a longer lifetime.

- [ ] **Step 6: Materialize cleanup edges and run all verifiers**

`OwnershipLowering` computes edge obligations and materializes only ordinary
edge retains/transfers/releases. `runCleanupLowering` owns all region-exit
materialization: it emits reverse-order cleanup blocks and threads the
in-flight outcome/value plus every release/finalizer operand as block
arguments, so no synthesized block references a non-dominating `ValueId`.
`verifyCleanup` proves each exit crosses each required region exactly once and
that ownership/cleanup never materialize the same release twice.

Cleanup plans permanently contain releases and only verifier-proven
effect-free `DirectReturn` finalizers; no `InvokeOutcome` dependency exists.
Ownership
analysis records their deterministic logical edge expansion in
`CleanupEdgePlan`, cleanup lowering materializes exactly that plan, and the
ownership verifier recomputes facts on the result. No later task admits a
nonlocal-capable implicit finalizer.

At this milestone `runTypedIrPipeline` supports direct, nonlocal-free,
effect-free functions through the authoritative order and rejects everything
else instead of pretending it is `LlvmReady`. Task 12 inserts general outcome
edges; Tasks 13-14 add performed/async exits before the same ownership and
cleanup suffix runs.

Run:

```bash
python3 scripts/run-focused-tests.py ./out/build/x64-debug-linux/tests \
  -tc='Typed IR ownership*,Typed IR cleanup*,Typed IR escape*'
python3 scripts/quality.py --build-dir out/build/x64-debug-linux \
  --preset x64-debug-linux --jobs 2 sanitize
git diff --check
```

Expected: all generated CFG matrices verify and sanitizer allocation probes
balance without enabling arenas.

- [ ] **Step 7: Commit the ownership proof layer**

```bash
git add include/yona/TypedIr src/TypedIr test/TypedIr \
  include/yona/Runtime/Core/OwnedSlotState.h \
  src/Runtime/Core/OwnedSlotState.c test/Runtime/OwnedSlotStateTest.cpp \
  test/Codegen/TypedIrExecutionTest.cpp \
  src/Codegen/Llvm/FunctionLowerer.cpp src/Codegen/Llvm/BlockLowerer.cpp \
  cmake/YonaComponents.cmake CMakeLists.txt
git add docs/superpowers/plans/2026-09-01-typed-ir-codegen-rewrite.md
git commit -m "feat: prove typed ir ownership across cfg"
```

### Task 11: Lower uniform aggregates, descriptor-first collections, and safe generators

**Files:**

- Create: `include/yona/Runtime/Collections/Aggregate.h`
- Create: `src/Runtime/Collections/Aggregate.c`
- Create: `include/yona/Runtime/Collections/Cursor.h`
- Create: `src/Runtime/Collections/Cursor.c`
- Modify: `include/yona/Runtime/Collections/Sequence.h`
- Modify: `include/yona/Runtime/Collections/Set.h`
- Modify: `include/yona/Runtime/Collections/Dictionary.h`
- Modify: `include/yona/Runtime/Collections/Arrays.h`
- Modify: `src/Runtime/Collections/Sequence.c`
- Modify: `src/Runtime/Collections/DictionarySet.c`
- Modify: `src/Runtime/Collections/Hamt.c`
- Modify: `src/Runtime/Collections/HamtInternal.h`
- Modify: `src/Runtime/Collections/Arrays.c`
- Create: `include/yona/Runtime/Stdlib/Iterator.h`
- Modify: `src/Runtime/Stdlib/Iterator.c`
- Create: `include/yona/Runtime/Stdlib/String.h`
- Create: `src/Runtime/Stdlib/String.c`
- Create: `include/yona/Semantics/RuntimeEntryRegistry.h`
- Create: `include/yona/Semantics/RuntimeEntryRegistry.def`
- Create: `src/Semantics/RuntimeEntryRegistry.cpp`
- Create: `test/Semantics/RuntimeEntryRegistryTest.cpp`
- Modify: `src/Runtime/Stdlib/Native.c`
- Modify: `src/Runtime/Platform/FileLinux.c`
- Modify: `src/Runtime/Platform/FileMacOs.c`
- Modify: `src/Runtime/Platform/FileWindows.c`
- Modify: `src/Runtime/Core/Internal.h`
- Modify: `src/Runtime/Core/Runtime.c`
- Modify: `include/yona/Syntax/Ast.h`
- Modify: `include/yona/Syntax/AstVisitor.h`
- Modify: `include/yona/Syntax/AstVisitorImpl.h`
- Modify: `include/yona/Syntax/Lexer.h`
- Modify: `include/yona/Syntax/Parser.h`
- Modify: `include/yona/Support/SourceManager.h`
- Modify: `src/Syntax/Ast.cpp`
- Modify: `src/Syntax/Lexer.cpp`
- Modify: `src/Syntax/Parser.cpp`
- Modify: `src/Syntax/ParserModule.cpp`
- Modify: `src/Syntax/ParserImpl.h`
- Modify: `src/Support/SourceManager.cpp`
- Modify: `include/yona/Semantics/SemanticModel.h`
- Modify: `src/Semantics/SemanticModel.cpp`
- Modify: `include/yona/Semantics/TypeChecker.h`
- Modify: `src/Semantics/TypeChecker.cpp`
- Modify: `test/Syntax/AstTest.cpp`
- Modify: `test/Syntax/LexerTest.cpp`
- Modify: `test/Semantics/SemanticModelTest.cpp`
- Modify: `test/Semantics/TypeCheckerTest.cpp`
- Create: `test/Support/CompilerStdlibSourceFixture.h`
- Create: `include/yona/TypedIr/Passes/GeneratorLowering.h`
- Create: `src/TypedIr/Passes/GeneratorLowering.cpp`
- Create: `include/yona/TypedIr/Passes/RuntimeFailureNormalization.h`
- Create: `src/TypedIr/Passes/RuntimeFailureNormalization.cpp`
- Create: `include/yona/TypedIr/Passes/KeyOperationsPreparation.h`
- Create: `src/TypedIr/Passes/KeyOperationsPreparation.cpp`
- Create: `test/TypedIr/RuntimeFailureNormalizationTest.cpp`
- Create: `test/TypedIr/KeyOperationsPreparationTest.cpp`
- Create: `test/TypedIr/CollectionOwnershipTest.cpp`
- Create: `test/TypedIr/GeneratorLoweringTest.cpp`
- Create: `test/Runtime/CursorTest.cpp`
- Create: `test/Runtime/StringStorageTest.cpp`
- Create: `test/Codegen/CollectionLoweringTest.cpp`
- Modify: `test/Codegen/TypedIrExecutionTest.cpp`
- Create: `test/Fixtures/TypedIr/Ownership/seq_generator_named_reuse.yona`
- Create: `test/Fixtures/TypedIr/Ownership/seq_generator_named_reuse.expected`
- Create: `test/Fixtures/TypedIr/Ownership/seq_generator_named_reuse_fused.yona`
- Create: `test/Fixtures/TypedIr/Ownership/seq_generator_named_reuse_fused.expected`
- Create: `test/Fixtures/TypedIr/Ownership/iterator_early_drop_owned_source.yona`
- Create: `test/Fixtures/TypedIr/Ownership/iterator_early_drop_owned_source.expected`
- Create: `test/Fixtures/TypedIr/Ownership/closure_capture_aggregate.yona`
- Create: `test/Fixtures/TypedIr/Ownership/closure_capture_aggregate.expected`
- Create: `test/Fixtures/TypedIr/Ownership/currying_aggregate_result.yona`
- Create: `test/Fixtures/TypedIr/Ownership/currying_aggregate_result.expected`
- Create: `test/Fixtures/TypedIr/Ownership/pattern_payload_escape.yona`
- Create: `test/Fixtures/TypedIr/Ownership/pattern_payload_escape.expected`
- Modify: `test/Runtime/HamtRcTest.cpp`
- Modify: `test/Runtime/RuntimeGuardsTest.cpp`
- Modify: `test/Codegen/PatternOwnershipTest.cpp`
- Modify: `include/yona/TypedIr/Instruction.h`
- Modify: `include/yona/TypedIr/TypedIr.h`
- Modify: `src/TypedIr/Verifier.cpp`
- Modify: `src/TypedIr/Printer.cpp`
- Modify: `src/TypedIr/Parser.cpp`
- Modify: `src/TypedIr/AstLowering.cpp`
- Modify: `src/TypedIr/Analysis/OwnershipAnalysis.cpp`
- Modify: `src/TypedIr/Analysis/EscapeAnalysis.cpp`
- Modify: `src/TypedIr/Verification/OwnershipVerifier.cpp`
- Modify: `src/TypedIr/Verification/EscapeVerifier.cpp`
- Modify: `src/TypedIr/PatternCanonicalization.cpp`
- Modify: `src/TypedIr/Passes/ControlFlowLowering.cpp`
- Modify: `test/TypedIr/PatternCanonicalizationTest.cpp`
- Modify: `test/TypedIr/ControlFlowLoweringTest.cpp`
- Modify: `src/TypedIr/Pipeline.cpp`
- Modify: `src/Codegen/Llvm/ModuleLowerer.cpp`
- Modify: `src/Codegen/Llvm/BlockLowerer.cpp`
- Modify: `src/Codegen/Llvm/FunctionLowerer.cpp`
- Modify: `src/Codegen/Llvm/TypeLowering.cpp`
- Modify: `cmake/YonaComponents.cmake`
- Modify: `CMakeLists.txt`

**Interfaces:**

- Consumes: Task 7 descriptors and Task 10 ownership/cleanup rules.
- Produces: one uniform heap representation for tuples, records, and ADTs;
  descriptor-first replacement collection APIs; explicit cursor IR and CFG
  loops; managed `typeOf` results.
- Coexistence: replacement constructors use the existing RC-compatible heap
  layouts but derive all child-heap metadata from descriptors before storing
  the first child. Legacy post-construction mask setters remain frozen for the
  oracle and are deleted in Task 17.

- [ ] **Step 1: Write red runtime replacement/ownership tests**

Extend HAMT RC tests for duplicate managed set keys, duplicate dictionary
keys, old-value release, same-pointer replacement, and path-copy/unique-owner
behavior. The descriptor must be present before the first insertion:

```cpp
TEST_CASE("HamtRc replacement dictionary consumes duplicate key and old value") {
  RcProbe Key;
  RcProbe OldValue;
  RcProbe NewValue;
  auto Dictionary = createDictionary(Key.descriptor(), OldValue.descriptor());
  Dictionary = putOwned(Dictionary, Key.owner(), OldValue.owner());
  Dictionary = putOwned(Dictionary, Key.retainedOwner(), NewValue.owner());
  releaseDictionary(Dictionary);
  CHECK(Key.balanced());
  CHECK(OldValue.balanced());
  CHECK(NewValue.balanced());
}
```

Add Typed IR tests proving functional heap ADT update uses
`UpdateAggregateInst`, never LLVM insertvalue; `typeOf` returns a managed
String/nominal payload; and exact-sequence/typed-pattern children retain an
owner when escaping.

- [ ] **Step 2: Write generator source-reuse and cleanup red fixtures**

`seq_generator_named_reuse.yona` must use the source after comprehension:

```yona
let source = [1, 2, 3], mapped = [x + 1 for x = source]
in (mapped, source)
```

Expected output is `([2, 3, 4], [1, 2, 3])`. Add a fused named-source case
plus iterator early-drop allocation checks. Each executable fixture runs with
`YONA_ALLOC_STATS=1` and expects zero live allocations for its managed tags.
Add the deferred source-level captured aggregate and curried aggregate-result
fixtures here and run them at O0-O3.
Create top-level sentinels `Typed IR generators: named persistent sources are
retained before cursor open` and `Typed IR execution: ownership survives
aggregate and generator lowering`; Task 11's collection sentinel is the exact
`Typed IR collections: heap ADT update uses runtime functional update` case.

- [ ] **Step 3: Run and verify descriptor/cursor failures**

```bash
cmake --build --preset build-debug-linux --target tests -j2
python3 scripts/run-focused-tests.py ./out/build/x64-debug-linux/tests \
  -tc='Typed IR collections*,Typed IR generators*,Typed IR runtime failure normalization*,HamtRc*'
python3 scripts/run-focused-tests.py ./out/build/x64-debug-linux/tests \
  -ts='RuntimeGuards' -tc='*iterator*'
python3 scripts/run-focused-tests.py ./out/build/x64-debug-linux/tests \
  -tc='Runtime String storage*'
```

Expected: new APIs are absent and source-reuse fixtures cannot lower.

- [ ] **Step 4: Add uniform aggregate and descriptor-first APIs**

Expose ABI-distinct final replacement C contracts. They coexist with, but
never call, the frozen legacy constructors until Task 17:

```c
typedef void *YonaAggregateRef;
bool YonaRuntimeAbiAggregateBuildStructuralMove(
    const YonaAbiTypeDescriptor *TupleOrRecordType,
    YonaAbiValue *OwnedFields, uint64_t FieldCount,
    YonaAggregateRef *EmptyOutput,
    YonaControlOutcome *EmptyFailure);
bool YonaRuntimeAbiAggregateBuildNominalMove(
    const YonaAbiTypeDescriptor *NominalType,
    uint64_t ConstructorTag, YonaAbiValue *OwnedFields,
    uint64_t FieldCount,
    YonaAggregateRef *EmptyOutput, YonaControlOutcome *EmptyFailure);
bool YonaRuntimeAbiAggregateProjectBorrowed(
    YonaAggregateRef Aggregate, uint64_t FieldIndex,
    const YonaAbiValue **BorrowedField,
    YonaControlOutcome *EmptyFailure);
bool YonaRuntimeAbiAggregateProjectOwned(YonaAggregateRef Aggregate,
                                         uint64_t FieldIndex,
                                         YonaAbiValue *EmptyOutput,
                                         YonaControlOutcome *EmptyFailure);
bool YonaRuntimeAbiAggregateTakeFieldMove(
    YonaAggregateRef *OwnedAggregate, uint64_t FieldIndex,
    YonaAbiValue *EmptyOutput, YonaControlOutcome *EmptyFailure);
bool YonaRuntimeAbiAggregateUpdateMove(YonaAggregateRef *OwnedAggregate,
                                       uint64_t FieldIndex,
                                       YonaAbiValue *OwnedReplacement,
                                       YonaAggregateRef *EmptyOutput,
                                       YonaControlOutcome *EmptyFailure);

typedef void *YonaSequenceRef;
bool YonaRuntimeAbiSequenceBuildMove(
    const YonaAbiTypeDescriptor *SequenceType,
    YonaAbiValue *OwnedElements, uint64_t ElementCount,
    YonaSequenceRef *EmptyOutput,
    YonaControlOutcome *EmptyFailure);
bool YonaRuntimeAbiSequenceAppendMove(YonaSequenceRef *OwnedSequence,
                                      YonaAbiValue *OwnedElement,
                                      YonaSequenceRef *EmptyOutput,
                                      YonaControlOutcome *EmptyFailure);
bool YonaRuntimeAbiSequencePrependMove(YonaSequenceRef *OwnedSequence,
                                       YonaAbiValue *OwnedElement,
                                       YonaSequenceRef *EmptyOutput,
                                       YonaControlOutcome *EmptyFailure);

typedef void *YonaSetRef;
typedef bool (*YonaAbiKeyHashEntry)(const YonaAbiValue *BorrowedKey,
                                    uint64_t *OutputHash);
typedef bool (*YonaAbiKeyEqualsEntry)(const YonaAbiValue *BorrowedLeft,
                                      const YonaAbiValue *BorrowedRight,
                                      uint32_t *OutputEqual);
typedef struct {
  uint32_t AbiVersion;
  uint32_t Reserved;
  uint64_t StructuralFingerprint;
  const uint8_t *CanonicalBytes;
  uint64_t CanonicalByteCount;
  const YonaAbiTypeDescriptor *KeyType;
  YonaAbiKeyHashEntry HashBorrowed;
  YonaAbiKeyEqualsEntry EqualsBorrowed;
} YonaAbiKeyOperationsDescriptor;
bool YonaRuntimeAbiKeyOperationsEquivalent(
    const YonaAbiKeyOperationsDescriptor *Left,
    const YonaAbiKeyOperationsDescriptor *Right);
bool YonaRuntimeAbiSetCreate(const YonaAbiKeyOperationsDescriptor *ElementOps,
                             YonaSetRef *EmptyOutput,
                             YonaControlOutcome *EmptyFailure);
bool YonaRuntimeAbiSetBuildMove(
    const YonaAbiTypeDescriptor *SetType,
    const YonaAbiKeyOperationsDescriptor *ElementOps,
    YonaAbiValue *OwnedElements, uint64_t ElementCount,
    YonaSetRef *EmptyOutput,
    YonaControlOutcome *EmptyFailure);
bool YonaRuntimeAbiSetInsertMove(YonaSetRef *OwnedSet,
                                 YonaAbiValue *OwnedElement,
                                 YonaSetRef *EmptyOutput,
                                 YonaControlOutcome *EmptyFailure);

typedef void *YonaDictionaryRef;
typedef struct {
  YonaAbiValue Key;
  YonaAbiValue Value;
} YonaAbiOwnedDictionaryEntry;
bool YonaRuntimeAbiDictionaryCreate(
    const YonaAbiKeyOperationsDescriptor *KeyOps,
    const YonaAbiTypeDescriptor *ValueType,
    YonaDictionaryRef *EmptyOutput,
    YonaControlOutcome *EmptyFailure);
bool YonaRuntimeAbiDictionaryBuildMove(
    const YonaAbiTypeDescriptor *DictionaryType,
    const YonaAbiKeyOperationsDescriptor *KeyOps,
    YonaAbiOwnedDictionaryEntry *OwnedEntries,
    uint64_t EntryCount,
    YonaDictionaryRef *EmptyOutput,
    YonaControlOutcome *EmptyFailure);
bool YonaRuntimeAbiDictionaryPutMove(YonaDictionaryRef *OwnedDictionary,
                                     YonaAbiValue *OwnedKey,
                                     YonaAbiValue *OwnedValue,
                                     YonaDictionaryRef *EmptyOutput,
                                     YonaControlOutcome *EmptyFailure);

typedef uint32_t YonaAbiQueryStatus;
enum {
  YONA_ABI_QUERY_ERROR = 0u,
  YONA_ABI_QUERY_ABSENT = 1u,
  YONA_ABI_QUERY_PRESENT = 2u
};
bool YonaRuntimeAbiAggregateConstructorTag(
    YonaAggregateRef BorrowedAggregate, uint64_t *OutputTag,
    YonaControlOutcome *EmptyFailure);
YonaAbiQueryStatus YonaRuntimeAbiRecordFindField(
    YonaAggregateRef BorrowedRecord, const uint8_t *Utf8Name,
    uint64_t NameByteCount, uint64_t *OutputFieldIndex,
    YonaControlOutcome *EmptyFailure);
bool YonaRuntimeAbiSequenceLength(
    YonaSequenceRef BorrowedSequence, uint64_t *OutputLength,
    YonaControlOutcome *EmptyFailure);
YonaAbiQueryStatus YonaRuntimeAbiSequenceProjectBorrowed(
    YonaSequenceRef BorrowedSequence, uint64_t Index,
    const YonaAbiValue **EmptyBorrowedOutput,
    YonaControlOutcome *EmptyFailure);
YonaAbiQueryStatus YonaRuntimeAbiSequenceProjectOwned(
    YonaSequenceRef BorrowedSequence, uint64_t Index,
    YonaAbiValue *EmptyOutput, YonaControlOutcome *EmptyFailure);
YonaAbiQueryStatus YonaRuntimeAbiSequenceSliceOwned(
    YonaSequenceRef BorrowedSequence, uint64_t Start, uint64_t Count,
    YonaSequenceRef *EmptyOutput, YonaControlOutcome *EmptyFailure);
YonaAbiQueryStatus YonaRuntimeAbiSequenceSliceMove(
    YonaSequenceRef *OwnedSequence, uint64_t Start, uint64_t Count,
    YonaSequenceRef *EmptyOutput, YonaControlOutcome *EmptyFailure);
YonaAbiQueryStatus YonaRuntimeAbiSetContains(
    YonaSetRef BorrowedSet, const YonaAbiValue *BorrowedKey,
    YonaControlOutcome *EmptyFailure);
YonaAbiQueryStatus YonaRuntimeAbiDictionaryLookupBorrowed(
    YonaDictionaryRef BorrowedDictionary,
    const YonaAbiValue *BorrowedKey,
    const YonaAbiValue **EmptyBorrowedOutput,
    YonaControlOutcome *EmptyFailure);
YonaAbiQueryStatus YonaRuntimeAbiDictionaryLookupOwned(
    YonaDictionaryRef BorrowedDictionary,
    const YonaAbiValue *BorrowedKey, YonaAbiValue *EmptyOutput,
    YonaControlOutcome *EmptyFailure);
```

Arrays use a deliberately smaller callback-free leaf ABI. Public headers expose
three distinct incomplete object types—never interchangeable `void *`, raw
element pointers, sequence bridges, or callable callbacks:

```c
typedef struct YonaByteArrayObject *YonaByteArrayRef;
typedef struct YonaIntArrayObject *YonaIntArrayRef;
typedef struct YonaFloatArrayObject *YonaFloatArrayRef;
```

For each row below, let `Ref` and `Element` be the stated C types. The four
symbols have these exact prototypes; the repeated type descriptor must be the
closed immutable-array structural descriptor for that row:

```c
bool <Alloc>(const YonaAbiTypeDescriptor *ArrayType, int64_t Count,
             Ref *EmptyOutput, YonaControlOutcome *EmptyFailure);
bool <Length>(const YonaAbiTypeDescriptor *ArrayType, Ref BorrowedArray,
              int64_t *OutputLength,
              YonaControlOutcome *EmptyFailure);
bool <Get>(const YonaAbiTypeDescriptor *ArrayType, Ref BorrowedArray,
           int64_t Index, Element *OutputElement,
           YonaControlOutcome *EmptyFailure);
bool <PutMove>(const YonaAbiTypeDescriptor *ArrayType, Ref *OwnedArray,
               int64_t Index, Element Value, Ref *EmptyOutput,
               YonaControlOutcome *EmptyFailure);
```

| Kind | `Ref` | `Element` | `<Alloc>` | `<Length>` | `<Get>` | `<PutMove>` |
|---|---|---|---|---|---|---|
| Byte | `YonaByteArrayRef` | `int64_t` | `YonaRuntimeAbiByteArrayAllocZeroedV2` | `YonaRuntimeAbiByteArrayLengthV2` | `YonaRuntimeAbiByteArrayGetV2` | `YonaRuntimeAbiByteArrayPutMoveV2` |
| Int | `YonaIntArrayRef` | `int64_t` | `YonaRuntimeAbiIntArrayAllocZeroedV2` | `YonaRuntimeAbiIntArrayLengthV2` | `YonaRuntimeAbiIntArrayGetV2` | `YonaRuntimeAbiIntArrayPutMoveV2` |
| Float | `YonaFloatArrayRef` | `double` | `YonaRuntimeAbiFloatArrayAllocZeroedV2` | `YonaRuntimeAbiFloatArrayLengthV2` | `YonaRuntimeAbiFloatArrayGetV2` | `YonaRuntimeAbiFloatArrayPutMoveV2` |

Byte encoding adds one leaf only:

```c
bool YonaRuntimeAbiByteArrayFromStringV2(
    const YonaAbiTypeDescriptor *ByteArrayType,
    const YonaAbiValue *BorrowedString, YonaByteArrayRef *EmptyOutput,
    YonaControlOutcome *EmptyFailure);
```

The iterator bridge is likewise exact rather than an implied legacy alias.
Origin is not part of an `Iterator a` value's static source type, so
`Runtime/Stdlib/Iterator.h` deliberately separates the source-independent type
descriptor used by `next` from a concrete factory descriptor used only while
constructing an iterator. The bridge deliberately does not encode iterator
state as a reusable Yona callable: a source may be a linear resource, so
retaining a captured callable would be invalid and consuming it would make the
iterator one-shot. A factory instead owns an exact runtime-only state vtable,
invoked only while the outer linear Iterator has a verifier-proven scoped
unique borrow:

```c
typedef struct YonaAbiIteratorTypeDescriptor {
  uint32_t AbiVersion;
  uint32_t Reserved;
  uint64_t StructuralFingerprint;
  const uint8_t *CanonicalBytes;
  uint64_t CanonicalByteCount;
  const YonaAbiTypeDescriptor *ElementType;
  const YonaAbiTypeDescriptor *IteratorType;
  const YonaAbiTypeDescriptor *OptionElementType;
  YonaAbiResultOwnership ElementOwnership;
  uint32_t ReservedOwnership;
} YonaAbiIteratorTypeDescriptor;

typedef struct YonaAbiIteratorFactoryDescriptor
    YonaAbiIteratorFactoryDescriptor;
typedef bool (*YonaAbiIteratorAdvanceBorrowedUniqueFn)(
    const YonaAbiIteratorFactoryDescriptor *BorrowedFactory,
    void *BorrowedUniqueState, YonaAbiValue *EmptyOption,
    YonaControlOutcome *EmptyFailure);
typedef void (*YonaAbiIteratorDestroyStateFn)(
    const YonaAbiIteratorFactoryDescriptor *BorrowedFactory,
    void *OwnedState);
struct YonaAbiIteratorFactoryDescriptor {
  uint32_t AbiVersion;
  uint32_t Reserved;
  uint64_t StructuralFingerprint;
  const uint8_t *CanonicalBytes;
  uint64_t CanonicalByteCount;
  const YonaAbiIteratorTypeDescriptor *Iterator;
  const YonaAbiTypeDescriptor *SourceType;
  YonaAbiIteratorAdvanceBorrowedUniqueFn AdvanceBorrowedUnique;
  YonaAbiIteratorDestroyStateFn DestroyState;
};
```

Each canonical byte string encodes its complete ordered structural tuple except
pointers and callback addresses. Factory bytes include one stable
producer/adapter identity, the closed source and iterator type bytes, and the
transactional state contract. Every reserved field is zero and both callbacks
are non-null exact-prototype symbols selected by the authenticated RuntimeEntry
registry, never by source spelling. Each runtime iterator object stores its
exact factory pointer and one opaque mutable state allocation. That state owns
the moved source, including a linear FileHandle when applicable; it is never
retained, copied, exposed as a Yona value, or captured by a callable. `next`
validates only its source-independent expected type, proves the Iterator owner
has no competing loan, validates the object's factory against that type, and
invokes `AdvanceBorrowedUnique` under the scoped unique loan. It never guesses
a source kind from a parameter, branch, import, or runtime tag. Descriptor
validation compares canonical bytes and every nested descriptor before reading
state. It declares:

```c
bool YonaRuntimeAbiIteratorFromSequenceMoveV2(
    const YonaAbiIteratorFactoryDescriptor *Factory,
    YonaSequenceRef *OwnedSource, YonaAbiValue *EmptyIterator,
    YonaControlOutcome *EmptyFailure);
bool YonaRuntimeAbiIteratorFromByteArrayMoveV2(
    const YonaAbiIteratorFactoryDescriptor *Factory,
    YonaByteArrayRef *OwnedSource, YonaAbiValue *EmptyIterator,
    YonaControlOutcome *EmptyFailure);
bool YonaRuntimeAbiIteratorFromIntArrayMoveV2(
    const YonaAbiIteratorFactoryDescriptor *Factory,
    YonaIntArrayRef *OwnedSource, YonaAbiValue *EmptyIterator,
    YonaControlOutcome *EmptyFailure);
bool YonaRuntimeAbiIteratorFromFloatArrayMoveV2(
    const YonaAbiIteratorFactoryDescriptor *Factory,
    YonaFloatArrayRef *OwnedSource, YonaAbiValue *EmptyIterator,
    YonaControlOutcome *EmptyFailure);
bool YonaRuntimeAbiIteratorNextV2(
    const YonaAbiIteratorTypeDescriptor *ExpectedType,
    YonaAbiValue *BorrowedUniqueIterator, YonaAbiValue *EmptyOption,
    YonaControlOutcome *EmptyFailure);
void YonaRuntimeAbiIteratorReleaseV2(YonaAbiWord OwnedIterator);
```

String storage exposes only three callback-free UTF-8 primitives in
`Runtime/Stdlib/String.h`:

```c
bool YonaRuntimeAbiStringLengthV2(
    const YonaAbiTypeDescriptor *StringType,
    const YonaAbiValue *BorrowedString, int64_t *OutputScalarCount,
    YonaControlOutcome *EmptyFailure);
bool YonaRuntimeAbiStringCharAtV2(
    const YonaAbiTypeDescriptor *StringType,
    const YonaAbiValue *BorrowedString, int64_t ScalarIndex,
    int64_t *OutputUnicodeScalar, YonaControlOutcome *EmptyFailure);
bool YonaRuntimeAbiStringFromCharsMoveV2(
    const YonaAbiTypeDescriptor *StringType,
    const YonaAbiTypeDescriptor *CharSequenceType,
    YonaSequenceRef *OwnedChars, YonaAbiValue *EmptyString,
    YonaControlOutcome *EmptyFailure);
```

Length and CharAt validate strict UTF-8 and count/index Unicode scalar values,
never bytes; CharAt bounds or malformed storage is a reserved String-contract
diagnostic. FromChars validates every Unicode scalar and the exact `Seq Char`
descriptor, stages the complete UTF-8 allocation, then consumes the sequence
and publishes one Owned String. False leaves every owner/output unchanged.
These are storage primitives, not public `Std\String` functions; Task 15
implements traversal and combinators in Yona. Locale/Unicode case mapping,
where retained, is a checked direct native leaf in the closed stdlib manifest
rather than another storage intrinsic.

All five require full descriptor equivalence and pairwise storage disjointness.
A From entry allocates and initializes its complete opaque state before commit;
on success it moves/clears the source into one Owned iterator, while false
leaves source/output unchanged. Next takes a verifier-proven scoped unique
borrow of the mutable single-pass iterator. `AdvanceBorrowedUnique` stages the
exact `Option` before committing position, publishes one Owned Option on true,
and on false leaves state and output unchanged so an identical retry observes
the same element. Exhaustion is successful `None`, never false. Destroy calls
the factory's `DestroyState` exactly once; that callback releases the source
and all partial state without a Yona call, allocation, or failure path.

There is intentionally no ByteArray-to-String leaf or public
`Std\ByteArray.toString`: arbitrary bytes are not a Yona String. Strict decode
remains `Std\Convert.decodeUtf8 : ByteArray -> Result (String, ConvertError)`,
including malformed-byte rejection and embedded-NUL preservation, without a
ByteArray-to-Convert module dependency.

The compiler models these thirteen functions as private checked array
intrinsics, not as Functions or NativeExtern calls whose C signature would
hide the out-parameters. Add `ArrayStorageKind { Byte, Int, Float }` plus
`ArrayAllocZeroedInst`, `ArrayLengthInst`, `ArrayGetInst`,
`ArrayPutMoveInst`, and `ByteArrayFromStringInst` to
`CheckedRuntimePayload`. Extend syntax/semantics with a private-only
`intrinsic name : Type = array.<operation> <storage-kind>` declaration and a
array subset `SemanticIntrinsicKind::{ArrayAllocZeroed, ArrayLength, ArrayGet,
ArrayPutMove, ByteArrayFromString}`. Typechecking validates the exact
kind-specific structural signature, forbids export/import/first-class use,
and records the enum identity on each resolved call. AST lowering consumes
that call directly into the array instruction and emits no Function,
NativeSymbol, or declaration-only TIRF dependency. Runtime failure
normalization emits the matching checked terminator, and LLVM resolves the
enum/kind pair through Task 11's single RuntimeEntry registry; no pass
dispatches by source spelling. Alloc produces
one Owned array, Length/Get borrow and produce a Trivial scalar, PutMove
Consumes one array and produces one Owned array, and FromString borrows String
and produces one Owned ByteArray. All have a closed empty source effect row;
their `Failure` is the nonreturning compiler/runtime-contract edge and cannot
be observed as a language exception.

The lexer/parser accept this declaration only in compiler-owned stdlib source
mode, and semantic resolution additionally requires the reserved intrinsic
module identity; ordinary user modules cannot forge the spelling. Lexer, AST,
visitor, parser, semantic-model, and typechecker tests cover all five valid
forms plus wrong storage kind, signature, visibility, first-class use,
export/import, and user-source rejection. The visitor regression sends an
`IntrinsicDecl` through `AstVisitorImpl::dispatchVisit` as an `AstNode *`, not
only through a statically typed overload, and proves the dynamic dispatch
chain recognizes the new concrete node. The projected enum—not token text—is
the only fact AST lowering consumes.

`ParserConfig` gains `ParserSourceMode::{Ordinary, CompilerStdlib}` and defaults
to Ordinary; `SourceManager` records an immutable compiler-stdlib parse
authorization (manifest row identity plus canonical path) per source. Its
constructor requires an unforgeable internal capability and is not exposed by
ordinary source-loading APIs. Task 11's tests obtain that capability only
through `test/Support/CompilerStdlibSourceFixture.h`, compiled under
`BUILD_TESTING`; production code has no general test/provenance constructor.
No CLI flag, module-path entry, package declaration, or claimed module name can
set CompilerStdlib. Task 16's internal bootstrap/regenerator selects and
validates a canonical manifest path before opening bytes, then creates this
authorization and parses with CompilerStdlib mode. Immediately after parsing,
it checks the declared ModuleIdentity and exact intrinsic/resource inventory
against the selected row and issues a separate immutable
`VerifiedCompilerStdlibSource` seal; a mismatch discards the AST. The parser
requires parse authorization, while TypeChecker requires both that same
source authorization and the post-parse seal/reserved identity. Ordinary
`CompilerPipeline::compile` always uses Ordinary. Tests cover ordinary
rejection, forged Std module name/path/symlink rejection, a capability-parsed
wrong identity/inventory rejected before typechecking, and the canonical
bootstrap source.

Make both private-array and sequence-end operator resolution durable rather
than rediscovering it in Typed IR:

```cpp
namespace yona::semantics {
enum class SemanticIntrinsicKind : std::uint8_t {
  ArrayAllocZeroed = 0, ArrayLength = 1, ArrayGet = 2,
  ArrayPutMove = 3, ByteArrayFromString = 4,
  SetContains = 5, DictionaryContainsKey = 6,
  IteratorFromSequence = 7, IteratorFromByteArray = 8,
  IteratorFromIntArray = 9, IteratorFromFloatArray = 10,
  IteratorNext = 11, StringLength = 12, StringCharAt = 13,
  StringFromChars = 14
};
enum class ArrayIntrinsicStorageKind : std::uint8_t {
  Byte = 0, Int = 1, Float = 2
};
struct ArrayIntrinsicCallProjection {
  SemanticIntrinsicKind Kind;
  ArrayIntrinsicStorageKind Storage;
  compiler::typechecker::MonoTypePtr DeclaredFunctionType;
};
struct ProjectedArrayIntrinsicCall {
  SemanticIntrinsicKind Kind;
  ArrayIntrinsicStorageKind Storage;
  model::TypeId FunctionType;
};
struct KeyQueryIntrinsicCallProjection {
  SemanticIntrinsicKind Kind; // SetContains or DictionaryContainsKey
  compiler::typechecker::MonoTypePtr DeclaredFunctionType;
};
struct ProjectedKeyQueryIntrinsicCall {
  SemanticIntrinsicKind Kind;
  model::TypeId FunctionType;
};
struct IteratorIntrinsicCallProjection {
  SemanticIntrinsicKind Kind; // one of the five Iterator values
  compiler::typechecker::MonoTypePtr DeclaredFunctionType;
};
struct ProjectedIteratorIntrinsicCall {
  SemanticIntrinsicKind Kind;
  model::TypeId FunctionType;
};
struct StringIntrinsicCallProjection {
  SemanticIntrinsicKind Kind; // one of the three String values
  compiler::typechecker::MonoTypePtr DeclaredFunctionType;
};
struct ProjectedStringIntrinsicCall {
  SemanticIntrinsicKind Kind;
  model::TypeId FunctionType;
};
enum class SequenceMoveKind : std::uint8_t { Prepend = 0, Append = 1 };
struct SequenceMoveProjection {
  SequenceMoveKind Kind;
  compiler::typechecker::MonoTypePtr Element;
  compiler::typechecker::MonoTypePtr Sequence;
};
struct ProjectedSequenceMove {
  SequenceMoveKind Kind;
  model::TypeId Element;
  model::TypeId Sequence;
};
}
```

Task 11 extends `NodeSemanticProjection`, `ProjectedNodeFact`, and
`NodeSemantics` with optional `ArrayIntrinsicCall`, `KeyQueryIntrinsicCall`,
`IteratorIntrinsicCall`, `StringIntrinsicCall`, and `SequenceMove`
fields and adds
`arrayIntrinsicCallFor`, `keyQueryIntrinsicCallFor`,
`iteratorIntrinsicCallFor`, `stringIntrinsicCallFor`,
and `sequenceMoveFor`
accessors. The one batch
freeze projects their full types in the owning binder scope and rejects
missing, duplicate, foreign-node, wrong-arity, or overlapping facts
transactionally. Only a direct fully applied intrinsic call may carry the
first fact; identifier, partial, first-class, export, and import uses are
invalid. A typed `element :: sequence` BinaryExpr records Prepend and emits
`SequencePrependMoveInst`; `sequence :> element` records Append and emits
`SequenceAppendMoveInst`. AST lowering consumes each projected fact exactly
once and never branches on spelling or legacy `CType`. Tests round-trip,
remap, and roll back every fact and cover operand-order/type rejection.

The two key-query declarations are likewise direct/full-arity compiler-owned
intrinsics, legal only under the reserved `Std\Set`/`Std\Dict` module
identities with their well-scoped generic signatures. They lower directly to
the existing `PatternTestInst::{SetContains, DictionaryContainsKey}` query
paths: PRESENT/ABSENT becomes Bool and ERROR reaches the enclosing
`CheckedRuntimeOp` failure edge. They create no Function, NativeExtern, or
public C symbol. Tests enforce exact `set,key` / `dictionary,key` order,
generic binder ownership, private visibility, and all first-class/partial/
forged-module rejection.

The five iterator declarations use
`iterator.from.{sequence,byte-array,int-array,float-array}` and
`iterator.next` spellings and are legal only as private, direct, fully applied
intrinsics in canonical `Std\Iterator`. Their projected fact contains the
complete generic FunctionType; AST lowering emits `IteratorFromSourceInst`
with one interned factory ID or `IteratorNextInst` with one interned source-
independent type ID, never a
NativeExtern or symbol-name special case. The four From forms Consume their
source and return Owned `Iterator element`; Next borrows the iterator and
returns Owned `Option element`; because Iterator is linear, lowering turns that
borrow into a scoped unique loan for the duration of the checked operation and
rejects an alias or competing loan. Typechecker tests reject a wrong source/result
element, Borrowed From, consuming Next, export/import/partial use, and forged
provenance.

The three String declarations use `string.{length,char-at,from-chars}` and are
legal only as private, direct, fully applied intrinsics in canonical
`Std\String`. Their projected fact contains the complete FunctionType and AST
lowering emits `StringLengthInst`, `StringCharAtInst`, or
`StringFromCharsInst` as ordinary Canonical instructions; no NativeExtern or symbol
spelling survives. Length/CharAt Borrow String and return Trivial Int/Char;
FromChars Consumes `Seq Char` and returns Owned String. Tests reject byte-based
index/result types, a Borrowed FromChars source, a consuming query, wrong
effect row, export/import/partial use, and forged provenance.

Task 11 creates the single compiled RuntimeEntry registry in `yona_semantics`
before any Typed IR or LLVM lowering depends on it. This preserves the locked
`typed_ir -> semantics` edge and never makes semantics include Typed IR.
`RuntimeEntryRegistry.def` is a data-only X-macro table
with exactly the 23 rows available at this milestone: thirteen array entries,
two key-query entries, five Iterator entries, and three String entries. A row
contains the closed semantic intrinsic key (including storage kind where
applicable), reserved module identity, compiler-stdlib path, exact native
symbol, owner header/source, ABI archetype, and, for each Iterator producer,
its stable adapter identity plus exact `AdvanceBorrowedUnique` and
`DestroyState` callback symbols. `RuntimeEntryRegistry.h/.cpp` generate the
typed lookup table from that file; semantic admission, descriptor planning,
LLVM lowering, and ABI tests all consume it. No second switch, symbol-spelling
inference, or ad hoc callback map is allowed. The registry test enumerates the
closed semantic key product in both directions, rejects duplicate/missing
keys and symbols, verifies all exact C function-pointer types, and proves that
every emitted RuntimeEntry-backed intrinsic instruction resolves one row.

Task 15 makes `lib/stdlib-manifest.toml` the source authority, adds its two
File-iterator RuntimeEntry rows, and changes
`generate_stdlib_manifest.py --check` to regenerate and byte-compare this same
`.def` file. Thus Task 11 is independently compilable with one 23-row
registry, while Task 15 extends that exact registry to 25 rows rather than
introducing a second mapping.

The public Yona definitions document the valid input domains; invalid input is
a defined fatal array-contract violation, not a Yona Raised effect. The checked
C leaf owns that validation: negative/overflow lengths, out-of-bounds indices,
and Byte values outside `0..255` return false with
`YONA_ABI_FAILURE_ARRAY_CONTRACT`, which generated lowering reports and traps.
They never wrap, truncate, return a default, or trigger C undefined behavior.
Allocation failure uses the same defined fatal allocation path with
`YONA_ABI_FAILURE_OUT_OF_MEMORY`. On false, borrowed/scalar inputs and outputs
remain unchanged, move inputs remain owned and Empty outputs null, and exactly
one reserved diagnostic is written. PutMove
validates and allocates before commit; at commit `rc == 1` mutates in place,
while `rc > 1` copies then clears/releases exactly the consumed caller owner.
Every true path returns exactly one owner and leaves failure Empty.

Runtime and lowering tests cover all thirteen array and three String exact
function-pointer types,
cross-kind prototype rejection, descriptor mismatch, negative/overflow count,
bounds, Byte range, forced OOM, `rc == 1` and `rc > 1` PutMove, embedded NUL
encoding, and unchanged false poststates. Task 17 deletes every legacy array
map/fold/filter callback and Sequence bridge; no replacement array-storage
entry accepts a Function, Callable, Sequence bridge, `int64_t *`, or
`double *` carrier.
The same ABI-conformance suite assigns all five iterator symbols to their exact
function-pointer types and tests descriptor collision, each source kind,
managed/trivial elements, exhaustion, retry after injected failure, concurrent
borrowed Next serialization, source transfer, and early destruction.
It also assigns all three String symbols and covers empty/ASCII/multibyte/
embedded-NUL strings, malformed UTF-8, invalid scalar/surrogate, negative and
out-of-range scalar indices, OOM, source transfer, and unchanged false states.

Every handle-producing API requires a non-null output slot containing null on
entry, and every API above requires a non-null initially Empty
`EmptyFailure`. Every move/update output slot must be storage-distinct from every
owning input slot and every other output slot; overlap is rejected without
mutation or a failure write. After structural validation, descriptor or
allocation failure leaves every owning input unchanged and every output
null/Empty and writes the exact reserved nonallocating diagnostic. Success
clears every consumed
input slot before returning and stores exactly one caller-owned reference per
output while leaving `EmptyFailure` Empty. The failure slot is byte-distinct
from all inputs/outputs. Bulk Build arrays are null only for zero count; every
slot is an exact distinct owner, and dictionary key/value slots are mutually
distinct. Build validates every carrier, runs every fallible Hash/Eq callback,
allocates the complete shell/node topology, and computes the actual recursive
Shareable bit while inputs remain unchanged. One infallible commit then moves
or deliberately releases every input exactly once and publishes the handle.
No partially initialized handle is observable and no failure is possible after
the first input clears. A unique-owner in-place optimization may reuse the underlying object,
but never aliases C owner slots and begins mutation only after all validation
and fallible preparation has committed. `AggregateTakeFieldMove` validates the
field and empty, storage-distinct output before mutation. With a unique
aggregate it clears the owner, moves the selected field, releases every
unselected field exactly once, and destroys the shell. With `rc > 1`, it first
copies a Trivial selected field or retains a Shareable selected field, then
clears/releases only the caller's aggregate owner; the remaining owners keep
the unchanged shell and children. A non-Shareable selected field in a shared
aggregate is rejected without mutation (and the descriptor verifier normally
makes such sharing unconstructible).

Derive static child masks from the supplied descriptors, but compute and store
each aggregate/sequence/set/dictionary instance's atomic Shareable bit from
the actual staged children before publication. Path-copy
update/prepend/append/insert/put recomputes it from the resulting children
before commit; an in-place update
stores the new bit only with the otherwise-infallible commit. Duplicate set
Build/insertion releases the incoming duplicate at commit. Dictionary Build/replacement
releases the incoming duplicate key and old value; same-pointer replacement
still consumes the extra incoming owner exactly once. All allocations and
element arrays are zero-initialized and staged topology records refer only to
input indices until commit, so OOM or abandoned preparation never reads or
releases an input carrier. Build/create/update/prepend/append/insert/put obey the
all-or-nothing contract above. Runtime tests inject descriptor, callback, and
allocation failure at every field/element/entry position and require every
input byte unchanged, output null, and exactly one diagnostic; success and
duplicate-key cases prove every input clears exactly once.
Each set/dictionary stores its immutable process-lifetime
`YonaAbiKeyOperationsDescriptor`. Its canonical bytes include the complete key
type and the resolved Hash/Eq trait target applications, including ordered
type/effect arguments; callback addresses and the fingerprint are not semantic
identity. Both callbacks accept exact immutable Borrow operands, are generated
typed adapters, and are verifier-proven to have a closed operation-free,
nonraising, noncancelling source row and the exact Borrow/result ABI. Totality,
determinism, and Eq/Hash consistency are declared Hash/Eq trait laws, not
properties inferred from arbitrary Yona bodies. A lawful custom instance may
allocate internally; a checked allocation failure follows that target's
cleanup-and-trap invariant path and never returns a partial key callback
result. A false callback return denotes
an internal descriptor/adapter invariant failure, never key inequality.
Creation validates version/reserved fields, full key-type equivalence, bytes,
and non-null entries. Every insertion/query revalidates the stored descriptor
and the incoming key's full type before calling it. Hash selects a bucket only;
`EqualsBorrowed` decides equality. Cross-module custom aggregate-key tests use
equivalent independently emitted descriptors, and forced equal hashes with
unequal keys must remain distinct.
Borrowed
projection returns an internal pointer valid while the aggregate is alive;
owned projection clones only Shareable fields. Taking a field is the only
consuming projection and has one result, matching the single-result Typed IR
instruction model. Replacement lowering never calls legacy `*SetHeap`
functions.

Read/query APIs are the sole lowering surface for Task 6 pattern operations;
LLVM never reads a collection/aggregate header directly. `ABSENT` is the
ordinary semantic no-match result for bounds, missing key/field, or set
nonmembership and leaves every value output empty. `ERROR` is reserved for
ABI/storage/descriptor/equality-adapter failure; generated code releases its
diagnostic and follows `TrapCompilerFailure`. `PRESENT` initializes exactly
the documented output. Borrowed projection pointers remain valid only while
the source owner is live and unmutated. Owned projection clones only Trivial
or Shareable values; a non-Shareable field requires the consuming aggregate/
sequence take or slice-move path. `SliceOwned` shares only a recursively
Shareable persistent slice; `SliceMove` consumes the unique caller owner and
may detach/move linear nodes. Both validate bounds and allocate/path-copy
before commit. Record lookup compares the complete UTF-8 field label,
constructor tests compare the exact nominal descriptor plus returned tag, and
set/dictionary lookup uses hash only as a prefilter before the generated Eq
adapter. Forced collisions cannot produce a match. All scalar/output/failure
slots are non-null where required and byte-disjoint; structural rejection
changes nothing. Add empty/bounds/missing-key/field, forced hash collision,
shared Shareable slice, rejected shared-linear slice, unique linear slice,
Borrow/Owned/Take lifetime, and descriptor-corruption tests. Pattern lowering
must cover SequenceShape/Element/Slice, SetContains, DictionaryContainsKey/
Value, RecordHasField/projection, and ConstructorIs/field projection solely
through these replacement calls.

- [ ] **Step 5: Lower aggregate construction/projection/update and `typeOf`**

Extend `InstructionPayload` with:

```cpp
struct NominalConstructorRef {
  NominalTypeId Declaration;
  ConstructorId Constructor;
};
struct ConstructAggregateInst {
  model::TypeId Type;
  std::optional<NominalConstructorRef> Nominal;
  std::vector<ValueId> OwnedFields;
};
struct ProjectAggregateInst {
  ValueId Aggregate;
  std::uint32_t FieldIndex;
  ProjectionOwnership Ownership;
};
struct TakeAggregateFieldInst {
  ValueId OwnedAggregate;
  std::uint32_t FieldIndex;
};
struct UpdateAggregateInst {
  ValueId OwnedAggregate;
  std::uint32_t FieldIndex;
  ValueId OwnedReplacement;
};
struct DictionaryEntryOperand { ValueId OwnedKey; ValueId OwnedValue; };
struct BuildTypeValueRuntimeOp { model::TypeId ReflectedType; };
using KeyOperationsDescriptorId =
    StrongId<struct KeyOperationsDescriptorIdTag>;
struct KeyOperationsPlan {
  KeyOperationsDescriptorId Id;
  model::TypeId KeyType;
  model::TraitResolutionRequest Hash;
  model::TraitResolutionRequest Equals;
  std::optional<FunctionId> HashAdapter;
  std::optional<FunctionId> EqualsAdapter;
};
struct ConstructSequenceInst {
  model::TypeId Type;
  std::vector<ValueId> OwnedElements;
};
struct ConstructSetInst {
  model::TypeId Type;
  KeyOperationsDescriptorId ElementOperations;
  std::vector<ValueId> OwnedElements;
};
struct ConstructDictionaryInst {
  model::TypeId Type;
  KeyOperationsDescriptorId KeyOperations;
  std::vector<DictionaryEntryOperand> OwnedEntries;
};
struct SequenceAppendMoveInst {
  ValueId OwnedSequence;
  ValueId OwnedElement;
};
struct SequencePrependMoveInst {
  ValueId OwnedElement;
  ValueId OwnedSequence;
};
struct SetInsertMoveInst { ValueId OwnedSet; ValueId OwnedElement; };
struct DictionaryPutMoveInst {
  ValueId OwnedDictionary;
  ValueId OwnedKey;
  ValueId OwnedValue;
};
PassResult runKeyOperationsPreparation(Module &);
```

When `Nominal` is absent, `Type` must be a structural TupleType or RecordType
and fields must match its canonical element/field order. When present, `Type`
must be the matching instantiated NominalType and the declaration,
constructor tag, substituted field types, and arity must agree. All ADTs,
including nonrecursive ones, use the uniform heap path. Projection ownership
is explicit as borrow or shareable retain. `TakeAggregateFieldInst` consumes
one aggregate owner and returns the selected child as Owned/Trivial. It is
legal when that SSA owner has no later use; it does not assert runtime
uniqueness. The runtime moves/destroys on `rc == 1` and retains/copies the
selected child while releasing only this owner on `rc > 1`. A pattern needing
multiple fields uses Borrow/Retain projections and one final aggregate
release; ownership lowering may select Take only for a sole escaping field.
This avoids pretending that a single-result instruction can move an arbitrary
number of children.
`ProjectAggregateInst` reuses Task 6's `deriveProjectionValueSpec` without a
collection-specific provenance rule; its runtime pointer-validity check and
the Typed IR root-liveness proof must agree. Tests project through two nested
aggregate levels and prove the ultimate owner/parameter/static/unique-loan
root is preserved, then reject release or mutation of that root before the
final projected use.
Construction consumes every child; collection Move operations consume the
collection and element/key/value carriers and return one Owned collection.
Sequence/set/dictionary literals use their `Construct*` operations; generator
accumulation starts with an empty construction and uses Append/Insert/Put.
`ConstructAggregateInst`, `ConstructSequenceInst`, `ConstructSetInst`, and
`ConstructDictionaryInst` lower only to their matching transactional bulk
`*Build*Move` API. LLVM may not implement a whole construction as Create plus
committing per-element calls. The empty set/dictionary Create APIs are used
only for a verified zero-element construction or generator accumulator; later
single-element Move APIs remain transactional functional updates.
Updates call `YonaRuntimeAbiAggregateUpdateMove`; no LLVM
`CreateInsertValue` is legal for a runtime aggregate pointer.

`Module::KeyOperationsPlans` is a checked module-owned arena. AST lowering
creates each plan only from the source node's projected Hash/Eq evidence and
persists its ID on set/dictionary construction; no type-name lookup occurs in
Typed IR. Task 15 substitutes and resolves both requests through its one trait
resolver transaction, then interns plans only when key type and both complete
target applications are structurally equal. By `GenericPrepared`, Hash and Eq
must each hold resolved `TraitTargetApplication` evidence whose generated
adapter has exact Borrow inputs, closed direct Bool/Int result, and an empty
raise/cancel/operation row. Task 11 emits the immutable
`YonaAbiKeyOperationsDescriptor` from that plan. The verifier rejects a
missing/foreign plan ID, key-type mismatch, deferred evidence after generic
preparation, impure adapter, or set/dictionary operation that changes its
stored plan. Generic fragments serialize/remap both requests. Tests cover
local/imported/custom key instances, generic substitution selecting two
different instances, equivalent independently emitted descriptors, and a
forced hash collision resolved only by Eq.

`HashAdapter`/`EqualsAdapter` are empty in Canonical modules and serialized
generic fragments. `runKeyOperationsPreparation` is a named transactional
subpass of the Canonical-to-GenericPrepared transition after every request is
substituted, both trait targets are resolved/materialized, and before the
phase changes. It first canonicalizes/deduplicates plans by KeyType plus both
complete target applications, then reserves two FunctionIds per remaining
plan in plan-ID order, creates private non-generic/no-capture bodies that
DirectCall the exact resolved targets, fills both IDs, and verifies the whole
set before commit. Deduplication produces one total old-plan-ID-to-canonical-
plan-ID remap and transactionally rewrites every `ConstructSetInst`,
`ConstructDictionaryInst`, generator recipe, descriptor reference, and
module-arena reference before publication; no stale duplicate ID survives.
Task 6's temporary closed/no-generic fast path invokes the
same helper for already-resolved local targets; Task 15 owns deferred,
imported, and generic resolution. Destination TIRF import regenerates the
adapters after remapping requests rather than serializing source FunctionIds.

Those functions use `FunctionEntryKind::KeyHashAdapter` and
`KeyEqualsAdapter`. Their logical signatures are exactly
`Borrow Key -> Int` and `Borrow Key, Borrow Key -> Bool`; their LLVM entries
implement the exact `YonaAbiKeyHashEntry`/`YonaAbiKeyEqualsEntry` C prototypes,
validate and unpack each borrowed `YonaAbiValue`, write the scalar output only
on success, and return false with inputs/output unchanged on an invariant
failure. They are fixed-ABI roots: not first-class, imported/exported, or
captured; they receive no callable descriptor, universal adapter, execution
context, or boundary-context parameter. Their one DirectCall target is proven
closed, nonraising, noncancelling, and operation-free; after Task 13 this is
the only non-NativeExtern direct call outside CleanupDrop allowed to keep
`BoundaryContext = nullopt`, and LLVM passes a literal null to the target's
otherwise-uniform hidden boundary parameter. Any other call, any non-pure
target, or any attempt to forward a dynamic ambient context is rejected.
“Pure” here means the exact closed source effect row; it does not mean an
allocation-free opcode subset. Internal checked runtime failures terminate in
cleanup/trap within the DirectReturn target and do not add a callback outcome.
Closure conversion preserves them and its final
function-set snapshot includes them. Descriptor emission references only the
recorded IDs and cannot synthesize a late LLVM thunk. Parser/printer/remap,
phase/function-freeze, local/imported/generic target, forced-failure, and
cross-module descriptor tests cover both fields.

Add every operation to exhaustive operand visitors, canonical parser/printer,
phase verifier, ownership analysis, and LLVM/runtime lowering. Tests cover
tuple, record, nominal ADT, empty/nonempty sequence/set/dictionary, duplicate
set/dictionary keys, wrong field/element types, one-field take, retained-alias
take on Trivial/Shareable/non-Shareable children, multi-field pattern
projection, post-take use rejection, exact unique/shared child release counts,
and generator accumulation.
Add the now-implementable O0-O3 sentinel `Typed IR execution: currying
preserves aggregate results`; it must traverse the Task 10 ownership/cleanup
suffix and Task 11 aggregate BuildMove path.

Lower `typeOf` to `BuildTypeValueRuntimeOp`, whose exact result is the ordinary
Prelude `Type` ADT and whose reflected type must be closed. Add it to the
instruction payload, type/operand/remap/text visitors, phase allowlists, and
result verifier here; Task 12 later turns this explicitly fallible ordinary op
into a checked success/failure terminator. Its LLVM/runtime implementation
walks structural descriptors. Any textual nominal identity is copied with
`YonaRuntimeAllocateStringWithLength` and owned by its aggregate; never store
an LLVM global string address in a managed field.

- [ ] **Step 6: Lower comprehensions through owned cursors**

Define the `PatternCanonical` input form before the cursor operations:

```cpp
enum class GeneratorResultKind { Sequence, Set, Dictionary };
enum class GeneratorExecutionKind : std::uint8_t { Serial = 0, Parallel = 1 };
using TaskGroupPlanId = StrongId<struct TaskGroupPlanIdTag>;
struct GeneratorBindingPlan {
  MatchPlanId Match;
  ValueId Source;
  model::FunctionDeclarationIdentity CursorAdapter;
  std::optional<BlockId> GuardEntry;
  SourceRange Range;
};
struct GeneratorPlan {
  GeneratorId Id;
  FunctionId Function;
  GeneratorResultKind ResultKind;
  GeneratorExecutionKind Execution;
  std::vector<GeneratorBindingPlan> Bindings;
  BlockId BodyEntry;
  model::TypeId ElementType;
  std::optional<TaskGroupPlanId> GroupPlan;
  SourceRange Range;
};
struct GeneratorExprInst { GeneratorId Generator; };
```

Task 11 extends `Module` with module-owned `std::vector<GeneratorPlan>`, adds
checked builder lookup, and adds `GeneratorExprInst` to payload/operand
visitors, printer/parser, and phase verifier. AST lowering emits this exact
form from the semantic execution fact rather than the token spelling; the
verifier checks function-local values/blocks, Bool guards, pattern IDs,
execution kind, result/element types, and ranges. At Task 11's serial
milestone, `runGeneratorLowering` rejects a Parallel plan transactionally and
requires every `GroupPlan` to be empty; the no-parallel test-only phase path
remains explicit. Task 14 inserts
structured-concurrency planning and extends this same pass to lower Parallel
plans; no earlier pass is allowed to erase or silently serialize that marker.
`GeneratorLowered` forbids every `GeneratorExprInst` and plan reference.

Add the exact replacement cursor ABI. Descriptor/canonical storage and all
callback targets have immutable process lifetime:

```c
typedef struct YonaRuntimeAbiCursor *YonaCursorRef;
typedef struct YonaRuntimeAbiCursorStep *YonaCursorStepRef;
typedef struct YonaAbiCursorDescriptor YonaAbiCursorDescriptor;
typedef uint32_t YonaAbiCursorSourceShape;
enum {
  YONA_ABI_CURSOR_SEQUENCE = 0u, YONA_ABI_CURSOR_SET = 1u,
  YONA_ABI_CURSOR_DICTIONARY = 2u, YONA_ABI_CURSOR_ITERATOR = 3u,
  YONA_ABI_CURSOR_CUSTOM = 4u
};
typedef uint32_t YonaAbiCursorSourceAccess;
enum {
  YONA_ABI_CURSOR_SOURCE_PERSISTENT = 0u,
  YONA_ABI_CURSOR_SOURCE_LINEAR = 1u
};
typedef bool (*YonaAbiCursorInitializeFn)(
    const YonaAbiCursorDescriptor *BorrowedDescriptor,
    const YonaAbiValue *BorrowedSource, void **EmptyState,
    YonaControlOutcome *EmptyFailure);
typedef bool (*YonaAbiCursorAdvanceFn)(
    const YonaAbiCursorDescriptor *BorrowedDescriptor,
    const YonaAbiValue *BorrowedSource, void *MutableState,
    YonaAbiValue *EmptyElement, uint32_t *EmptyExhausted,
    YonaControlOutcome *EmptyFailure);
typedef void (*YonaAbiCursorDestroyStateFn)(
    const YonaAbiCursorDescriptor *BorrowedDescriptor, void *State);
struct YonaAbiCursorDescriptor {
  uint32_t AbiVersion;
  uint32_t Reserved;
  uint64_t StructuralFingerprint;
  const uint8_t *CanonicalBytes;
  uint64_t CanonicalByteCount;
  const YonaAbiTypeDescriptor *SourceType;
  const YonaAbiTypeDescriptor *ElementType;
  YonaAbiResultOwnership ElementContract;
  YonaAbiCursorSourceShape SourceShape;
  YonaAbiCursorSourceAccess SourceAccess;
  uint32_t Reserved2;
  YonaAbiCursorInitializeFn Initialize;
  YonaAbiCursorAdvanceFn Advance;
  YonaAbiCursorDestroyStateFn DestroyState;
};
bool YonaRuntimeAbiCursorDescriptorEquivalent(
    const YonaAbiCursorDescriptor *Left,
    const YonaAbiCursorDescriptor *Right);
bool YonaRuntimeAbiCursorOpenMove(
    const YonaAbiCursorDescriptor *Descriptor, YonaAbiValue *OwnedSource,
    YonaCursorRef *EmptyOutput, YonaControlOutcome *EmptyFailure);
bool YonaRuntimeAbiCursorNext(
    YonaCursorRef BorrowedUniqueCursor, YonaCursorStepRef *EmptyOutput,
    YonaControlOutcome *EmptyFailure);
bool YonaRuntimeAbiCursorStepExhausted(
    YonaCursorStepRef BorrowedStep, uint32_t *Output,
    YonaControlOutcome *EmptyFailure);
bool YonaRuntimeAbiCursorStepTakeValueMove(
    YonaCursorStepRef *OwnedStep, YonaAbiValue *EmptyOutput,
    YonaControlOutcome *EmptyFailure);
void YonaRuntimeAbiCursorReleaseMove(YonaCursorRef *OwnedCursor);
void YonaRuntimeAbiCursorStepReleaseMove(YonaCursorStepRef *OwnedStep);
```

Canonical descriptor bytes contain the resolved adapter declaration identity,
closed source/element types, normalized Trivial/Owned element contract, source
shape, and access mode; fingerprints/callback addresses are never semantic
identity. Version and both reserved fields are validated before any pointer is
followed. Cursor and step handles are managed, linear, and non-Shareable;
their `TryRetain` always fails. A cursor owns the moved source plus adapter
state and destruction calls `DestroyState` before releasing the source.
`OpenMove` allocates/initializes everything before moving the source. `Next`
uniquely mutably borrows the cursor, allocates a step first, then calls
`Advance`; false leaves the logical position unchanged, while true advances
exactly once and returns either `(Empty element, exhausted=1)` or one exact
element with `exhausted=0`. Adapter initialization/advance stages all fallible
work before logical-position commit; internal cache mutation is legal only if
a retry returns the identical next element. Releasing an unconsumed value step
releases its element. Taking consumes/clears the step and moves an Owned
element or copies a Trivial one. Borrowed element contracts are invalid.

Null/version/reserved/nonempty-output/overlap rejection changes nothing.
Descriptor/OOM/adapter-preparation failure leaves owners and logical position
unchanged and writes the exact reserved nonallocating diagnostic; success
leaves `EmptyFailure` Empty. All owner/output/failure ranges are pairwise
byte-disjoint. Release functions clear their owner slot before destruction
and accept an already-null slot. An adapter is legal only when its complete
contract is closed, operation-free, nonraising, and noncancelling. Therefore
source-visible iteration errors must be encoded in the element type (for
example `Result`) or wait for a future outcome-aware cursor; they cannot leak
through the ABI diagnostic. An Iterator-backed cursor adapter uses the same
scoped-unique state advance contract as `YonaRuntimeAbiIteratorNextV2`; it
never retains, applies, or reconstructs a source-level callable.

Add the module-owned descriptor plan and post-lowering cursor form:

```cpp
using CursorDescriptorId = StrongId<struct CursorDescriptorIdTag>;
enum class CursorSourceShape : std::uint8_t {
  Sequence = 0, Set = 1, Dictionary = 2, Iterator = 3, Custom = 4
};
enum class CursorSourceAccess : std::uint8_t {
  Persistent = 0, Linear = 1
};
struct CursorDescriptorPlan {
  CursorDescriptorId Id;
  model::FunctionDeclarationIdentity AdapterIdentity;
  model::TypeId SourceType;
  model::TypeId ElementType;
  model::ResultOwnership ElementContract;
  CursorSourceShape SourceShape;
  CursorSourceAccess SourceAccess;
  FunctionId InitializeAdapter;
  FunctionId AdvanceAdapter;
  FunctionId DestroyStateAdapter;
};
struct CursorOpenInst { CursorDescriptorId Descriptor; ValueId Source; };
struct CursorNextInst { ValueId Cursor; };
struct CursorValueInst { ValueId Step; };
struct CursorExhaustedInst { ValueId Step; };
enum class ArrayStorageKind : std::uint8_t { Byte = 0, Int = 1, Float = 2 };
struct ArrayAllocZeroedInst { ArrayStorageKind Kind; ValueId Count; };
struct ArrayLengthInst { ArrayStorageKind Kind; ValueId Array; };
struct ArrayGetInst { ArrayStorageKind Kind; ValueId Array; ValueId Index; };
struct ArrayPutMoveInst {
  ArrayStorageKind Kind;
  ValueId Array;
  ValueId Index;
  ValueId Element;
};
struct ByteArrayFromStringInst { ValueId String; };
struct StringLengthInst { ValueId String; };
struct StringCharAtInst { ValueId String; ValueId Index; };
struct StringFromCharsInst { ValueId Chars; };
using IteratorTypeDescriptorId =
    StrongId<struct IteratorTypeDescriptorIdTag>;
using IteratorFactoryDescriptorId =
    StrongId<struct IteratorFactoryDescriptorIdTag>;
enum class IteratorSourceKind : std::uint8_t {
  Sequence = 0, ByteArray = 1, IntArray = 2, FloatArray = 3
};
struct IteratorTypeDescriptorPlan {
  IteratorTypeDescriptorId Id;
  model::TypeId ElementType;
  model::TypeId IteratorType;
  model::TypeId OptionElementType;
  model::ResultOwnership ElementOwnership;
};
struct IteratorFactoryDescriptorPlan {
  IteratorFactoryDescriptorId Id;
  IteratorTypeDescriptorId Iterator;
  model::TypeId SourceType;
  model::FunctionDeclarationIdentity AdapterIdentity;
  std::string AdvanceBorrowedUniqueNativeSymbol;
  std::string DestroyStateNativeSymbol;
};
struct IteratorFromSourceInst {
  IteratorSourceKind Kind;
  IteratorFactoryDescriptorId Factory;
  ValueId Source;
};
struct IteratorNextInst {
  IteratorTypeDescriptorId ExpectedType;
  ValueId Iterator;
};

using CheckedRuntimePayload = std::variant<
    BuildTypeValueRuntimeOp, MakeMatchErrorInst,
    ConstructAggregateInst, ProjectAggregateInst, TakeAggregateFieldInst,
    UpdateAggregateInst, ConstructSequenceInst, ConstructSetInst,
    ConstructDictionaryInst, SequenceAppendMoveInst, SequencePrependMoveInst,
    SetInsertMoveInst,
    DictionaryPutMoveInst, PatternTestInst, PatternProjectInst,
    CursorOpenInst, CursorNextInst, CursorValueInst, CursorExhaustedInst,
    ArrayAllocZeroedInst, ArrayLengthInst, ArrayGetInst, ArrayPutMoveInst,
    ByteArrayFromStringInst, IteratorFromSourceInst, IteratorNextInst,
    StringLengthInst, StringCharAtInst, StringFromCharsInst,
    CheckedDirectNativeCallInst>;
struct CheckedRuntimeOp {
  CheckedRuntimePayload Operation;
  ProducedBranchTarget Success;
  RuntimeFailureDisposition Failure;
};
PassResult runRuntimeFailureNormalization(Module &);
```

`Module::IteratorTypeDescriptors` and
`Module::IteratorFactoryDescriptors` are separately interned deterministic
arenas. Type plans key only on element/Iterator/Option descriptors and element
ownership; factory plans key on one type-plan ID, source type, the stable
producer/adapter declaration identity, and two exact native callback symbols.
Those symbols come only from the authenticated RuntimeEntry registry and have
the exact state-vtable prototypes above; arbitrary source declarations cannot
supply callbacks. Open plans may exist only in a canonical generic fragment
and are structurally remapped during specialization;
executable normalization requires every referenced plan closed. Canonical
text, parser/printer, clone/remap, phase and ownership verification cover both
arenas. `LlvmModuleLowerer` emits their private constants in ID order, resolves
all nested type globals and the registry-authenticated exact-prototype callback
addresses, and validates the exact C layouts before lowering any body. A From
instruction references only a factory; Next references only a
source-independent type plan and its Iterator operand must carry a scoped
unique loan. Tests pass one `Iterator a`
through a parameter, import, phi/branch merge, and each factory origin and
require the same Next descriptor in every case; a forged source-specific Next
descriptor or callback symbol is unrepresentable.

Task 11 also adds every listed runtime-backed operation—including the three
String and five Iterator forms—to the ordinary `InstructionPayload` admitted
from Canonical through AcceleratorSelected. The final
`runRuntimeFailureNormalization` pass splits at each such instruction and
moves its payload into `CheckedRuntimeOp`; after successful normalization—and
at `ControlOutcomeLowered` and every later phase—verification rejects residual
ordinary forms. Operand/remap/parser/printer/phase
and LLVM tests cover all three String shapes and ensure the Task 13 alias
extension retains, rather than drops, the Iterator and String alternatives.

`CursorOpenInst` consumes a prepared source owner and returns the descriptor's
Owned CursorType; `CursorNextInst` uniquely borrows that cursor and returns an
Owned CursorStepType; `CursorValueInst` consumes the step, clears its element
slot, and moves an Owned managed element (or copies a Trivial element) to the
result; `CursorExhaustedInst` borrows the step and returns trivial Bool. A step
owns any unextracted managed element and releases it on destruction, so no
element can borrow storage from a consumed step. Add all four to
`InstructionPayload` and its operand visitor. Add the five raw array forms and
both iterator-intrinsic forms to that ordinary payload as well: they are
admitted from Canonical through
AcceleratorSelected, round-trip in canonical TIRF/text/remap visitors, and
exist as raw operations only until runtime-failure normalization replaces each
with its `CheckedRuntimeOp`. `ControlOutcomeLowered` and every later phase
forbid raw array/iterator forms while retaining the seven variants inside
`CheckedRuntimePayload`. Parser/printer/remap/phase tests prove both the raw
round trip and mandatory erasure. This task's
`runRuntimeFailureNormalization` turns every aggregate/collection/query/cursor
form above into a `CheckedRuntimeOp` before any focused execution path enters
representation, ownership, cleanup, or LLVM; success values exist only as
successor block arguments and false leaves every input owner unchanged before
`TrapCompilerFailure`. The normalizer is phase-neutral in Task 11 so the
remaining semantic control forms can still be processed, but its exhaustive
classifier rejects an unlisted fallible runtime operation. Task 12 extends the
same closed variant and makes normalization mandatory for the whole
`AcceleratorSelected -> ControlOutcomeLowered` transition;
cursor diagnostics are never source exceptions.

`CheckedRuntimeOp` is a `Terminator` variant arm, never an instruction. The
normalizer splits the block, removes the raw result-producing instruction, and
installs this terminator in the original block. Its Success edge defines the
former result as a produced block-argument prefix and its Failure edge defines
none. Exhaustive successor/operand visitors, CFG/dominance, parser/printer,
clone/remap, phase, ownership, cleanup, and LLVM lowering all handle the arm;
Task 12 reuses that same terminator while extending only its closed payload
variant.

`runGeneratorLowering` accepts `PatternCanonical`, produces
`GeneratorLowered`, and converts comprehensions into explicit CFG loops before
decision/control-flow and closure lowering.
After `CursorValueInst`, it calls the shared `lowerDecisionRoot` with the
binding plan's canonical root, the cursor item as its sole concrete input,
and `RaiseMatchErrorFailure{}`; a failed row follows the ordinary ranged
MatchError/cleanup policy, and no generator-local pattern compiler exists. It
consumes every guard yield owned
by that generator plan. Ordinary case/catch `DecisionSwitch` terminators and
detached handler guard yields remain legal in `GeneratorLowered` for the
subsequent owning passes.
Opening a cursor consumes an independently retained source owner; next borrows
the cursor. Shareable persistent sources clone one owner before opening;
consuming/linear iterators move elements from their owned source and cannot be
reused under another name. The semantic checker rejects attempted reuse of a
non-Shareable named source. A shared persistent source may only copy a Trivial
element or retain a Shareable one; a unique source may move destructively. A
dictionary cursor's element is the exact structural key/value tuple. The
verifier requires `GeneratorBindingPlan::CursorAdapter` to be persisted
resolved iterable evidence, interns exactly one matching descriptor plan
after generic specialization, and never chooses behavior from source type or
spelling. Value extraction must be dominated by the non-exhausted branch for
that exact step, and Next's cursor operand is a unique mutable loan. Descriptor
plans/adapter identities/functions participate in canonical text,
generic-fragment remap, verification, and function freeze. Cursor/step cleanup
is registered immediately. This task proves
exhaustion, direct return, failed match, and early-drop edges; Tasks 12-14 add
and test Raised, Performed, and Cancelled exits before ownership/cleanup is
rerun. ABI-distinct replacement iterator/file entry points store only the
exact factory pointer plus opaque owned state and use the state-vtable
callbacks above. Leave the old raw closure entry points byte-for-byte
compatible for the oracle until Task 17.

`CursorTest.cpp` covers transactional Open OOM/initializer failure; Next
allocation/adapter failure followed by an identical retry; sequence/set/
dictionary/iterator/file adapters; descriptor hash collision with unequal
canonical adapter bytes; Trivial Float bit preservation; Owned element move;
unextracted-step release; exhausted-step rejection; early drop ordering
(state, then source); Borrowed-contract rejection; null/overlap/nonempty output;
persistent named-source reuse; moved-linear-source rejection; and dictionary
tuple typing. Generator/ownership tests add failed-match, Raised, Performed,
Cancelled, and early-return cleanup.

Task 14 changes the final input phase to `AsyncPlanned`. Its async planner
turns semantic independent-let and Parallel-generator facts into deterministic
module-owned task-group plans; generator lowering then outlines each Parallel
element body, applies the same canonical match root inside that worker, and
emits explicit task submission/join/extraction IR while it owns the cursor
loop. Serial plans continue through the path above. This ordering is
normative: parallel syntax is still present when concurrency is planned, and
all generator markers are gone before control-flow lowering.

- [ ] **Step 7: Run collection/generator execution and sanitizer gates**

```bash
python3 scripts/run-focused-tests.py ./out/build/x64-debug-linux/tests \
  -tc='Typed IR collections*,Typed IR generators*,HamtRc*'
python3 scripts/run-focused-tests.py ./out/build/x64-debug-linux/tests \
  -ts='RuntimeGuards' -tc='*iterator*'
python3 scripts/run-focused-tests.py ./out/build/x64-debug-linux/tests -tc='Typed IR execution*ownership*'
python3 scripts/quality.py --build-dir out/build/x64-debug-linux \
  --preset x64-debug-linux --jobs 2 sanitize
git diff --check
```

Expected: replacement and same-pointer cases release exactly once, named
sources remain unchanged, every fallible Task 11 operation is already a
checked terminator before LLVM, and every fixture balances allocation tags.

- [ ] **Step 8: Commit aggregates and collections**

```bash
git add include/yona/Runtime/Collections src/Runtime/Collections \
  include/yona/Runtime/Stdlib/Iterator.h \
  include/yona/Runtime/Stdlib/String.h \
  src/Runtime/Core/Internal.h src/Runtime/Core/Runtime.c \
  src/Runtime/Stdlib/Iterator.c src/Runtime/Stdlib/String.c \
  src/Runtime/Stdlib/Native.c \
  src/Runtime/Platform/FileLinux.c src/Runtime/Platform/FileMacOs.c \
  src/Runtime/Platform/FileWindows.c \
  include/yona/Syntax/Ast.h include/yona/Syntax/AstVisitor.h \
  include/yona/Syntax/AstVisitorImpl.h \
  include/yona/Syntax/Lexer.h include/yona/Syntax/Parser.h \
  include/yona/Support/SourceManager.h src/Support/SourceManager.cpp \
  src/Syntax/Ast.cpp src/Syntax/Lexer.cpp src/Syntax/Parser.cpp \
  src/Syntax/ParserModule.cpp src/Syntax/ParserImpl.h \
  include/yona/Semantics/SemanticModel.h \
  include/yona/Semantics/TypeChecker.h src/Semantics/SemanticModel.cpp \
  src/Semantics/TypeChecker.cpp \
  include/yona/Semantics/RuntimeEntryRegistry.h \
  include/yona/Semantics/RuntimeEntryRegistry.def \
  src/Semantics/RuntimeEntryRegistry.cpp \
  test/Semantics/RuntimeEntryRegistryTest.cpp \
  test/Syntax/AstTest.cpp \
  test/Syntax/LexerTest.cpp test/Semantics/SemanticModelTest.cpp \
  test/Semantics/TypeCheckerTest.cpp \
  test/Support/CompilerStdlibSourceFixture.h \
  include/yona/TypedIr src/TypedIr src/Codegen/Llvm \
  test/TypedIr test/Codegen/CollectionLoweringTest.cpp \
  test/Codegen/PatternOwnershipTest.cpp test/Codegen/TypedIrExecutionTest.cpp \
  test/Runtime/HamtRcTest.cpp test/Runtime/RuntimeGuardsTest.cpp \
  test/Runtime/CursorTest.cpp test/Runtime/StringStorageTest.cpp \
  test/Fixtures/TypedIr/Ownership cmake/YonaComponents.cmake CMakeLists.txt
git add docs/superpowers/plans/2026-09-01-typed-ir-codegen-rewrite.md
git commit -m "feat: lower owned aggregates and collections"
```

### Task 12: Replace generated-program unwinding with explicit outcomes

**Files:**

- Modify: `include/yona/TypedIr/Control.h`
- Create: `include/yona/TypedIr/Passes/ControlOutcomeLowering.h`
- Create: `src/TypedIr/Passes/ControlOutcomeLowering.cpp`
- Modify: `include/yona/TypedIr/Passes/RuntimeFailureNormalization.h`
- Modify: `src/TypedIr/Passes/RuntimeFailureNormalization.cpp`
- Modify: `test/TypedIr/RuntimeFailureNormalizationTest.cpp`
- Create: `include/yona/TypedIr/Passes/TailCallLowering.h`
- Create: `src/TypedIr/Passes/TailCallLowering.cpp`
- Create: `test/TypedIr/ControlOutcomeLoweringTest.cpp`
- Create: `test/TypedIr/TailCallLoweringTest.cpp`
- Create: `test/Codegen/ControlOutcomeLoweringTest.cpp`
- Create: `test/CMake/typed_ir_no_sjlj_contract.py`
- Modify: `test/TypedIr/CleanupVerifierTest.cpp`
- Modify: `include/yona/Semantics/SemanticModel.h`
- Modify: `include/yona/Semantics/TypeChecker.h`
- Modify: `include/yona/Semantics/RuntimeEntryRegistry.h`
- Modify: `src/Semantics/SemanticModel.cpp`
- Modify: `src/Semantics/TypeChecker.cpp`
- Modify: `test/Semantics/SemanticModelTest.cpp`
- Modify: `test/Semantics/TypeCheckerTest.cpp`
- Modify: `test/Semantics/RuntimeEntryRegistryTest.cpp`
- Modify: `test/Support/CompilerStdlibSourceFixture.h`
- Create: `test/Fixtures/TypedIr/Control/exception_nullary.yona`
- Create: `test/Fixtures/TypedIr/Control/exception_nullary.expected`
- Create: `test/Fixtures/TypedIr/Control/exception_multifield.yona`
- Create: `test/Fixtures/TypedIr/Control/exception_multifield.expected`
- Create: `test/Fixtures/TypedIr/Control/with_raise.yona`
- Create: `test/Fixtures/TypedIr/Control/with_raise.expected`
- Create: `test/Fixtures/TypedIr/Control/non_exhaustive_case.yona`
- Create: `test/Fixtures/TypedIr/Control/non_exhaustive_case.expected`
- Create: `test/Fixtures/TypedIr/Control/seq_generator_early_raise.yona`
- Create: `test/Fixtures/TypedIr/Control/seq_generator_early_raise.expected`
- Modify: `src/TypedIr/AstLowering.cpp`
- Modify: `include/yona/TypedIr/Instruction.h`
- Modify: `include/yona/TypedIr/TypedIr.h`
- Modify: `include/yona/TypedIr/Callable.h`
- Modify: `src/TypedIr/Verifier.cpp`
- Modify: `src/TypedIr/Printer.cpp`
- Modify: `src/TypedIr/Parser.cpp`
- Modify: `test/TypedIr/PrinterParserTest.cpp`
- Modify: `src/TypedIr/Pipeline.cpp`
- Modify: `include/yona/TypedIr/Analysis/OwnershipAnalysis.h`
- Modify: `src/TypedIr/Analysis/OwnershipAnalysis.cpp`
- Modify: `src/TypedIr/Passes/CleanupLowering.cpp`
- Modify: `src/TypedIr/Verification/OwnershipVerifier.cpp`
- Modify: `src/TypedIr/Verification/CleanupVerifier.cpp`
- Modify: `src/Codegen/Llvm/FunctionLowerer.cpp`
- Modify: `src/Codegen/Llvm/BlockLowerer.cpp`
- Modify: `src/Codegen/Llvm/CallableLowering.cpp`
- Modify: `include/yona/Runtime/Core/Callable.h`
- Modify: `test/Codegen/PatternOwnershipTest.cpp`
- Modify: `cmake/YonaComponents.cmake`
- Modify: `CMakeLists.txt`

**Interfaces:**

- Consumes: complete nominal exception values, Task 6 decision trees, Task 10
  cleanup regions, and Task 7 outcomes.
- Produces: `FunctionControlMode`, explicit outcome-returning CFG,
  whole-value catch matching, all-outcome `with` finalization, and path-local
  self-tail-call loops.
- Invariant: new generated programs never declare or call SJLJ, TLS exception,
  or fixed-size unwind-frame APIs.

- [ ] **Step 1: Write red control-outcome and TCO tests**

Cover nullary, Int, String/heap, two-field, and nominally distinct exceptions;
nested rethrow; unmatched catch; runtime failed match; normal/raise cleanup;
more than sixteen owned locals; mixed tail/base branches; and dynamic
`InvokeOutcome`'s distinct false-before-commit versus true-reserved-diagnostic
cleanup states. The latter keeps an unrelated linear owner live, proves the
callable/Consume arguments are released exactly once on both paths, and proves
a direct FunctionId invoke has neither internal failure disposition:

```cpp
TEST_CASE("Control outcome lowering transfers a complete exception ADT") {
  auto Module = lowerToClosureConverted(R"(
module Main
type Failure = PairError Int String
main = try
  raise PairError 4 "bad"
catch PairError code text -> (code, text)
end
)");
  REQUIRE(runControlOutcomeLowering(Module).has_value());
  CHECK(printModule(Module).find("outcome.raised %pair_error") !=
        std::string::npos);
  CHECK(printModule(Module).find("exception.field0.string") ==
        std::string::npos);
}

TEST_CASE("Typed IR tail calls: cleanup is edge-local") {
  auto Module = makeMixedTailAndReturnFunction();
  REQUIRE(runTailCallLowering(Module).has_value());
  CHECK(hasLoopBackedge(Module));
  REQUIRE(runControlAndOwnershipSuffix(Module).has_value());
  CHECK(releasesBeforeReturn(Module));
  CHECK(transfersBeforeBackedge(Module));
}
```

`PassResult runControlAndOwnershipSuffix(Module &)` is a test-local
composition helper, not a production pipeline shortcut. It first calls
`advanceNoAcceleratorCandidatesToAcceleratorSelected`, which verifies that
the focused fixture contains no accelerator candidate and performs the
otherwise-no-op phase transition, then runs `runControlOutcomeLowering`,
`runRepresentationSelection`, `runOwnershipLowering`, and
`runCleanupLowering` in that order. Each intermediate phase verifier must
pass; the helper stops at the first failed pass and returns that pass's full
`std::vector<PassDiagnostic>`. This keeps the Task 12 test
independent of Task 16's real accelerator selector while exercising the exact
production suffix.
The helper has the exact test-only signature
`PassResult advanceNoAcceleratorCandidatesToAcceleratorSelected(Module &)`: it
requires `TailCallsLowered`, exhaustively visits the instruction payload, and
changes only the phase after proving no accelerator-related placeholder/op is
present. At the Task 12 commit the closed payload variant itself proves this;
Task 16 updates the same test helper to reject
`ArrayOperationCandidateInst`, `AcceleratorOp`, and `CpuArrayOp` explicitly.
Wrong phase or any candidate returns diagnostics without mutation.
Create top-level sentinels `Typed IR control outcomes: with finalizer runs for
every control outcome` and `Typed IR execution: control outcomes preserve
exception payloads`; the shown `Control outcome lowering...` and `Typed IR
tail calls...` cases cover the other Task 12 prefixes.

- [ ] **Step 2: Run and observe missing outcome lowering**

```bash
cmake --build --preset build-debug-linux --target tests -j2
python3 scripts/run-focused-tests.py ./out/build/x64-debug-linux/tests \
  -tc='Typed IR control outcomes*,Typed IR tail calls*,Control outcome lowering*'
```

Expected: compilation fails for `ControlOutcomeLowering.h`.

- [ ] **Step 3: Select direct versus explicit-outcome function control**

Define:

```cpp
enum class FunctionControlMode { DirectReturn, ExplicitOutcome };
enum class ControlOutcomeKind { Success, Raised, Performed, Cancelled };
using OutcomeCallee = std::variant<FunctionId, ValueId>;
struct InvokeOutcome {
  OutcomeCallee Callee;
  std::vector<ValueId> Arguments;
  std::optional<ValueId> CallableEnvironment; // FunctionId recursive intra-SCC only
  std::optional<ValueId> BoundaryContext;
  std::optional<RuntimeFailureDisposition> Failure;
  std::optional<RuntimeFailureDisposition> ConsumedFailure;
  ProducedBranchTarget Success;
  ProducedBranchTarget Raised;
  ProducedBranchTarget Performed;
  BranchTarget Cancelled;
};
struct RouteInnerOutcome {
  ProducedBranchTarget Success;
  ProducedBranchTarget Raised;
  ProducedBranchTarget Performed;
  BranchTarget Cancelled;
  RuntimeFailureDisposition Failure; // malformed hidden inputs remain unread
};
struct ReturnSuccess { std::optional<ValueId> Result; };
struct ReturnRaised { ValueId Exception; };
struct ReturnPerformed { ValueId EffectRequest; };
struct ReturnCancelled {};
enum class CancellationPointKind { ExplicitCheck };
struct CancellationPointInst { CancellationPointKind Kind; };
struct CancellationRequestedInst { ValueId ExecutionContext; };
struct AllocateStringRuntimeOp { std::string Utf8; };
// Replaces Task 11's alias with the complete pre-async/effect payload set.
using CheckedRuntimePayload = std::variant<
    AllocateStringRuntimeOp, BuildTypeValueRuntimeOp, MakeMatchErrorInst,
    MakeFunctionInst, MakeClosureInst,
    ConstructAggregateInst, ProjectAggregateInst, TakeAggregateFieldInst,
    UpdateAggregateInst, ConstructSequenceInst, ConstructSetInst,
    ConstructDictionaryInst, SequenceAppendMoveInst, SequencePrependMoveInst,
    SetInsertMoveInst,
    DictionaryPutMoveInst, PatternTestInst, PatternProjectInst,
    CursorOpenInst, CursorNextInst,
    CursorValueInst, CursorExhaustedInst,
    ArrayAllocZeroedInst, ArrayLengthInst, ArrayGetInst, ArrayPutMoveInst,
    ByteArrayFromStringInst, IteratorFromSourceInst, IteratorNextInst,
    StringLengthInst, StringCharAtInst, StringFromCharsInst,
    CheckedDirectNativeCallInst>;
// CheckedRuntimeOp itself is the Task 11 record and is not redefined.
```

The already-normalized CheckedDirect alternative remains in this exhaustive
payload through LlvmReady. Task 12 adds a printer/parser/phase regression that
carries one such operation across the alias expansion and proves its exact
descriptor, operands, success edge, and false-unchanged failure edge survive.

A function whose solved effect row has no operation, is closed, and has
`MayRaise == false` and `MayCancel == false` keeps its direct typed result.
Open/unknown/native rows are conservative. Otherwise its ABI returns `void` and appends a
non-null `const YonaExecutionContext *context` followed by a final
`YonaControlOutcome *out` parameter after its declared parameters. Adapters
for universal callables always use explicit outcomes and the same context
position. The fixed KeyHash/KeyEquals callbacks below and cleanup roots are
the only adapter/root ABI exceptions. An
`OutcomeRouter` is always explicit-outcome and receives the same context;
`CleanupDrop` keeps its dedicated infallible `void` callback ABI but also
receives the context. The pass creates and dominates each special entry's
`ExecutionContextParameter` explicitly instead of applying the empty-row
direct-return rule to it. Store
`FunctionControlMode` on both
`Function` and `CallableDescriptor`.

`KeyHashAdapter` and `KeyEqualsAdapter` are always `DirectReturn` logically,
but EntryKind overrides their physical LLVM ABI with the fixed C callback
prototypes from Task 11. Their prologue validates/unpacks borrowed
`YonaAbiValue` arguments, the logical body DirectCalls its resolved pure Yona
target with a literal-null boundary context, and success alone stores the
`uint64_t` hash or normalized `uint32_t` Bool before returning true. Pre-body
validation returns false with inputs and output unchanged. They have no
ExecutionContextParameter, outcome, cleanup region, or source-visible failure
edge; verifier and ABI tests assert both the logical mode and physical shape.

Compiler/runtime ABI failures are not implicit language effects. Descriptor,
alias/storage, null-owner, impossible non-callable, and compiler-internal
allocation failures in closure/state/request/frame construction take a
generated cleanup edge and end in non-returning `llvm.trap`; they never create
or propagate `Raised(AbiFailure)` through source CFG. The C APIs still write a
nonallocating ABI-failure outcome for runtime unit tests, embedding hosts, and
diagnostics, but generated lowering never forwards a diagnostic accompanying
`false`. A source-visible fallible operation instead returns Raised through
its ordinary typed outcome after the ABI call itself succeeds. Submission and
IO/GPU prepublication false results are internal failures and cleanup-trap;
declared backend errors are published/returned as normal typed outcomes. The
control/effect verifier checks every runtime false successor is a cleanup trap, so a
`DirectReturn` function can contain allocating implementation details without
acquiring an undeclared outcome ABI.

Task 11 introduced the checked record and normalizer for its early execution
milestone. As the final subpass of `runControlOutcomeLowering`, the extended
`runRuntimeFailureNormalization` splits every remaining ordinary fallible runtime-backed
operation and replaces it with `CheckedRuntimeOp`. The success prefix contains
the former instruction result (if any), remapped to target block arguments;
false precommit leaves all input owners unchanged. Every compiler-internal
operation in the variant above has `TrapCompilerFailure`.
`RetainInst` is the sole deliberately late fallible ownership pseudo-op: this
normalizer leaves it untouched because representation selection has not yet
chosen its carrier. Task 10's later `runOwnershipLowering` is its only legal
consumer and replaces it with `TryRetainRuntime`; the control-outcome
normalizer must neither wrap it in `CheckedRuntimeOp` nor permit it to reach
`OwnershipLowered`.
Both Borrow and Owned `ProjectAggregateInst` forms enter the variant because
corrupt runtime storage can still return an internal error; only the success
ownership differs. `PatternTestInst` and
`PatternProjectInst` enter it exactly for Task 11's aggregate/collection query
surface: their produced prefix represents PRESENT/ABSENT as the typed Bool or
projection result, while `ERROR` follows Failure. Primitive numeric/symbol/
string tests proven C-infallible remain ordinary instructions. Callable application and the
already checked recursive-group creation, cleanup/effect/async prepared
terminators use their dedicated
outcome/terminator forms but obey the same produced-prefix and
failure-disposition rules. Runtime adapters stage validation, allocation,
clones, and output storage before committing any Consume input; compound
aggregate/collection construction stages all children and makes post-commit
initialization infallible. On false, generated code releases the reserved
diagnostic, traverses the declared cleanup target with the unchanged owners,
and traps. A source-visible runtime error is instead a true-returning ABI call
whose normal `YonaControlOutcome` is Raised with the exact declared nominal
exception; runtime-false never enters source control flow.

`ControlOutcomeLowered` rejects any allocating String literal, `typeOf` value,
`MakeMatchErrorInst`, ordinary or recursive-group callable construction,
fallible aggregate/collection/
cursor operation, or other bool-returning runtime primitive that remains an
ordinary instruction, including a query-backed pattern test/projection; the runtime-operation classification visitor is
exhaustive and has no default. It also rejects a checked internal operation
without `TrapCompilerFailure`, a success-prefix mismatch, or a false path that
can observe a partially committed owner. A callable `InvokeOutcome` is invalid
unless its unchanged-input and consumed-input failure dispositions are both
present, distinct in ownership state, and trap after exact cleanup; a direct
invoke is invalid if either is present. Forced-OOM tests keep an unrelated
Owned value live across string, MatchError, `typeOf`, closure, aggregate,
collection, and cursor construction and require exact cleanup before the
invariant trap. Query forced-error cases keep the same unrelated owner live
and distinguish semantic ABSENT from internal ERROR. This normalization is part of the existing
`AcceleratorSelected -> ControlOutcomeLowered` transition, not a new optional
phase or an LLVM-only branch.

Every generated call between explicit-outcome functions forwards the context
unchanged. Functions whose solved row has `MayCancel` emit queries only at the
explicit `CancellationPointInst`s recorded by the frontend; a true
query produces `ReturnCancelled` through the normal cleanup path. Pure direct
functions need no hidden context. The verifier rejects a cancellation point
without a dominating context parameter and rejects TLS/current-task reads.
Task 12 appends `SemanticIntrinsicKind::CancellationCheck = 15` and adds a
typed raw/projected `CancellationCheckIntrinsicCall` node fact containing the
exact closed `Unit -> Unit ! {Cancel}` FunctionType. The compiler-stdlib test
fixture declares the private intrinsic used by
`Std\Task.checkCancellation`; TypeChecker accepts only its authenticated
compiler-stdlib declaration and a direct full application, then records that
fact in the same atomic semantic batch. AST lowering consumes only that fact
into `CancellationPointInst`; it never recognizes source spelling, emits a
FunctionId, or looks up a C symbol. Identifier, partial, first-class, export,
import, wrong-row, and ordinary-user `intrinsic` uses are ranged errors.
For Task 12, the `BUILD_TESTING` `CompilerStdlibSourceFixture` supplies the
complete authenticated in-memory `Std\Task` module, including that private
declaration and the exported public `checkCancellation` wrapper; Task 15
replaces the fixture source with the canonical on-disk module.
`RuntimeEntryRegistryTest` proves value 15 has no RuntimeEntry row.
Task 14's dedicated
worker/task/group/channel C entries poll their forwarded context internally
and return their specified Cancelled outcome; async lowering never wraps one
committing ABI call in synthetic before/after points. There are no implicit
arbitrary loop-backedge polls: a CPU loop is cooperatively
cancellable only when its source/body contains an explicit check or another
declared point. Add an execution test importing public
`Std\Task.checkCancellation` inside a running loop, semantic rollback and
provenance tests, and a verifier test rejecting a point in a row that lacks
`MayCancel`.

`InvokeOutcome` is a terminator, not a dynamically typed instruction. Its
Success successor receives the statically declared result (unless Unit), its
Raised successor receives the builtin existential `ExceptionValue`, its
Performed successor receives the builtin `EffectRequest` reference, and its
Cancelled successor receives no implicit argument; explicit threaded
arguments still follow normal `BranchTarget` checking. Add the four typed
return terminators and `InvokeOutcome` to the terminator visitor. No generic
`OutcomePayloadInst` exists.
For a direct FunctionId callee, both failure dispositions are absent because
the typed entry returns no structural bool and cannot produce the reserved C
diagnostic. For a callable ValueId, both are mandatory: a false structural
return follows `Failure` with the callable and Consume arguments unchanged,
while a true callee-owns return carrying reserved `AbiFailure` releases that
diagnostic and follows `ConsumedFailure` with the callable and every Consume
argument already cleared. Only the four declared source outcomes reach the
produced successors. Ownership and cleanup verification check those two
different post-states; LLVM may not merge them before cleanup.
`RouteInnerOutcome` is the dedicated entry terminator of a
`FunctionEntryKind::OutcomeRouter`. It consumes the C ABI's hidden mutable
`OwnedInnerOutcome`, clears it, and branches with exactly the same typed
payload arities as `InvokeOutcome`; its router-state owner is the function's
first ordinary Consume parameter. It is illegal in every other function and
must dominate all router work. Task 12's printer/parser/operand visitor and
LLVM lowering map this one kind to `YonaOutcomeRouterEntry`, not to an ordinary
typed or universal function ABI. At this milestone every valid route consumes/
clears both the input outcome and router state through exactly one successor;
structural failure occurs before either is read and follows the mandatory
unchanged-input `Failure::CleanupThenTrap` edge. That cleanup block owns and
releases the hidden input outcome and ordinary router-state parameter, releases
the nonallocating ABI diagnostic, and traps. Task 13 extends the same entry
ABI with its implicit detached-boundary capability and strengthens that
capability's path rule without changing the four payload arities.
Focused router tests corrupt each hidden storage/tag/type contract in turn and
prove the language successors are unreachable while state, outcome, detached
token, and diagnostic are discharged exactly once on the trap edge.
Add `CancellationPointInst` to the pre-outcome instruction payload and
`CancellationRequestedInst` to the post-outcome payload, including exhaustive
operand visitors, printer/parser/remapper, snapshots, and phase verifiers. A
point has a Unit result. Control-outcome lowering splits its block, emits one
Bool query from the function's dominating hidden context, branches false to a
continuation block where the Unit result is defined, and branches true through
the exact `ReturnCancelled` cleanup edge. No cancellation query is invented in
LLVM.
`LlvmBlockLowerer` owns exhaustive lowering for `InvokeOutcome` and all four
`Return*` terminators, while `LlvmFunctionLowerer` selects the direct versus
outcome LLVM signature and allocates the out-parameter once. Focused LLVM
tests exercise every successor/result arity and reject any residual legacy
raise/SJLJ declaration.
The final pipeline invokes `runControlOutcomeLowering` on
`AcceleratorSelected`; focused unit tests may pass `ClosureConverted` only
when the operation/effect, tail-call, and accelerator verifiers prove there is
nothing to instantiate, finalize, or select.
It produces `ControlOutcomeLowered`.

- [ ] **Step 4: Lower raise/catch/no-match through full values**

`Raise{Exception}` transfers the complete nominal `ValueId` into a Raised
outcome. Replace each remaining raise/cancel-capable but operation-free call
with `InvokeOutcome`. This Task 12 milestone accepts only functions and
TryRegions whose verified rows contain no operation application and rejects
raw `Perform`, `InvokeWithContinuation`, prepared boundaries, or any
perform-capable region. For an operation-free TryRegion, route
typed successors directly to propagation or `TryRegion::CatchDispatch`,
passing the whole `ExceptionValue` to its sole block argument. Task 13 extends
this same pass after effect outlining with the perform-capable forms; Task 12
does not predeclare or simulate them. Task 6 has already lowered every
canonical `CatchRoot`; Task 12 never reconstructs a match or calls a second
pattern compiler. Catch projections use normal aggregate descriptors and
ownership rules. The catch root's `RaiseValueFailure` preserves and re-raises
the exact incoming exception when no clause matches; ordinary case/function/
generator roots use `RaiseMatchErrorFailure`, construct `MatchError`, run
cleanup, and return Raised. Neither failure contributes a fabricated value to
a merge. Remove each operation-free TryRegion after its edges are wired; the
phase verifier rejects any residual operation-free try record after
`ControlOutcomeLowered` and validates that nested region exits preserve the
parent chain.

- [ ] **Step 5: Route every `with` exit through finalization**

Register cleanup only after acquisition succeeds. If the acquired type is a
manifest-backed ResourceType, register
`ReleaseResource{Resource, Declaration}` and use its descriptor release;
otherwise register `InvokeFinalizer{Resource, Callable}`, where the callable
value is the statically resolved `Closeable.close` method or another
semantically selected finalizer and may carry captures.
`runControlOutcomeLowering` gives all currently representable
outcomes explicit successors; raw `Perform` is rejected in Task 12. Task 13
inserts effect outlining ahead of this pass and enables the real Performed
path; Task 14 enables real cancellation. Task 10's cleanup lowering creates
reverse-order blocks that invoke the finalizer and then propagate the selected
outcome. Tests assert exactly one close on normal/Raised paths and no close
after failed acquisition here, then add Performed/Cancelled coverage in Tasks
13-14.

Neither resource release nor a callable finalizer can produce a replacement
outcome: release is infallible and the callable's verified empty DirectReturn
contract preserves the in-flight Success/Raised/Performed/Cancelled value
byte-for-byte. Nested finalizers run in reverse acquisition
order. Extend `CleanupEdgePlan`, ownership analysis, cleanup lowering, and
both verifiers with each outcome edge, threading the in-flight outcome and
every cleanup operand explicitly; cleanup materializes exactly that analyzed
expansion, then ownership is reverified on the complete CFG. Tests cover
Success, Raised, failed acquisition, nested reverse order, Performed,
Cancelled, and rejection of any raising/cancelling/performing finalizer.

- [ ] **Step 6: Lower self-tail calls as loop backedges**

Convert eligible self-tail calls to the entry loop's block arguments. Let
ownership lowering insert predecessor-specific transfers/releases. Do not add
function-global cleanup flags. Non-self or effectful tail calls remain ordinary
calls until LLVM tail-call legality is proven. In the final pipeline the pass
consumes `EffectOutlined`, produces `TailCallsLowered`, and runs before
accelerator/outcome/ownership lowering. This task's focused no-effect
milestone may consume `ClosureConverted` only when the operation/effect
verifier proves there is nothing to instantiate or finalize.

- [ ] **Step 7: Run execution, allocation, and no-SJLJ IR checks**

```bash
python3 scripts/run-focused-tests.py ./out/build/x64-debug-linux/tests \
  -tc='Typed IR control outcomes*,Typed IR tail calls*,Control outcome lowering*'
python3 scripts/run-focused-tests.py ./out/build/x64-debug-linux/tests \
  -ts='Pattern ownership'
python3 scripts/run-focused-tests.py ./out/build/x64-debug-linux/tests -tc='Typed IR execution*control*'
python3 test/CMake/typed_ir_no_sjlj_contract.py \
  --build-dir out/build/x64-debug-linux \
  --artifacts out/build/x64-debug-linux/typed-ir-test-artifacts
git diff --check
```

Expected: all outcome fixtures pass at O0-O3 with balanced allocations; the
contract rejects forbidden text in generated LLVM IR and resolves
`llvm-nm`/the target equivalent to reject forbidden undefined symbols in
every generated control object.

- [ ] **Step 8: Commit explicit control flow**

```bash
git add include/yona/TypedIr src/TypedIr src/Codegen/Llvm \
  include/yona/Runtime/Core/Callable.h \
  include/yona/Semantics/SemanticModel.h \
  include/yona/Semantics/TypeChecker.h \
  include/yona/Semantics/RuntimeEntryRegistry.h \
  src/Semantics/SemanticModel.cpp src/Semantics/TypeChecker.cpp \
  test/Semantics/SemanticModelTest.cpp \
  test/Semantics/TypeCheckerTest.cpp \
  test/Semantics/RuntimeEntryRegistryTest.cpp \
  test/Support/CompilerStdlibSourceFixture.h \
  test/TypedIr test/Codegen/ControlOutcomeLoweringTest.cpp \
  test/Codegen/PatternOwnershipTest.cpp test/Fixtures/TypedIr/Control \
  test/CMake/typed_ir_no_sjlj_contract.py \
  cmake/YonaComponents.cmake CMakeLists.txt
git add docs/superpowers/plans/2026-09-01-typed-ir-codegen-rewrite.md
git commit -m "feat: lower nonlocal control to outcomes"
```

### Task 13: Closure-convert effects and typed continuations

**Files:**

- Create: `include/yona/Runtime/Core/Effect.h`
- Create: `src/Runtime/Core/Effect.c`
- Modify: `include/yona/Runtime/Core/OwnedSlotState.h`
- Modify: `src/Runtime/Core/OwnedSlotState.c`
- Create: `include/yona/TypedIr/Passes/EffectPreparation.h`
- Create: `src/TypedIr/Passes/EffectPreparation.cpp`
- Create: `include/yona/TypedIr/Analysis/BoundaryContextAnalysis.h`
- Create: `src/TypedIr/Analysis/BoundaryContextAnalysis.cpp`
- Create: `include/yona/TypedIr/Passes/OperationInstantiation.h`
- Create: `src/TypedIr/Passes/OperationInstantiation.cpp`
- Create: `include/yona/TypedIr/Passes/EffectConversion.h`
- Create: `src/TypedIr/Passes/EffectConversion.cpp`
- Create: `include/yona/TypedIr/Verification/EffectVerifier.h`
- Create: `src/TypedIr/Verification/EffectVerifier.cpp`
- Create: `test/TypedIr/EffectConversionTest.cpp`
- Create: `test/TypedIr/OperationInstantiationTest.cpp`
- Create: `test/Runtime/EffectTest.cpp`
- Create: `test/TypedIr/Snapshots/effects/capture_float.tir`
- Create: `test/TypedIr/Snapshots/effects/nested_continuation.tir`
- Create: `test/Codegen/EffectLoweringTest.cpp`
- Create: `test/Fixtures/TypedIr/Effects/effect_capture_bool.yona`
- Create: `test/Fixtures/TypedIr/Effects/effect_capture_bool.expected`
- Create: `test/Fixtures/TypedIr/Effects/effect_capture_float.yona`
- Create: `test/Fixtures/TypedIr/Effects/effect_capture_float.expected`
- Create: `test/Fixtures/TypedIr/Effects/effect_capture_heap.yona`
- Create: `test/Fixtures/TypedIr/Effects/effect_capture_heap.expected`
- Create: `test/Fixtures/TypedIr/Effects/effect_typed_resume.yona`
- Create: `test/Fixtures/TypedIr/Effects/effect_typed_resume.expected`
- Create: `test/Fixtures/TypedIr/Effects/effect_nested_continuation.yona`
- Create: `test/Fixtures/TypedIr/Effects/effect_nested_continuation.expected`
- Create: `test/Fixtures/TypedIr/Effects/effect_with_cleanup.yona`
- Create: `test/Fixtures/TypedIr/Effects/effect_with_cleanup.expected`
- Modify: `test/TypedIr/CleanupVerifierTest.cpp`
- Modify: `include/yona/TypedIr/Instruction.h`
- Modify: `include/yona/TypedIr/TypedIr.h`
- Modify: `include/yona/TypedIr/Callable.h`
- Modify: `src/TypedIr/Verifier.cpp`
- Modify: `src/TypedIr/Printer.cpp`
- Modify: `src/TypedIr/Parser.cpp`
- Modify: `test/TypedIr/PrinterParserTest.cpp`
- Modify: `test/TypedIr/ClosureConversionTest.cpp`
- Modify: `src/TypedIr/AstLowering.cpp`
- Modify: `src/TypedIr/Analysis/FreeVariables.cpp`
- Modify: `src/TypedIr/Analysis/EscapeAnalysis.cpp`
- Modify: `src/TypedIr/Analysis/OwnershipAnalysis.cpp`
- Modify: `src/TypedIr/Passes/ClosureConversion.cpp`
- Modify: `src/TypedIr/Passes/CleanupLowering.cpp`
- Modify: `src/TypedIr/Verification/CallableVerifier.cpp`
- Modify: `src/TypedIr/Verification/EscapeVerifier.cpp`
- Modify: `src/TypedIr/Verification/OwnershipVerifier.cpp`
- Modify: `src/TypedIr/Verification/CleanupVerifier.cpp`
- Modify: `src/TypedIr/Passes/ControlOutcomeLowering.cpp`
- Modify: `src/TypedIr/Pipeline.cpp`
- Modify: `src/Codegen/Llvm/BlockLowerer.cpp`
- Modify: `src/Codegen/Llvm/FunctionLowerer.cpp`
- Modify: `cmake/YonaComponents.cmake`
- Modify: `CMakeLists.txt`
- Modify: `docs/typed-ir.md`

**Interfaces:**

- Consumes: no-async `CleanupPrepared` from Task 10's verified phase fast path,
  including prepared cleanup obligations/drop entries, plus Task 9 callable
  vocabulary/ABI and Task 12 outcome vocabulary. Task 14 later extends every
  affected pass/verifier to the explicit async records.
- Produces: the staged `EffectPrepared` -> `ClosureConverted` ->
  `OperationInstantiated` -> `EffectOutlined` transition: first every
  effect-generated function, then every closure adapter and the frozen
  function set, then closed `OperationInstanceId`/runtime rows, then ordinary
  generated handler-entry/dispatch CFG. Task 12's later control-outcome pass
  materializes owned `YonaEffectRequestRef` values from the prepared
  suspension terminators.
- Invariant: effect argument/result/resume carriers use structural types and
  the universal callable ABI; runtime code does not dispatch handlers.

- [ ] **Step 1: Write red effect conversion and execution tests**

Cover lexical captures, Unit/Bool/Int/Float/String/aggregate operation
arguments and results, typed resume values, nested handlers, returned handler
values, unhandled propagation, and cleanup across perform. Assert converted
handler functions have only their declared environment/arguments:

Extend exact cleanup case `Typed IR control outcomes: with finalizer runs for
every control outcome` with its Performed replacement/propagation rows.

```cpp
TEST_CASE("Effect conversion gives handlers explicit lexical environments") {
  auto Module = lowerToCleanupPrepared(
      "let scale = 2.5 in handle perform Math.scale 4.0 with "
      "Math.scale value resume -> resume (value * scale) end");
  REQUIRE(runEffectPreparation(Module).has_value());
  REQUIRE(runClosureConversion(Module).has_value());
  REQUIRE(runOperationInstantiation(Module).has_value());
  REQUIRE(runEffectConversion(Module).has_value());
  REQUIRE(verifyEffects(Module).has_value());
  const auto Handler = findGeneratedHandler(Module, "Math.scale");
  CHECK(parameterTypes(Module, Handler) ==
        std::vector{environmentType(Module, Handler), floatType(Module),
                    handlerResumeType(Module, Handler)});
  CHECK(foreignValueUses(Module, Handler).empty());
}
```

`lowerToCleanupPrepared` is a test helper that runs the normal canonical,
generic, pattern, generator, control-flow, Task 10 no-async preparation, and
cleanup-preparation passes. It never writes `Module::Phase` directly; a test
that accidentally contains a Promise/task form fails at the no-async boundary.

Create top-level sentinels `Effect lowering preserves typed argument and
resume carriers` and `Typed IR execution: effects preserve captures and
cleanup`; the shown conversion case and Task 12's control-outcome sentinel
cover the remaining positive prefixes.

- [ ] **Step 2: Run and confirm conversion/runtime APIs are absent**

```bash
cmake --build --preset build-debug-linux --target tests -j2
python3 scripts/run-focused-tests.py ./out/build/x64-debug-linux/tests \
  -tc='Effect conversion*,Effect lowering*,Typed IR execution*effect*,Typed IR control outcomes*'
```

Expected: compile failures for `EffectPreparation.h`, `EffectConversion.h`,
and `Effect.h`.

- [ ] **Step 3: Define the owned effect-request runtime object**

`Effect.h` includes Task 10's `OwnedSlotState.h`; the records below reference
that already-final `YonaOwnedSlotStateDescriptor` and do not redeclare its ABI
or generic state APIs. Expose only the effect-specific surface:

```c
typedef struct YonaEffectRequest *YonaEffectRequestRef;
typedef struct YonaContinuationBoundaryNode *YonaContinuationBoundaryNodeRef;
typedef struct YonaContinuationDescriptor YonaContinuationDescriptor;
typedef struct YonaContinuationBoundaryContext
    YonaContinuationBoundaryContext;
typedef struct {
  const YonaEffectOperationDescriptor *Operation;
  const YonaContinuationDescriptor *CurrentContinuationType;
  const YonaContinuationDescriptor *ResultingContinuationType;
} YonaContinuationTransition;
typedef struct {
  uint32_t AbiVersion;
  uint32_t Reserved;
  const YonaContinuationTransition *Entries;
  uint64_t EntryCount;
} YonaContinuationTransitionTable;
struct YonaContinuationDescriptor {
  uint32_t AbiVersion;
  YonaAbiCallingConvention Convention;
  uint32_t Reserved;
  uint32_t InputReserved;
  uint64_t StructuralFingerprint;
  const uint8_t *CanonicalBytes;
  uint64_t CanonicalByteCount;
  const YonaAbiTypeDescriptor *CarrierType;
  const YonaAbiTypeDescriptor *InputType;
  YonaAbiParameterOwnership InputOwnership;
  uint32_t ResultReserved;
  const YonaAbiTypeDescriptor *ResultType;
  YonaAbiResultOwnership ResultOwnership;
  uint32_t EffectReserved;
  const YonaAbiEffectRowDescriptor *EffectRow;
};
typedef void (*YonaOutcomeRouterEntry)(
    YonaContinuationBoundaryNodeRef *OwnedDetachedBoundary,
    YonaAbiValue *OwnedBoundaryState,
    YonaControlOutcome *OwnedInnerOutcome,
    const YonaContinuationBoundaryContext *BorrowedOuterBoundaryContext,
    const YonaExecutionContext *BorrowedContext,
    YonaControlOutcome *EmptyOutcome);
typedef struct YonaContinuationBoundaryDescriptor {
  uint32_t AbiVersion;
  uint32_t Reserved;
  uint64_t BoundaryIdentityFingerprint;
  const uint8_t *BoundaryIdentityCanonicalBytes;
  uint64_t BoundaryIdentityCanonicalByteCount;
  const YonaOwnedSlotStateDescriptor *State;
} YonaContinuationBoundaryDescriptor;
typedef uint32_t YonaContinuationBoundaryContextKind;
enum {
  YONA_CONTINUATION_BOUNDARY_CONTEXT_DIRECT = 0u,
  YONA_CONTINUATION_BOUNDARY_CONTEXT_CHAIN = 1u
};
struct YonaContinuationBoundaryContext {
  uint32_t AbiVersion;
  YonaContinuationBoundaryContextKind Kind;
  uint32_t Reserved0;
  uint32_t Reserved1;
  const void *BorrowedRemainingChain;
  const struct YonaContinuationBoundaryContext *BorrowedParentContext;
  const YonaContinuationBoundaryDescriptor *BorrowedDirectBoundary;
  const YonaAbiValue *BorrowedDirectState;
};
typedef struct {
  YonaContinuationBoundaryDescriptor Boundary;
  const YonaAbiTypeDescriptor *InnerResultType;
  const YonaAbiTypeDescriptor *ResultType;
  const YonaAbiEffectRowDescriptor *ResultEffects;
  YonaOutcomeRouterEntry RouteMove;
} YonaHandlerBoundaryDescriptor;
typedef struct {
  YonaContinuationBoundaryDescriptor Boundary;
  const YonaAbiTypeDescriptor *ProtectedResultType;
  const YonaAbiTypeDescriptor *ResultType;
  const YonaAbiEffectRowDescriptor *ResultEffects;
  YonaOutcomeRouterEntry RouteMove;
} YonaTryBoundaryDescriptor;
bool YonaRuntimeEffectRequestCreateMove(
    const YonaEffectOperationDescriptor *Operation,
    YonaAbiArgument *Arguments,
    uint64_t ArgumentCount,
    const YonaContinuationDescriptor *InitialContinuationType,
    YonaCallableRef *OwnedInitialFrame,
    YonaEffectRequestRef *EmptyOutput,
    YonaControlOutcome *EmptyFailure);
void YonaRuntimeEffectRequestRelease(YonaEffectRequestRef Request);
bool YonaRuntimeEffectRequestOperationBorrowed(
    YonaEffectRequestRef Request,
    const YonaEffectOperationDescriptor **EmptyBorrowedOperation,
    YonaControlOutcome *EmptyFailure);
bool YonaRuntimeEffectRequestArgumentCount(
    YonaEffectRequestRef Request, uint64_t *EmptyCount,
    YonaControlOutcome *EmptyFailure);
bool YonaRuntimeEffectRequestArgumentBorrowed(
    YonaEffectRequestRef Request, uint64_t Index,
    const YonaAbiValue **EmptyBorrowedArgument,
    YonaControlOutcome *EmptyFailure);
bool YonaRuntimeEffectRequestContinuationBorrowed(
    YonaEffectRequestRef Request,
    const YonaAbiValue **EmptyBorrowedContinuation,
    YonaControlOutcome *EmptyFailure);
bool YonaRuntimeEffectRequestAppendContinuationFrameMove(
    YonaEffectRequestRef *OwnedUniqueRequest,
    const YonaContinuationTransitionTable *Transitions,
    YonaCallableRef *OwnedOuterFrame,
    YonaControlOutcome *EmptyFailure);
bool YonaRuntimeEffectRequestAppendHandlerBoundaryMove(
    YonaEffectRequestRef *OwnedUniqueRequest,
    const YonaContinuationTransitionTable *Transitions,
    const YonaHandlerBoundaryDescriptor *Boundary,
    YonaAbiValue *OwnedRouterState,
    YonaControlOutcome *EmptyFailure);
bool YonaRuntimeContinuationAppendHandlerBoundaryMove(
    YonaAbiValue *OwnedContinuation,
    const YonaEffectOperationDescriptor *Operation,
    const YonaContinuationTransitionTable *Transitions,
    const YonaHandlerBoundaryDescriptor *Boundary,
    YonaAbiValue *OwnedRouterState,
    YonaControlOutcome *EmptyFailure);
bool YonaRuntimeEffectRequestAppendTryBoundaryMove(
    YonaEffectRequestRef *OwnedUniqueRequest,
    const YonaContinuationTransitionTable *Transitions,
    const YonaTryBoundaryDescriptor *Boundary,
    YonaAbiValue *OwnedTryState,
    YonaControlOutcome *EmptyFailure);
bool YonaRuntimeContinuationAppendTryBoundaryMove(
    YonaAbiValue *OwnedContinuation,
    const YonaEffectOperationDescriptor *Operation,
    const YonaContinuationTransitionTable *Transitions,
    const YonaTryBoundaryDescriptor *Boundary,
    YonaAbiValue *OwnedTryState,
    YonaControlOutcome *EmptyFailure);
void YonaRuntimeContinuationBoundaryNodeReleaseMove(
    YonaContinuationBoundaryNodeRef *OwnedDetachedBoundary);
void YonaRuntimeEffectRequestReinstallBoundaryNodeMove(
    YonaEffectRequestRef *OwnedUniqueRequest,
    YonaContinuationBoundaryNodeRef *OwnedDetachedBoundary,
    YonaAbiValue *OwnedBoundaryState);
void YonaRuntimeContinuationBoundaryContextInitializeDirectBorrowed(
    const YonaContinuationBoundaryDescriptor *Boundary,
    const YonaAbiValue *BorrowedState,
    const YonaContinuationBoundaryContext *BorrowedParentContext,
    YonaContinuationBoundaryContext *EmptyContext);
bool YonaRuntimeContinuationBoundaryCaptureBorrowed(
    const YonaContinuationBoundaryContext *BorrowedContext,
    const YonaContinuationBoundaryDescriptor *ExpectedBoundary,
    uint64_t FieldIndex,
    const YonaAbiValue **EmptyBorrowedField,
    YonaControlOutcome *EmptyFailure);
bool YonaRuntimeHandlerResumeCreateMove(
    const YonaHandlerBoundaryDescriptor *Boundary,
    const YonaEffectOperationDescriptor *Operation,
    const YonaContinuationTransitionTable *Transitions,
    const YonaCallableDescriptor *ResumeCallable,
    YonaAbiValue *OwnedRawContinuation,
    YonaAbiValue *OwnedRouterState,
    YonaAbiValue *EmptyResumeCallable,
    YonaControlOutcome *EmptyFailure);
bool YonaRuntimeEffectRequestTakePayloadMove(
    YonaEffectRequestRef *OwnedRequest,
    YonaAbiValue *EmptyArguments,
    uint64_t ArgumentCount,
    YonaAbiValue *EmptyContinuation,
    YonaControlOutcome *EmptyFailure);
bool YonaRuntimeContinuationResumeMove(
    YonaAbiValue *OwnedContinuation,
    YonaAbiArgument *Argument,
    const YonaContinuationBoundaryContext *BorrowedOuterBoundaryContext,
    const YonaExecutionContext *BorrowedContext,
    YonaControlOutcome *EmptyOutcome);
```

`YonaOwnedSlotStateDescriptor::DropMove` is non-null for every descriptor,
including an all-trivial/zero-field state. Descriptor validation rejects a
null drop entry, wrong calling convention, or a DropIdentity that differs
from the canonical `OwnedSlotStateType`. The three DropIdentity fields are the
exact canonical `FunctionDeclarationIdentity` encoding embedded in the carrier
type bytes; the fingerprint is only a prefilter. Runtime validation compares
the complete bytes. Compiler descriptor emission additionally proves that
`DropMove` is the generated adapter for that exact FunctionId/identity; runtime
cannot infer a C function's semantic identity from its address and does not
pretend pointer equality proves it. The callback is infallible,
nonperforming, nonraising, and noncancelling; it consumes/clears the state and
every still-armed field exactly once. Handler, try, and cleanup preparation
all synthesize and freeze this entry before descriptor publication—there is no
runtime fallback that loops over fields without the declared identity.

`YonaContinuationBoundaryDescriptor` is the exact common first member of both
boundary records. Its state pointer is `RouterState` or `TryState`,
respectively; validators require the enclosing record and common member to
agree and compare full identity bytes after the fingerprint prefilter. The
fixed-size boundary context is an ephemeral synchronous view, never an ABI
value or descriptor child. A Direct context contains one live local
boundary/state pair, null `BorrowedRemainingChain`, and an optional borrowed
parent context for lexically enclosing live/installed boundaries. A Chain
context contains the runtime-private remaining-chain pointer plus an optional
borrowed parent context; its direct fields are null. The chain pointer may be
null only when the parent is non-null; if both are absent the caller passes a
null boundary-context value instead of constructing an empty Chain record.
Lookup searches owned remaining-chain nodes first and only then the borrowed
parent. The infallible direct initializer merely writes
this compiler-controlled stack record after the caller has verified the
static boundary, live state, and longer-lived parent view; it does not retain,
allocate, inspect a field, or outlive the immediately nested frame apply. The
capture helper validates version/kind/reserved/null rules, rejects a cyclic
parent/remaining chain using iterative constant-space cycle detection, and
searches the local Direct entry before its parent (or
the runtime Chain), then uses full identity and state-descriptor equivalence.
Runtime resume constructs a Chain context while its loop owns the remainder
and borrows the resume call's incoming outer context as that view's parent.
Exact 64-bit layout/offset assertions and malformed/cyclic-context tests cover
both variants. There is no fixed nesting-depth limit: finite synchronous
recursion through Direct parents and runtime Chain nodes remains valid.

Each Direct context contributes exactly one local boundary/state pair, but a
frame may require that boundary plus any statically known enclosing boundary.
The verifier stores the frame's ordered boundary requirements, requires the
local recipes of `InvokeBoundaryFrameOutcome` to name its exact state
descriptor, and requires every additional recipe to be satisfiable by the
forwarded ambient context contract. It rejects duplicate identities,
out-of-scope ancestors, or two different states claimed for one identity—not
valid nested states.

Task 13 makes continuation-boundary context a distinct uniform hidden call
channel. Before publishing `EffectPrepared`, it installs one rootless
Borrow-parameter `BoundaryContextParameter` of
`AbiOpaqueKind::ContinuationBoundaryContext` on every non-NativeExtern Yona
Definition/Imported direct entry, whether DirectReturn or ExplicitOutcome, and
on every generated `OutcomeRouter`. Closure conversion installs the same IR
parameter on every universal adapter and maps it to the universal ABI's
explicit `BorrowedBoundaryContext` argument. `CleanupDrop` and raw
NativeExtern entries, plus the fixed-ABI `KeyHashAdapter` and
`KeyEqualsAdapter` roots, are the only generated/runtime call entries that do
not receive it. The pointer may be null at runtime, but the in-module parameter and
every required call operand are never omitted; public/synchronous root wrappers
pass null into that parameter. It is not part of source arity, FunctionType,
generic substitution, a callable environment, or any stored descriptor.

The resulting generated ABI order is fixed: a DirectReturn Yona entry takes
`[hidden callable environment if required], declared parameters...,
BorrowedBoundaryContext` and returns its typed value; an ExplicitOutcome Yona
entry then takes `BorrowedExecutionContext, EmptyOutcome`. Universal adapters
use their separately declared environment/argument-array/count/boundary/
execution/outcome order. `OutcomeRouter` uses its detached-boundary/state/
inner-outcome/outer-boundary/execution/outcome order. NativeExtern signatures
are unchanged, and CleanupDrop remains state plus execution context. LLVM ABI
tests assert every ordering and forbid platform-dependent insertion.

In the same preparation transaction, every Yona `DirectCallInst` and every
surviving dynamic `ApplyCallableInst` outside `FunctionEntryKind::CleanupDrop`
receives the caller's exact dominating
boundary-context parameter; NativeExtern direct calls keep
`BoundaryContext = nullopt`. A dynamic call always crosses the universal Yona
entry, so its operand is required even when its closed row is operation-free.
The predeclared effect-free finalizer apply inside a CleanupDrop is the one
generated-runtime-root exception: it keeps `BoundaryContext = nullopt`, and
LLVM passes a literal null to `YonaRuntimeCallableApplyMove` because that
verified finalizer cannot raise, cancel, or perform.
The other fixed-ABI exception is the DirectCall inside a KeyHashAdapter or
KeyEqualsAdapter: its exact target is verifier-proven pure, it keeps
`BoundaryContext = nullopt`, and LLVM passes a literal null hidden boundary
argument. Those adapters have no dynamic apply and cannot call any other
target. No ordinary Typed/Imported entry may exploit either exception.
Prepared `ForwardCallableOutcome`,
`InvokeWithContinuation`, `InvokeHandledBodyOutcome`, and
`InvokeProtectedTryOutcome` carry that exact ValueId; the two initial boundary
invocations name it `ParentBoundaryContext`. `ResumeContinuationOutcome`
carries it independently as the ambient lookup tail for synchronous resume.
Control-outcome lowering
preserves the `ApplyCallableInst` operand when it rewrites an operation-free
dynamic call to `InvokeOutcome`; it may never rediscover the value from the
containing function. A dynamic callable or Yona FunctionId requires the
operand, while a NativeExtern FunctionId forbids it. A recursive explicit
FunctionId call also preserves its independently checked
`CallableEnvironment`; only verified intra-SCC calls may set that field.
`EffectPrepared` through `LlvmReady` require this full forwarding rule and
reject missing, foreign, wrong-type, nondominating, captured, stored, or
returned boundary-context values.

`BoundaryContextContract` and `BoundaryRequirementSet` are compiler-only,
duplicate-free sets in canonical boundary-ID order; runtime lookup order is a
property of the dynamic Direct/Chain value, not these sets. Neither record is
emitted in a public type/callable descriptor, v2, or TIRF. The module interns
one canonical empty requirement set at foundation. Before Task 13 exists,
Task 9's isolated absence-verified milestone assigns that empty set to every
function-typed value and `CallableDescriptor`; its Function context/requirement
fields remain absent. In the complete pipeline, `runEffectPreparation` invokes
the named `BoundaryContextAnalysis` after every generated function, boundary,
call edge, and continuation segment has been reserved and before publishing
`EffectPrepared`.

The analysis solves two deterministic whole-module monotone fixed points.
`Required(F)`, persisted as `Function::AmbientBoundaryRequirements`, is the
least set containing every boundary queried by a
`BoundaryPinnedCaptureBorrow` reachable in F and every callee/callable
requirement not discharged by that call's verified local Direct overlay.
`Guaranteed(F)`, persisted as `Function::BoundaryContextContract`, is the
greatest sound set bounded by every possible entry edge: a source, exported,
imported, unknown, or async-worker root supplies empty; ordinary direct or
universal forwarding supplies the caller's guarantee; an
`InvokeBoundaryFunctionOutcome`/`InvokeBoundaryFrameOutcome` edge supplies its
parent guarantee plus its one verified local boundary; and a continuation
frame entry supplies only boundaries statically installed in its owned chain
plus requirements that its resume caller is itself required to provide. Entry
sets meet by intersection. A function with no proved incoming edge receives
empty, and a recursive SCC cannot manufacture a guarantee solely from its own
cycle. Every reachable function must finish with
`Required(F) subset-of Guaranteed(F)`.

For a prepared success frame, `Required(Frame) - Guaranteed(Parent)` is either
empty, in which case `SuccessFrameOverlay` is null and lowering uses ordinary
`InvokeOutcome`, or exactly one boundary, in which case preparation records
the matching `BoundaryInvocationOverlay` with its dominating scoped unique
state loan and exact parent-context ValueId. More than one missing boundary,
the wrong boundary/state descriptor, or a missing/non-dominating loan is a
ranged preparation error. Later passes consume this stored decision and never
rediscover an overlay from CFG shape.

Closure conversion preserves both solved Function records, gives each
universal adapter the same records as its direct root, and writes exactly
`Required(F)` to `CallableDescriptor::AmbientBoundaryRequirements`. From
`EffectPrepared` onward every function-typed `Value` has a nonempty optional
field containing its exact requirement-set ID—including the canonical empty
set—while every non-function value forbids the field. Moves, block arguments,
function construction, recursive members, and partial application preserve
the set. A join records the canonical union of incoming callable requirements,
and each incoming set must be a subset of that block argument's set; dropping
or weakening an incoming requirement is invalid. Dynamic Apply requires that
set to be a subset of the containing function's guarantee. Text, remap, clone,
phase, callable, effect, ownership, and escape verifiers preserve and recheck
all IDs through `LlvmReady`; operation instantiation may fill runtime rows but
may not change a boundary contract.

An ordinary nested call therefore forwards its hidden parameter unchanged. A
nested initial Success overlays a new Direct entry on the explicit
`InvokeBoundaryFrameOutcome::ParentBoundaryContext`; a resumed frame forwards
the Chain view. Null is legal only as the runtime value supplied to a true
source/async root, or by the explicit CleanupDrop finalizer-root exception
above, never as missing Typed IR data for any other call. Thus a resumed frame A that directly
enters a helper/inner try or invokes suffix frame B preserves the outer Chain,
while nested immediate-success paths form a stack of Direct views without
storing any boundary pointer in a callable.

Verifier/LLVM tests cover root-null entry, direct and universal forwarding
(including a nested universal call to an operation-free DirectReturn helper),
the exact adapter/router ABI order, and retained recursive
`CallableEnvironment` alongside the independent boundary operand. Negative
rows reject absent/foreign/nondominating/wrong-type operands and any
store/capture/return/descriptor use. End-to-end cases cover Direct-parent-
Direct nested try/handle success with two non-Shareable state fields, resumed
outer Chain -> operation-free dynamic callable -> direct helper -> nested
try/handle -> pinned suffix, a detached
inner router whose catch/success/dispatch synchronously reaches an outer pinned
field, and an async awaited suffix inside a loaned handler/try state on both
Success and Performed. Re-perform, abandonment, malformed/cyclic
contexts, and every state/frame cleanup order remain balanced. An abandonment
case executes a captured finalizer through CleanupDrop, verifies the literal-
null ApplyMove boundary argument, and proves once-only cleanup.

`Effect.h` includes Task 9's `Callable.h`/`AbiArgument.h` (and therefore Task
7's `Abi.h`/`Outcome.h`); the complete
`YonaEffectOperationDescriptor` and
`YonaRuntimeEffectOperationEquivalent` contracts are defined and implemented
there. This task adds only the owned request abstraction and descriptor
emission from verified runtime-effect rows.

`OperationDeclaration` plus its closed `OperationInstance` are the compiler
truth; lowering emits one immutable runtime descriptor per reachable closed
instance, never per open declaration. Its runtime fingerprint and canonical
bytes come only from Task 2's closed isolated graph. Creation validates the
exact descriptor and is all-or-nothing: `EmptyOutput` must be non-null,
initially null; `EmptyFailure` must be non-null and Empty; both are mutually
storage-distinct from `OwnedInitialFrame`, every mutable Consume slot, every
Trivial/Borrow source, and the argument-record array. It uses Task 9's shared argument
stage internally: clone/copy/validation and request allocation finish while
the stage is uncommitted; success infallibly commits Consume moves and the
continuation move immediately before publishing the request. Validation/OOM/
staging failure with structurally valid storage discards the stage, leaves
arguments and continuation unchanged, keeps output null, and writes the exact
nonallocating OOM or descriptor-mismatch outcome to the initially Empty
failure output. Null/overlapping storage returns false without reading a
carrier or mutating/writing any slot. Success
leaves that output Empty. The request owns the committed stage as its
argument storage, so no second copy/allocation can fail after commit.
All four request-query APIs are checked nonmutating transactions. Their result
and initially Empty failure slots are non-null and pairwise byte-disjoint;
borrowed-pointer results start null and count starts zero. Null/corrupt request,
wrong descriptor, count/index mismatch, or overlap returns false without
changing the request/result; structurally valid corruption writes the exact
reserved descriptor diagnostic. Success initializes the exact operation,
count, argument Borrow, or continuation Borrow and leaves failure Empty. The
borrow remains valid only while the request owner is live and unmodified.
Effect requests are
linear: their ABI descriptor's `TryRetain` always returns false and no public
retain API exists. Release is null-safe and destroys the sole owner. The Performed
outcome owns the request through a dedicated ABI type descriptor.
The request owns a non-Shareable typed `YonaAbiValue` whose word refers to an
ordered chain of universal one-argument frames. `CarrierType` must be a
managed, non-Shareable `YONA_ABI_CONTINUATION` descriptor with retain/release
callbacks. `YonaContinuationDescriptor` is the authoritative closed function
contract for that carrier: exactly one input type/ownership, final result
type/ownership, `YONA_ABI_CALL_CONTINUATION`, and the complete closed runtime
effect row. Its canonical bytes include all of those fields, and all reserved
fields must be zero. Raw continuation contracts use one portable boundary-
normalization rule: for operation `O` escaping a function/handled body `F`,
the input is `O`'s result, the result is `F`'s declared result, and the row is
`F`'s complete closed declared/body row. The exact suffix `ResumeEffects`
remains an IR analysis fact and must be a subset of that normalized row, but
does not create site-specific ABI types. Thus two performs of the same closed
operation in one boundary have the same raw descriptor, and an imported or
dynamic callee's public closed FunctionType contains everything needed to
predict its escaping descriptors. The descriptor is the runtime form of an ordinary structural
`FunctionType` with `CallingConvention::Continuation`, not an untyped builtin.

Creation validates the initial frame's callable descriptor against the
supplied continuation descriptor, including operation-result input, enclosing
body-result output, ownership, and convention; the exact frame row must be a
full-application subset of the boundary-normalized descriptor row, not equal
to it, before wrapping it
transactionally. Each append site supplies an immutable, ABI-versioned
`YonaContinuationTransitionTable` derived only from the callee's closed public
FunctionType and the caller boundary. In canonical operation/application
order it maps every permitted pair of the request's full operation descriptor
and boundary-normalized current continuation descriptor to the already-
interned normalized caller-boundary descriptor. Append fully matches the
request pair, validates current-result/outer-input and the selected resulting
descriptor against the one-argument outer frame, and requires both the current
normalized row and exact outer-frame row to be subsets of the selected caller-
boundary row. It then allocates the list node and
stores that transition table on the node. On success it clears the frame owner
and replaces the request's typed continuation descriptor/value atomically.
Every request-mutating append takes a non-null
`YonaEffectRequestRef *OwnedUniqueRequest`, verifies the owner is present and
the internal reference count is one, and is storage-distinct from every moved
input/failure slot before reading it. It never mutates through a borrowed or
retained alias.
No runtime effect-row union or descriptor allocation occurs. Descriptor/OOM failure leaves
request/frame unchanged and writes the exact nonallocating outcome to the
initially Empty failure slot; a structural storage violation returns false
without mutating or writing any slot. Forced-wrong-descriptor, wrong-result,
wrong-row, and hash-collision tests require full structural rejection. This is
how a Performed outcome crossing a call boundary retains the caller's
remaining computation without allowing the compiler to fabricate a carrier
type after the fact.

The two handler-boundary APIs install the same outcome-intercepting node. The
descriptor is immutable generated data: its state/result descriptors and
effect row are closed, reserved fields are zero, and `RouteMove` is a generated
entry with the exact signature shown above. Operation instantiation closes
and emits both the descriptor and its transition table; finalization only
references their IDs. The request form selects the transition by the request's
full operation/current-continuation pair. The standalone continuation form
uses its explicit operation plus the carrier's authoritative current
descriptor. Both allocate and validate the node before committing; success
moves/clears the unique router-state owner and atomically replaces the typed
chain, while descriptor/OOM failure leaves the request/continuation and state
unchanged and writes the exact failure to an initially Empty, byte-range-
distinct failure slot. Null/overlapping storage changes nothing. A boundary
state descriptor may contain arbitrary linear fields and is always
non-Shareable.

The two try-boundary APIs use the identical allocation, descriptor,
transition, unique-owner, failure, and splice contracts but install a
`YonaTryBoundaryDescriptor`. Its generated router sends Success to the
protected expression's lexical continuation, Raised to the persisted catch
dispatch with the complete exception, Performed through the same detached
boundary node reinstalled on the nested request, and Cancelled outward after
region cleanup. Catch bodies and an unmatched re-raise bypass the boundary, so
they are not caught by the same `try`. The one owned try state contains the
catch environment and outside suffix; no `TryRegion` record is duplicated
between continuation segments.

The resume loop detaches, but does not free, a boundary node before invoking
its router. It atomically moves the node's state into `OwnedBoundaryState`,
clears the state slot in the now-empty `OwnedDetachedBoundary` token, and
builds an ephemeral Chain view over the saved owned outer remainder with the
resume call's `BorrowedOuterBoundaryContext` as its lookup-only parent. If the
owned remainder is empty it passes that parent directly; it never constructs a
context with neither source. It passes both owners plus this borrowed view to
`RouteMove`. The router's `BoundaryContextParameter` denotes this owned outer
remainder followed by the borrowed ambient lookup tail; the current detached
state is already its separate Owned parameter.
Neither the router nor any descendant may store the view. A router must consume
both owners exactly once. On
a try router's nested Performed path,
`YonaRuntimeEffectRequestReinstallBoundaryNodeMove` moves the state back into
that same token and relinks it into the verified unique request without
allocation; on every other try path it consumes the state through the selected
continuation/cleanup and releases the empty token. A handler router releases
the consumed token before its prepared dispatch; installing the semantically
fresh handler boundary for an unhandled request or source-resume callable
continues to use the checked transactional APIs above. The two `void` token
helpers are generated-router-only operations: their arguments can arise only
from a successful runtime detach and verified router CFG. They clear every
moved owner, allocate nothing, and trap on an impossible internal contract
violation rather than exposing a source outcome.

`YonaRuntimeHandlerResumeCreateMove` is the sole lowering of
`MakeHandlerResume`. It validates and allocates the boundary node, resume
callable/environment, and all staging storage before committing either owner.
The explicit `Operation` must fully match the raw continuation's current
descriptor and the selected transition-table row; a fingerprint match alone
is insufficient, and a multi-operation table cannot select by insertion order.
One infallible commit
then consumes/clears the raw continuation and router state and publishes the
callable. Any descriptor, operation, transition, alias, or forced allocation
failure leaves both owners and the Empty output unchanged and writes the exact
failure outcome. Lowering may not sequence a boundary append and callable
creation as two separately committing calls. ABI tests force failure at every
allocation point, collide operation fingerprints, and select two different
operations against the same raw continuation descriptor.

Router-state creation uses the shared argument stage over the descriptor's
complete field vector. It validates/allocates before commit, then moves Consume
captures and publishes one non-Shareable managed `YonaAbiValue`; failure leaves
captures/output unchanged and reports exact OOM/descriptor mismatch under the
same nonoverlap rules as callable creation. Borrow exposes one nonempty slot
only while the state owner is live. Take requires either that owner or a
verifier-issued scoped unique loan while the owner is suspended, validates an
Empty distinct output, then atomically moves/clears exactly that
slot; repeated take fails unchanged. Releasing the carrier clears/releases all
remaining slots. The state descriptor and boundary descriptor have exact
64-bit size/offset, version, reserved-field, identity-byte, vector, and full
structural-equivalence tests. Both field accessors require a non-null initially Empty
failure outcome and an initially null/Empty result slot byte-disjoint from the
state, failure, and every carrier inside it. Null, wrong kind/descriptor,
out-of-range, empty/repeated-take, and overlap rejection leave state and result
unchanged; structurally valid descriptor/storage corruption writes the exact
reserved diagnostic. Success initializes one exact result and leaves the
failure Empty.

`YonaRuntimeContinuationResumeMove` first verifies that the owned value is a
continuation carrier whose embedded descriptor is fully equivalent to the
chain object's authoritative descriptor. It then has the same
structural-validity bool and callee-owns rule as callable apply: a true return consumes the continuation
and any Consume resume slot, while immutable Trivial/Borrow input remains
unchanged; false is reserved exclusively for complete preflight rejection of
null/sentinel storage, alias ranges, execution-context shape, boundary-context
shape/cycles, argument carrier,
or the initial authoritative continuation descriptor, and leaves every input
unchanged. The runtime validates all initially reachable descriptor/table
records it can inspect before committing. After it clears either Consume slot
the operation is committed and must return true; no later branch can claim a
rollback-style false poststate.
Preflight proves graph disjointness, not merely top-level byte-range
disjointness. No context record, Chain node reachable from
`BorrowedOuterBoundaryContext`, or Direct state/carrier reachable from that
context may lie in the owned continuation graph this call can detach, relink,
consume, or destroy. None may overlap the mutable continuation,
Consume-argument, or outcome slots. Immutable descriptor/transition-table
records may be shared. Validation is allocation-free and completes before
either Consume slot clears; overlap returns false with the continuation,
argument, and EmptyOutcome byte-for-byte unchanged. The generated verifier
proves the same fact from provenance for compiler-built calls, while the public
C entry still rejects forged overlap. Runtime tests use an outer Chain whose
remaining pointer is a tail of `OwnedContinuation`, an outer Direct whose
state is owned by a boundary node in that continuation, and a fully disjoint
parent success case; the first two fail unchanged without use-after-free or
double routing.
It validates and forwards the non-null execution context unchanged to every
frame and borrows the explicit outer boundary context for the duration of the
call, then runs one explicit `(CurrentOutcome, RemainingChain)` loop. Before
each frame apply it passes a Chain view that searches the owned remaining chain
first and then that borrowed parent; with no remaining chain it forwards the
parent directly. Success
invokes and consumes the next ordinary frame, or, when the next node is a
boundary, detaches that boundary, moves its state/outcome through `RouteMove`,
passing the same owned-remainder-plus-parent Chain view, and continues with the router's
returned outcome against the saved outer remainder. Detach yields the unique empty node token plus its separately owned
state; the router must consume both before returning. Raised/Cancelled release
consecutive ordinary success-only frames, then likewise detach and invoke the
nearest boundary; without one they forward unchanged. A TryBoundary routes
Raised into its persisted catch dispatch and forwards Cancelled after cleanup;
a HandlerBoundary performs its deep-handler routing.

For Performed, the loop transactionally splices only the consecutive ordinary
frames up to the nearest boundary into the request using their stored
transition tables. If there is no boundary it fully matches the first
remaining ordinary node, validates the named composite descriptor, relinks
the already-owned lists, and returns the request. If there is a boundary, the
loop saves the outer remainder, detaches that boundary, constructs the same
ephemeral outer Chain view with the incoming borrowed parent, and calls
`RouteMove` with its empty node token,
one owned state, the complete inner outcome, and that view. A handler router sends
Success to the return clause, Performed to the prepared dispatch, and forwards
Raised/Cancelled after region cleanup. A try router sends Success to the
protected continuation, Raised to catch dispatch, reinstalls itself before
forwarding Performed, and forwards Cancelled after cleanup. An unhandled
handler dispatch result has already installed a fresh handler boundary around
its nested request before it can leave the router.

Crucially, every router result re-enters the same loop with the saved owned
outer remainder; `RouteMove` never receives or owns that remainder or the
borrowed ambient parent. The parent is lookup-only and is never used to route
the returned outcome; the caller outside this resume still owns that routing.
Thus a handler
router's Raised reaches an outer TryBoundary, a catch/success/clause result
bypasses only the boundary just consumed, and a router-produced Performed can
still acquire outer ordinary frames and boundaries. The loop stops only when
no node remains or an unbounded Performed request has absorbed the remaining
ordinary frames. Every valid ordinary-frame splice and detached try-boundary
reinstall is infallible and allocation-free; a handler router may execute its
separately checked boundary/resume transaction. A transition-table miss or
corruption discovered only after a frame/router outcome has advanced the loop
takes a committed defensive cleanup path: it releases that outcome and every
remaining chain/request owner, writes a reserved descriptor diagnostic to the
output, and returns true without fabricating a descriptor. The final Success
is the continuation outcome. Generated lowering recognizes and releases that
reserved diagnostic, then traps; it never forwards it as a language Raised
outcome. `ResumeContinuationRuntime::Failure` models only the false preflight structural return
with the continuation/resume Consume slot unchanged;
`ConsumedFailure` models every true reserved diagnostic after both have followed
the callee-owns commit. Ownership/cleanup never merge those post-states. OOM after a valid ordinary splice or try reinstall begins is
impossible because those operations only relink owned nodes. Runtime tests cover multi-frame success,
raise/cancel, nested perform and splice, mismatched adjacent descriptors,
linear resume values, and release without resume. Nested-splice tests use a
forced hash collision, a wrong/missing composite table entry, and an allocator
that fails after frame invocation in an ordinary/try-only chain to prove that
a valid splice/reinstall performs no allocation and publishes exactly the
compiler-emitted composite type. Add
HandlerBoundary -> outer TryBoundary with the handler router producing Raised,
TryBoundary -> outer HandlerBoundary with the catch producing Performed, and a
boundary-router-produced Performed that crosses two outer ordinary frames;
each must visit only the remaining outer chain and balance every boundary
state exactly once.
Add mirrored synchronous cases `outer TryState -> inner Handler -> clause
resume` and `outer HandlerState -> inner Try/Handler -> resume`: the outer
linear field exists only in the incoming Direct parent, the raw chain contains
only inner nodes, the resumed suffix's pinned reborrow succeeds, and lookup
observes each boundary identity once without inserting a duplicate Direct
entry. Missing/foreign context, nested Performed, and abandonment variants
must preserve the same owner counts.
Add one false storage/alias resume case that retains the continuation and
Consume resume slot for `Failure`, and one true table-corruption diagnostic
case that consumes both before `ConsumedFailure`; each keeps a third linear
owner live and proves its distinct cleanup block balances all owners once.
Add a dynamic/imported callee row containing two operations plus two sites for
the same operation with different exact suffix rows; both sites must use the
one boundary-normalized current type, while the transition table selects the
distinct operation inputs and exact caller-boundary result types.
Add exact `try (perform E.op ()) catch X -> handled end` coverage where an
outer handler resumes the request and the resumed suffix raises `X`; the
enclosing catch must run. Also cover nested tries, unmatched re-raise, a
perform inside the catch escaping to an outer handler, cancellation, cleanup,
and abandonment without duplicating or leaking try state.

`TakePayloadMove` is the synchronized one-shot ownership exit used only after
a handler clause has matched. `OwnedRequest` must be non-null and hold a
request; `ArgumentCount` must equal the stored count;
the argument array is null only for zero count and otherwise contains only
Empty values; `EmptyContinuation` is non-null and contains an Empty
`YonaAbiValue`; `EmptyFailure` is non-null and Empty; its fixed-size argument slots are pairwise distinct by
construction; and the whole argument-array range is byte-disjoint from the
request-owner, continuation, and failure slots. It validates the whole destination first, then atomically
moves all argument owners and the already-typed continuation value, releases the drained request,
and clears its owner slot while leaving failure Empty. Structural validation,
alias, count, or already-taken failure leaves request, outputs, and failure
unchanged; structurally valid request corruption writes the reserved diagnostic
without moving a payload. Borrowed pointers remain valid only
until the take or another mutating request operation. A source
Borrow parameter is represented inside the request by the stage's Shareable
owned clone. Effect preparation permits that escape only when the Borrow
parameter's structural type is statically `ALWAYS_SHAREABLE`; it rejects an
instance-sensitive type before request IR exists, so runtime clone failure is
an ABI invariant rather than source-dependent control. A source Consume
parameter is its moved owner. Releasing
an untaken request releases every payload owner; a successful take transfers
all of them before destroying the empty request.
Operation equivalence uses the fingerprint only as a prefilter and then
requires equal canonical-byte lengths/content. Those immutable bytes contain
the fully qualified operation identity, every concrete type/effect
instantiation argument (including phantom binders), every parameter
type/ownership, result type/ownership, and residual effect row, using Task 2's canonical graph encoding; the
field-by-field descriptor checks remain Debug assertions against corrupt
emission rather than a second identity algorithm. Tests force the same stored
runtime fingerprint for different closed operation FQNs with otherwise
identical signatures and require rejection.
If request creation fails, its C API writes the exact nonallocating
`YONA_ABI_FAILURE_OUT_OF_MEMORY` or
`YONA_ABI_FAILURE_DESCRIPTOR_MISMATCH` diagnostic while leaving every staged
owner precommit-unchanged. Generated control-outcome lowering releases that
diagnostic, sends the still-owned arguments/continuation through the current
cleanup edge, and traps; it does not return a language Raised outcome.
Add the same exact size/offset/ABI-version assertions and reserved-field
validation used by the other public descriptor records.
Runtime tests cover zero/one/many arguments, a non-Shareable linear Consume
argument, compiler rejection of an instance-sensitive Borrow escape plus
public-C precommit rejection of a non-Shareable Borrow, validation/alias
failure with unchanged inputs and exact failure code, repeated take, release
before take, and release after take with exact owner counts.
They also require `YonaRuntimeAbiValueClone` to reject a Performed request,
reject a forged retained-alias mutation unchanged, and prove append/take are
race-free unique-owner operations.
Add handler-boundary tests for inner Success, Raised/Cancelled, a handled
nested perform, an unhandled nested perform resumed by an outer handler, a
forced transition collision, and release without resume. Two non-Shareable
router fields must each move or release exactly once on Success versus
Performed paths.

- [ ] **Step 4: Prepare and closure-convert the complete function set**

Add:

```cpp
using HandlerRouterStateDescriptorId =
    StrongId<struct HandlerRouterStateDescriptorIdTag>;
using TryStateDescriptorId =
    StrongId<struct TryStateDescriptorIdTag>;
struct BoundaryPinnedCaptureRecipe {
  ContinuationBoundaryDescriptorId Boundary;
  std::uint32_t FieldIndex;
  std::vector<Projection> ReborrowPath;
};
struct ContinuationSegment {
  enum class Kind { SourceEntry, OrdinaryJoin, ChainedFrame } SegmentKind;
  ContinuationSegmentId Id;
  FunctionId SourceFunction;
  BlockId Entry;
  std::uint32_t FirstInstruction;
  std::optional<ValueId> DynamicParameter;
  struct SuspensionCapture {
    enum class Kind { MoveOwner, RetainShareable, ReborrowFromOwner,
                      BoundaryPinnedReborrow } CaptureKind;
    ValueId Value;
    std::optional<ValueId> Owner;
    std::vector<Projection> ReborrowPath;
    std::optional<BoundaryPinnedCaptureRecipe> BoundaryRecipe;
  };
  std::vector<SuspensionCapture> CapturedEdgeState;
  model::EffectRowId SemanticEffects;
  SourceRange Range;
  FunctionId PreparedFunction;
};
struct SuspendWithContinuation {
  OperationReference Operation;
  std::vector<ValueId> Arguments;
  ValueId ContinuationFrame;
  model::TypeId InitialContinuationType;
};
struct ForwardCallableOutcome {
  ValueId Callable;
  std::vector<ValueId> Arguments;
  ValueId BoundaryContext;
};
using PreparedCallTarget = std::variant<FunctionId, ValueId>;
struct ContinuationTransition {
  OperationReference Operation;
  model::TypeId CurrentContinuationType;
  model::TypeId ResultingContinuationType;
};
struct ContinuationTransitionTable {
  ContinuationTransitionTableId Id;
  std::vector<ContinuationTransition> Entries;
};
struct BoundaryInvocationOverlay {
  ContinuationBoundaryDescriptorId Boundary;
  ValueId BorrowedUniqueState;
  ValueId ParentBoundaryContext;
};
struct InvokeWithContinuation {
  PreparedCallTarget Callee;
  std::vector<ValueId> Arguments;
  ValueId ContinuationFrame;
  ContinuationTransitionTableId Transitions;
  ValueId BoundaryContext;
  std::optional<BoundaryInvocationOverlay> SuccessFrameOverlay;
};
struct ResumeContinuationOutcome {
  ValueId Continuation;
  ValueId Argument;
  ValueId BoundaryContext;
};
struct HandlerBoundaryDescriptorPlan {
  HandlerBoundaryDescriptorId Id;
  ControlRegionId Region;
  model::FunctionDeclarationIdentity Identity;
  HandlerRouterStateDescriptorId RouterStateDescriptor;
  model::TypeId InnerResultType;
  model::TypeId ResultType;
  model::EffectRowId SemanticEffects;
  std::optional<RuntimeEffectRowId> RuntimeEffects;
  FunctionId RouteFunction;
};
struct HandlerRouterStatePlan {
  HandlerRouterStateDescriptorId Id;
  ControlRegionId Region;
  model::TypeId StateType;
  FunctionId DropFunction;
  std::vector<LexicalBindingValue> Captures;
  FunctionId SuccessFunction;
  FunctionId DispatchFunction;
  FunctionId BoundaryFunction;
  HandlerBoundaryDescriptorId BoundaryDescriptor;
};
struct TryBoundaryDescriptorPlan {
  TryBoundaryDescriptorId Id;
  ControlRegionId Region;
  model::FunctionDeclarationIdentity Identity;
  TryStateDescriptorId TryStateDescriptor;
  model::TypeId ProtectedResultType;
  model::TypeId ResultType;
  model::EffectRowId SemanticEffects;
  std::optional<RuntimeEffectRowId> RuntimeEffects;
  FunctionId RouteFunction;
};
struct TryStatePlan {
  TryStateDescriptorId Id;
  ControlRegionId Region;
  model::TypeId StateType;
  FunctionId DropFunction;
  std::vector<LexicalBindingValue> Captures;
  FunctionId ProtectedFunction;
  FunctionId CatchFunction;
  FunctionId SuccessFunction;
  FunctionId BoundaryFunction;
  TryBoundaryDescriptorId BoundaryDescriptor;
};
struct CreateHandlerRouterState {
  HandlerRouterStateDescriptorId Descriptor;
  std::vector<ValueId> Captures;
  ProducedBranchTarget Success; // one Owned router-state prefix
  RuntimeFailureDisposition Failure;
};
struct CreateTryState {
  TryStateDescriptorId Descriptor;
  std::vector<ValueId> Captures;
  ProducedBranchTarget Success; // one Owned try-state prefix
  RuntimeFailureDisposition Failure;
};
struct BorrowOwnedSlotStateUniqueInst {
  ValueId State; // Instruction::Result is same-type Borrowed unique loan
};
struct BoundaryCaptureBorrow {
  ValueId State;
  std::uint32_t FieldIndex;
  ProducedBranchTarget Success; // one Borrowed field prefix
  RuntimeFailureDisposition Failure;
};
struct BoundaryCaptureTake {
  ValueId State;
  std::uint32_t FieldIndex;
  ProducedBranchTarget Success; // one Owned or Trivial field prefix
  RuntimeFailureDisposition Failure;
};
struct BoundaryPinnedCaptureBorrow {
  ValueId BoundaryContext;
  ContinuationBoundaryDescriptorId Boundary;
  std::uint32_t FieldIndex;
  ProducedBranchTarget Success; // exact Borrowed field prefix
  RuntimeFailureDisposition Failure;
};
struct InvokeBoundaryFunctionOutcome {
  FunctionId Callee;
  std::vector<ValueId> Arguments;
  ContinuationBoundaryDescriptorId Boundary;
  ValueId BorrowedUniqueState;
  ValueId ParentBoundaryContext;
  ProducedBranchTarget Success;
  ProducedBranchTarget Raised;
  ProducedBranchTarget Performed;
  BranchTarget Cancelled;
};
struct InvokeBoundaryFrameOutcome {
  ValueId Frame;
  std::vector<ValueId> Arguments;
  ContinuationBoundaryDescriptorId Boundary;
  ValueId BorrowedUniqueState;
  ValueId ParentBoundaryContext;
  ProducedBranchTarget Success;
  ProducedBranchTarget Raised;
  ProducedBranchTarget Performed;
  BranchTarget Cancelled;
  RuntimeFailureDisposition Failure; // false: frame/Consume args unchanged
  RuntimeFailureDisposition ConsumedFailure; // true reserved diagnostic
};
struct TestEffectRequestOperation {
  ValueId Request;
  OperationReference Expected;
  BranchTarget Matches;
  BranchTarget DoesNotMatch;
  RuntimeFailureDisposition Failure;
};
struct ValidateEffectRequestArgumentCount {
  ValueId Request;
  std::uint64_t Expected;
  BranchTarget Success;
  RuntimeFailureDisposition Failure;
};
struct BorrowEffectRequestArgument {
  ValueId Request;
  std::uint64_t Index;
  ProducedBranchTarget Success; // one exact Borrowed argument prefix
  RuntimeFailureDisposition Failure;
};
struct BorrowEffectRequestContinuation {
  ValueId Request;
  ProducedBranchTarget Success; // one exact Borrowed continuation prefix
  RuntimeFailureDisposition Failure;
};
struct CreateEffectRequestRuntime {
  OperationReference Operation;
  std::vector<ValueId> Arguments;
  model::TypeId InitialContinuationType;
  ValueId InitialContinuationFrame;
  ProducedBranchTarget Success; // one Owned EffectRequest prefix
  RuntimeFailureDisposition Failure;
};
struct AppendContinuationFrameToRequest {
  ValueId Request;
  ValueId OuterFrame;
  ContinuationTransitionTableId Transitions;
  ProducedBranchTarget Success; // one Owned updated-request prefix
  RuntimeFailureDisposition Failure;
};
struct TakeEffectRequestPayload {
  ValueId Request;
  OperationReference Operation;
  ProducedBranchTarget Success; // arguments then Owned continuation prefix
  RuntimeFailureDisposition Failure;
};
struct ResumeContinuationRuntime {
  ValueId Continuation;
  ValueId Argument;
  ValueId BoundaryContext;
  ProducedBranchTarget Success;
  ProducedBranchTarget Raised;
  ProducedBranchTarget Performed;
  BranchTarget Cancelled;
  RuntimeFailureDisposition Failure; // false: inputs unchanged
  RuntimeFailureDisposition ConsumedFailure; // true reserved diagnostic
};
struct ReleaseDetachedBoundaryAndBranch {
  BranchTarget Next;
};
struct ReinstallDetachedTryBoundaryAndReturnPerformed {
  ValueId Request;
  ValueId TryState;
};
struct MakeHandlerResume {
  ValueId RawContinuation;
  OperationReference Operation;
  ValueId RouterState;
  FunctionId ResumeFunction;
  HandlerBoundaryDescriptorId Boundary;
  ContinuationTransitionTableId Transitions;
  ProducedBranchTarget Success; // one Owned resume-callable prefix
  RuntimeFailureDisposition Failure;
};
struct InvokeHandledBodyOutcome {
  ControlRegionId Region;
  FunctionId BodyFunction;
  std::vector<ValueId> Arguments;
  ValueId RouterState;
  ValueId BorrowedUniqueState;
  ValueId ParentBoundaryContext;
};
struct InvokeProtectedTryOutcome {
  ControlRegionId Region;
  FunctionId ProtectedFunction;
  std::vector<ValueId> Arguments;
  ValueId TryState;
  ValueId BorrowedUniqueState;
  ValueId ParentBoundaryContext;
};
struct PreparedRequestTake {
  ControlRegionId Region;
  BlockId Selection;
  ValueId Request;
  OperationReference Operation;
  std::vector<ValueId> TakenArguments;
  ValueId TakenContinuation;
  ValueId RouterState;
  FunctionId ResumeFunction;
};
struct PreparedHandlerGroup {
  OperationReference Operation;
  BlockId MatchEntry;
  FunctionId ResumeFunction;
};
struct PreparedHandlerDispatch {
  ControlRegionId Region;
  ValueId EffectRequest;
  ValueId RouterState;
  std::vector<PreparedHandlerGroup> Groups;
  BlockId UnhandledEntry;
};
struct AppendHandlerBoundaryToRequest {
  ValueId Request;
  ValueId RouterState;
  HandlerBoundaryDescriptorId Boundary;
  ContinuationTransitionTableId Transitions;
  ProducedBranchTarget Success; // one Owned updated-request prefix
  RuntimeFailureDisposition Failure;
};
struct AppendHandlerBoundaryToContinuation {
  ValueId Continuation;
  OperationReference Operation;
  ValueId RouterState;
  HandlerBoundaryDescriptorId Boundary;
  ContinuationTransitionTableId Transitions;
  ProducedBranchTarget Success; // one Owned updated-continuation prefix
  RuntimeFailureDisposition Failure;
};
struct AppendTryBoundaryToRequest {
  ValueId Request;
  ValueId TryState;
  TryBoundaryDescriptorId Boundary;
  ContinuationTransitionTableId Transitions;
  ProducedBranchTarget Success; // one Owned updated-request prefix
  RuntimeFailureDisposition Failure;
};
struct AppendTryBoundaryToContinuation {
  ValueId Continuation;
  OperationReference Operation;
  ValueId TryState;
  TryBoundaryDescriptorId Boundary;
  ContinuationTransitionTableId Transitions;
  ProducedBranchTarget Success; // one Owned updated-continuation prefix
  RuntimeFailureDisposition Failure;
};
PassResult runEffectPreparation(Module &);
```

All checked Borrow-producing access forms use Task 6's provenance derivation.
`BoundaryCaptureBorrow` propagates the ultimate
`ScopedUniqueLoan` kind/root for a caller-local loan and the callee-local
`ScopedUniqueLoanParameter` kind/root for a prepared body's hidden loan
parameter, or
roots at that exact Owned state for an Owned operand; it never roots at an
intermediate Borrowed ValueId. `BorrowEffectRequestArgument` and
`BorrowEffectRequestContinuation` produce `OwnedValue` provenance rooted at
their dominating Owned Request. Their Success block arguments must preserve
that exact root, and ownership/cleanup verification keeps the root live and
unmutated until the final borrow use. `BoundaryPinnedCaptureBorrow` alone uses
the boundary-parameter provenance specified below.

Task 13 extends `FunctionEntryKind::OutcomeRouter`'s fixed ABI with the leading
hidden `OwnedDetachedBoundary` slot shown in `YonaOutcomeRouterEntry`.
`RouteInnerOutcome` consumes that non-null runtime-owned token, the first
ordinary Consume state parameter, and the hidden inner outcome. The token is
an implicit linear router capability, never a source type, ordinary `ValueId`,
descriptor child, or captured value. Every router path must end that capability
exactly once with `ReleaseDetachedBoundaryAndBranch`, or—only for a resumed try
whose inner outcome is Performed—with
`ReinstallDetachedTryBoundaryAndReturnPerformed`. The former calls the null-
safe release helper before entering `Next`; the latter moves the try state back
into the token, relinks it to the owned request, and writes/returns Performed in
one infallible terminator. The mandatory structural-failure cleanup is the
only pre-routing exception: because validation has not read/moved an input, it
releases the still-owned detached token, state, inner outcome, and diagnostic
before its nonreturning trap. Initial handled-body and protected-try invocations
use their prepared `Invoke*` records and lower to
`InvokeBoundaryFunctionOutcome` with ordinary four-way routing; they never
enter `OutcomeRouter`, because no detached token exists yet. Parser/printer,
successor and implicit-operand
visitors, phase verifier, cleanup verifier, and LLVM block lowering model this
hidden capability explicitly even though it is not in the general SSA arena.

`BoundaryPinnedCaptureBorrow` is the only continuation-frame access to a
borrow that originated in a handler/try state. Its boundary-context operand
must equal the frame's hidden dominating `BoundaryContextParameter`; lowering
reads only that distinct ephemeral argument, searches the saved
remaining chain for the full generated boundary identity (fingerprint is only
a prefilter), verifies the exact owned-state descriptor and nonempty field, and
produces one field Borrow with
`BorrowProvenance{BorrowParameter,
Root = Function.BoundaryContextParameter}` valid only for the current frame
invocation. Boundary identity/state-descriptor matching remains an opcode and
verifier fact, not an unrepresentable provenance owner. The
frame function's `SuspensionCapture::BoundaryRecipe` stores the boundary ID,
field ordinal, and subsequent structural projection recipe. Effect preparation
emits `BoundaryPinnedCaptureBorrow` for the field and then replays that exact
projection path in the frame body; neither the callable descriptor nor the
runtime callable instance stores a state pointer, raw field pointer, state
owner, or generic Borrow environment capture. The recipe's boundary ID resolves
to one immutable boundary plan whose `Identity` is exactly the generated route
function's deterministic `LocalFunctionIdentity`: the enclosing declaration
path followed by the boundary-kind discriminator and lexical `ControlRegionId`
ordinal. Descriptor emission embeds its complete canonical identity bytes and
fingerprint. A missing/wrong
boundary, empty slot, alias violation, or malformed context leaves output
Empty, reports the reserved diagnostic, and takes `TrapCompilerFailure`; a
semantic absence is impossible. When a frame suspends again it records the
same static recipe in the new frame and stores no live borrow. The resume loop
destroys each invoked frame before detaching the boundary that owns the state,
so every produced borrow ends before owner movement.

Exactly `BoundaryPinnedReborrow` requires `BoundaryRecipe` and forbids
`Owner` plus the top-level `ReborrowPath`; `ReborrowFromOwner` requires Owner
and uses the top-level path, while the other capture kinds require neither.
Effect preparation creates the frame direct entry's hidden
`BoundaryContextParameter` before emitting `BoundaryPinnedCaptureBorrow`;
closure conversion preserves this parameter separately and never creates or
reuses a `CallableEnvironmentParameter` merely for a pinned borrow. It is
required even when the frame has no stored environment fields, and its only
legal query use is the checked pinned borrow.
The parser, printer, clone/remap visitors, free-variable analysis, continuation
outliner, phase verifier, and cleanup/ownership verifier preserve this tagged
shape. A nested aggregate-field regression suspends with a boundary field's
subprojection live, resumes it twice, and proves the emitted checked boundary
borrow is followed by the identical projection path each time.

The same frame must also work when the inner operation/call succeeds without
ever installing a boundary. If the containing function's verified ambient
boundary-context contract already satisfies every frame recipe—including the
current Direct context supplied to an initial body/protected function—lowering
uses ordinary `InvokeOutcome` and forwards that exact parameter. It emits
`InvokeBoundaryFrameOutcome` only when the required local boundary is absent
from the ambient contract but the caller separately owns and uniquely loans
the matching state. Its `BorrowedUniqueState` is that still-live scoped loan;
lowering initializes one Direct boundary context on the stack, sets its parent
to the record's exact `ParentBoundaryContext`, and passes it to
`YonaRuntimeCallableApplyMove`. The verifier forbids overlaying a boundary
identity already supplied by the ambient context, so Direct(current) can never
become Direct(current) -> Direct(current). The frame's pinned query therefore
finds the local boundary identity/state exactly once or any live enclosing
entry on the immediate Success path. The context and its borrowed parent link end
before routing the returned outcome or moving the state. On a Performed path
the frame is instead stored in the request, and runtime continuation resume
passes a Chain context over the installed node. Both paths use the same
immutable frame function and recipe. This terminator otherwise has the exact
callable `InvokeOutcome` four-way and false-before-commit/true-after-commit
contracts, and only a frame with at least one pinned recipe may use it. Tests
cover immediate Success, immediate Raised/Cancelled, initial Performed then
resume, a suffix that itself performs, and nested synchronous outer+inner
state borrows, proving no normal path observes a null/missing boundary
context.

Task 13 extends `Module` with checked, module-owned
`std::vector<PreparedRequestTake>` and
`std::vector<ContinuationTransitionTable>` plus
`std::vector<HandlerRouterStatePlan>` and
`std::vector<HandlerBoundaryDescriptorPlan>`,
`std::vector<TryStatePlan>`, and
`std::vector<TryBoundaryDescriptorPlan>` arenas; records are keyed by region/selection and
every `PreparedRequestTake` output ID is a predeclared argument of its
`Selection` block with `ValueOrigin::BlockArgument`, never a result defined in
the predecessor. Effect finalization replaces the planning record with one
`TakeEffectRequestPayload` terminator. Its `ProducedBranchTarget` prefix
matches only the taken arguments followed by the continuation. The already-
owned `RouterState` is transferred as the explicit successor-argument suffix;
the C take does not produce or mutate it. On false both request and router
state remain owned for cleanup; on success the request is consumed and the
router-state owner transfers exactly once. Tests force take failure with a
live linear router state and compare it with the successful selected-clause
path.
All four append operations and both state creations are prepared terminators with Success/failure
successors; operation instantiation rewrites their operation/row fields and
fills the boundary `RuntimeEffects`, and effect finalization lowers them to the
four C APIs without creating metadata. The printer/parser, operand visitor,
free-variable analysis, remapper, and phase verifier handle these arenas and
operations exhaustively. Boundary descriptor records survive region erasure
through `LlvmReady`, as do their independently keyed owned-state descriptors,
so LLVM can emit both immutable C records. Finalization rewrites every
region-only diagnostic link to these stable IDs before erasing `HandleRegion`;
no surviving instruction resolves layout through a `ControlRegionId`.

`runEffectPreparation` consumes `CleanupPrepared` and produces
`EffectPrepared`. It is the only pass that creates effect-generated
functions. It processes TryRegions and HandleRegions together, innermost-first
in lexical region order.

For each TryRegion whose protected computation can perform, preparation
outlines the protected computation, persisted catch dispatch, and lexical
success continuation; predeclares one private OutcomeRouter boundary function;
and constructs one `TryStatePlan` containing the exact union of their lexical
captures and outstanding cleanup state. The original site becomes
`InvokeProtectedTryOutcome{Region, ProtectedFunction, Arguments, TryState,
BorrowedUniqueState, ParentBoundaryContext = containing
Function.BoundaryContextParameter}`; preparation
copies the exact dominating ValueId rather than looking it up later. Initial Success moves the state to
`PreparedSuccess`; initial Raised moves the same state and complete exception
to `PreparedCatch`; initial Performed appends one TryBoundary to the request
before propagation; Cancelled releases state through cleanup and forwards.
The boundary router applies those identical rules after resumption, reinstalling
itself transactionally on every nested Performed outcome. Catch and success
functions bypass the boundary, so exceptions/effects originating there cannot
be recaught by the same try. Regions proven operation-free need no runtime
boundary but still use Task 12's ordinary Raised edge. All prepared try
functions, states, descriptors, append sites, rows, and transition tables are
reserved before any block is moved; the whole transformation commits or rolls
back as one transaction.

`PreparedProtected` receives a verifier-scoped unique loan of the one owned
TryState while its caller suspends that owner; it accesses captures only with
the generic boundary-state Borrow/Take operations. `PreparedCatch`,
`PreparedSuccess`, and the OutcomeRouter each Consume that same state owner.
Any protected-body borrow live across a suspension is recorded as
`BoundaryPinnedReborrow`: its frame plan stores only the static TryBoundary ID,
field ordinal, and structural subprojection recipe. At invocation the checked
boundary-context query reconstructs the ephemeral Borrow; no state or field
address is stored in the frame. The invocation either ends every such frame
before consuming the state on Success/Raised/Cancelled or atomically moves the
state into the TryBoundary on Performed. There is no independent borrowed
capture owner.
Every route has an explicit typed state/result/request/exception block
signature, cleared-field facts travel with the owner, and a unique loan ends
before state movement or suspension. Tests move two independent linear
captures through initial Success, initial Raised, perform/resume/raise,
unmatched catch, nested Performed, Cancelled, and abandonment and prove each
slot is moved or released once.

For each HandleRegion it
outlines the exact dominated handled computation rooted at `BodyEntry`, the
return/success clause rooted at `SuccessEntry`, and every handler clause body
into top-level functions; assigns their stored semantic rows; and sets
`PreparedBody`, `PreparedSuccess`, and each clause's `PreparedFunction`. An
absent source return clause becomes an explicit identity success function.
Preparation also predeclares `PreparedEntry`, `PreparedDispatch`, one
`PreparedBoundary`, and one `PreparedResume` function for each complete
operation group; these fields are all filled before any block is moved.

Capture analysis computes one `HandlerRouterStatePlan` for the region. Its
owned managed state contains exactly one owner for the union of lexical values
needed by the body, return clause, dispatch, and handler clauses; it never
contains owning success/dispatch callables or a self-reference. The plan's
mandatory private CleanupDrop entry releases every still-present
field and is the `OwnedSlotStateType::DropIdentity`; TryState plans use the
same total rule. Prepared body,
success, dispatch, and clause functions are private direct functions with
exact hidden contracts: body receives a `BorrowOwnedSlotStateUniqueInst`
loan while entry suspends its sole owner; success and dispatch Consume the
state; a selected clause initially
Consumes the raw continuation and state separately. The entry constructs one state
and ends in the named-field form
`InvokeHandledBodyOutcome{Region, BodyFunction, Arguments, RouterState,
BorrowedUniqueState, ParentBoundaryContext = containing
Function.BoundaryContextParameter}`; preparation
copies the exact dominating ValueId rather than looking it up later.
Body Success moves the state into the prepared success function, body
Performed moves it with the owned request into `PreparedDispatch`, and
Raised/Cancelled run the state's cleanup and forward unchanged. A selected
handler clause's whole outcome and the success/return clause's whole outcome
bypass dispatch, so an operation performed lexically inside either clause is
handled only by an outer handler.

The runtime continuation chain supports one explicit, generated
`HandlerBoundary` node in addition to ordinary success-suffix frames. That
non-Shareable node owns the single router state and `PreparedBoundary`
descriptor. When the inner chain yields Success it invokes `PreparedSuccess`;
when it yields Performed it invokes `PreparedDispatch` with the same moved
state; Raised/Cancelled clean up the state and propagate. Dispatch appends
exactly one such boundary node before either propagating an unhandled request
or exposing a raw continuation as a source resume. Consequently an outer
handler resuming an unhandled operation still reinstalls this inner handler
for every later perform; an ordinary suffix frame would not be sufficient.
Appending/replacing the boundary is a transactional request operation and
never exposes a state-less interval.

Each prepared resume function has the solved source type
`OperationResult -> HandleResult`, takes exactly that one dynamic resume
argument, and captures only the typed raw
`OperationResult -> HandleResult` continuation after its handler-boundary node
has been installed. Every site for the same closed operation in that handled
body therefore has the same capture field type even when its exact
`ResumeEffects` suffix differs. It ends in
`ResumeContinuationOutcome{Continuation, Argument, BoundaryContext =
containing Function.BoundaryContextParameter}`; the boundary performs all
deep routing and the explicit context remains the lookup-only outer tail.
Resumptions are deliberately one-shot:
the raw continuation capture and boundary state are non-Shareable, the resume
callable is consequently linear, and ownership verification rejects a second
call or escape requiring a clone. Clause code may borrow its lexical router
environment while that resume owner is live, but a Consume/escape of the
resume is a borrow-ending sink: later use of a non-Shareable router capture is
a ranged linearity error. Shareable captures may be retained explicitly. The
original handle site becomes invocation of `PreparedEntry`; its typed Success
feeds the original `Continuation`, while residual Performed propagation
receives the outer continuation frame during segment normalization below.

A nonempty `CallableBoundaryRequirements` set is a scoped compile-time
capability, not a request for runtime trial-and-error. It may flow through
same-function SSA/block arguments and may be consumed by an exact synchronous
Apply/resume only when the containing Function guarantee covers the full set.
Passing it through a statically resolved helper is legal only when that
parameter carries the same conservative set and interprocedural escape
analysis proves a synchronous noescape chain whose every entry contract
covers it. It is otherwise forbidden to return the value; store it in an
aggregate, global, or heap slot; capture it in another source closure,
continuation, or cleanup obligation; partially apply, retain, or clone it;
submit it to async work; append it as a source effect-request argument; pass
it to NativeExtern/exported/imported/unknown higher-order code; or merge it
into a value whose recorded union drops an incoming requirement.

The compiler-owned effect path has two explicit checked exceptions to that
storage ban. `MakeHandlerResume` may capture the raw continuation only after
installing its boundary and assigns the produced resume value the exact
residual requirements not satisfied by that owned chain. A prepared ordinary
frame/request/boundary append may store a frame only when the final published
request chain discharges every frame requirement; no intermediate request is
source-observable or resumable. A boundary available only through the incoming
Direct/Chain parent remains in the residual set. Consequently a resume that
depends on an enclosing initial state cannot outlive that state's dynamic
extent and fail later as a boundary-capture ABI trap, while a self-contained
or requirement-free one-shot resume keeps its ordinary legal escape behavior.
Tests cover immediate consumption, an exact verified noescape helper,
mutually recursive helpers reached only under the boundary, and rejection
when one null-root/unknown incoming edge removes that guarantee. An outer-Try
pinned field feeding an inner-Handler resume must be rejected at the source
escape range when returned, stored, partially applied, submitted, or passed
to unknown code.

Add `CreateHandlerRouterState`, `CreateTryState`,
`BorrowOwnedSlotStateUniqueInst`, `BoundaryCaptureBorrow`,
`BoundaryCaptureTake`, `BoundaryPinnedCaptureBorrow`, the four checked effect-request queries, the four
low-level request/continuation runtime terminators, and
`MakeHandlerResume` to the prepared IR. Both state
Create forms, all three capture accessors, every request query/runtime form, Make,
and all append forms are
fallible terminators with explicit typed Success and failure dispositions.
Their `ProducedBranchTarget` prefix contains exactly the router state, accessed
field, resume callable, or updated owner as target block arguments. Ordinary
Borrow produces a Borrow tied to the explicit state owner; pinned Borrow is
tied to the matching boundary owner in the invocation context. Take produces the descriptor field's
exact Trivial or Owned contract. Request-operation testing has disjoint Match/
NoMatch edges after full descriptor equivalence; count validation has one
ordinary Success edge. Argument/continuation queries produce one exact Borrow
tied to the request owner. Every query's runtime diagnostic follows only its
Failure edge.

`BorrowOwnedSlotStateUniqueInst` is the one nonfallible marker in that list.
Its operand must be the dominating sole Owned state value; its ordinary
`Instruction::Result` has the identical `OwnedSlotStateType`, Borrowed
ownership, and
`BorrowProvenance{Kind = ScopedUniqueLoan, Root = State}`. It appears
immediately before `InvokeHandledBodyOutcome` or
`InvokeProtectedTryOutcome`, is passed as the callee's hidden first Borrow
parameter, and the caller may neither access nor move the owner until that
invoke returns. Those two records' `Arguments` exclude parameter zero. The
callee's internal FunctionType has exactly the Borrow OwnedSlotState loan
parameter followed by those outlined arguments, so the verifier requires
`Arguments.size() + 1 == Parameters.size()` with exact types/ownerships. The
special invoke edge converts the caller's owner-rooted loan into the callee's
rootless `ScopedUniqueLoanParameter`; no foreign caller ValueId crosses the
FunctionLocalDomain and no other call can accept it. LLVM call order is loan,
outlined arguments, hidden BoundaryContext, then—for ExplicitOutcome only—
ExecutionContext and Outcome. In representation
and LLVM lowering it is the same pointer-sized carrier (a checked no-op), emits
no C call, and ends at the invoke return. Parser/printer/remapping and phase,
SSA, ownership, cleanup, representation, and LLVM visitors handle it
explicitly. Tests reject a second live loan, an ordinary-call escape, state use
during the loan, and Take through a non-unique Borrow, and prove the valid
marker emits no retain/release/runtime call.
PreparedBody/PreparedProtected entries carrying this parameter are internal
direct-only functions. They cannot be imported/exported, referenced by
MakeFunction/MakeClosure, assigned a `CallableDescriptor`, or receive a
universal adapter; closure conversion analyzes their bodies but creates an
adapter only for callable-reachable functions. A verifier case forges such an
adapter and must fail before function-set freeze.
For each initial `InvokeHandledBodyOutcome` or
`InvokeProtectedTryOutcome`, control-outcome lowering resolves the region to
its stable boundary ID and emits `InvokeBoundaryFunctionOutcome`; it does not
materialize a general context-pointer SSA value. That low-level terminator
carries the direct FunctionId, arguments, boundary ID, exact
`BorrowedUniqueState`, parent context, and four typed successors. LLVM alone
constructs the stack Direct record and passes it as the prepared body/protected
entry's hidden boundary parameter. The
verifier requires the caller loan's `ScopedUniqueLoan` root to equal the record's
RouterState/TryState and the parent to equal the containing function's exact
hidden parameter. This lookup view does not install a continuation boundary or
route the returned outcome; it lives only until the synchronous call returns,
then ordinary initial Success/Raised/Performed/Cancelled routing owns the
state transition. Consequently a synchronously resumed inner handler can
still find an enclosing initial boundary that exists only as this Direct view.
Because every one is compiler-internal state/request construction or access,
its
disposition is always `TrapCompilerFailure`: lowering releases the C diagnostic
outcome, preserves or cleans the owners promised by the runtime transaction,
and ends in the non-returning invariant trap. They cannot bind an ExceptionValue
or forward a runtime-false diagnostic as a source exception. No
`ControlOutcome` becomes SSA; only the explicit produced-prefix mechanism
introduces edge results. Handler-router and try state are non-Shareable
managed slot arrays; Borrow leaves a slot intact, Take moves/clears a slot and
requires its unique owner or verifier-issued unique loan, and state destruction
releases every nonempty slot. A protected try body or handled body may
therefore consume a linear capture;
an active direct loan must end before suspension/movement. A borrow needed by
the resumed suffix is replaced before suspension by its static pinned recipe,
and the cleared-field fact continues only with the owner.
`MakeHandlerResume` transactionally consumes the clause's raw continuation
and router state, installs the handler boundary, and creates the source resume
callable. It is emitted at the first owning use of the lexical resume binding,
not unconditionally at clause entry. Thus a clause that never resumes may use
or Consume its captures and then releases the raw chain/state; a clause that
creates, passes, or calls resume has transferred the state and cannot later
use a non-Shareable capture. Branches may choose either ownership path with
ordinary SSA transfers. The verifier rejects two resume constructions, a
state use after construction, taking without an owner/unique loan, or a router
loan live across movement. Tests use two linear captures across
body-success, handled/unhandled perform, a no-resume clause, and a conditional
one-shot resume and check every slot exactly once. ABI and lowering tests also
force a corrupt descriptor/index failure for Borrow and Take with unrelated
live owners: outputs remain empty, state is unchanged, generated cleanup
releases the diagnostic and every owner once, and control ends in the invariant
trap.

All BodyEntry/SuccessEntry/clause blocks, values, match plans, cleanup/control
records, and ranges are rehomed exactly once before closure conversion. There
is no original-to-entry/dispatch branch or foreign ValueId. The retained HandleRegion is prepared
metadata linking those functions and is erased only by finalization. Nested
handlers, a handled perform followed by outer caller work, return clauses, and
body success without perform have exact extraction tests; the verifier rejects
an undominated body block, an external predecessor, or any cross-function
reference.
Group clauses in source order by complete semantic `OperationUse` equality
(qualified key plus ordered concrete type/effect arguments). For each group,
create a dispatch-local entry whose typed block arguments are the operation
argument borrows and the original request; finalization will decode and
branch to it after descriptor selection. Remap each argument MatchPlan and
its guard/binding blocks into this dispatch function, replace successful bodies
with dispatch-local selection blocks, and call
`lowerDecisionRoot` on the typed group arguments with a `BranchFailure` to
the next clause. The last candidate branches to the dispatch-local unhandled
block, which transactionally installs the region's one owned handler-boundary
node and forwards the same request/argument payload unchanged.

Persist that topology in `PreparedHandlerDispatch`: `Groups` contains one
unique complete operation reference in first-source-occurrence order and its
dispatch-local `MatchEntry` plus its predeclared `ResumeFunction`;
`UnhandledEntry` is the final dispatch-local forwarder. A
group entry has exactly N+2 block arguments for operation arity N: argument i
has the substituted operation type and is Trivial for a Trivial parameter or
Borrowed for Borrow/Consume, followed by one Owned builtin EffectRequest. The
last argument is the one Owned router state. The unhandled entry has exactly
the Owned request and Owned router-state arguments. The dispatch
request, every target, and every clause belong to
`HandleRegion::PreparedDispatch`; every clause occurs in exactly one group, and
all group failure edges stay in that function. No cross-function target or
detached match pseudo-operation is permitted.

Decision tests, projections, bindings, and guards borrow from the still-owned
request; they never clone, retain, or move an argument. Before closure
conversion, each successful selection block owns one `PreparedRequestTake`
record. The record predeclares exact typed `ValueId`s for every taken argument
and for the raw continuation plus the incoming one Owned router state. It
passes those owners as the selected clause's hidden parameters; that clause's
predeclared `MakeHandlerResume`, if reached, installs the boundary and
creates the normal `MakeFunctionInst` of the group's prepared resume function.
The resume function lexically references only the boundary-wrapped continuation.
Free-variable analysis scans this module-owned record, so the single closure pass
freezes the resume callable descriptor and environment layout before the
function/descriptor set closes. Finalization may materialize that already-
declared take and closure instance, but cannot invent a function, descriptor,
capture field, or value.

The materialized selection calls
`YonaRuntimeEffectRequestTakePayloadMove` exactly once, consuming and clearing
the request owner, and constructs descriptor-matching `YonaAbiArgument`
records over the taken owner array. Trivial/Borrow records point at their
taken carriers; Consume records point at mutable owner slots. The typed
API requires the request owner, each exactly sized initially Empty argument
slot, the initially Empty continuation slot, and an initially Empty failure
outcome to be pairwise byte-disjoint. It validates the request descriptor,
argument count/types, continuation type, and every destination before commit.
Structural rejection writes nothing; a structurally valid corruption writes
the reserved diagnostic. Either false result leaves the request and all result
slots unchanged, while true atomically clears the request, initializes every
payload result, and leaves the failure Empty. The prepared terminator's failure
disposition is mandatorily `TrapCompilerFailure`.
The typed
non-Shareable continuation and router state move into the prepared clause's
two hidden Consume parameters; no descriptor is fabricated at the handler
site. The clause otherwise receives the closed operation declaration's exact
Trivial/Borrow/Consume arguments. Its first owning use of the source resume
binding executes the already-declared `MakeHandlerResume` and yields the
linear callable of stored `OperationResult -> HandleResult` type. Its complete outcome is
emitted through `ForwardCallableOutcome`, so clause-local Performed outcomes
bypass this dispatch. After the call, lowering releases every carrier still
present in the taken array, including owned pins backing semantic Borrow
parameters. The statically proved ApplyMove storage contract must return true;
a defensive false releases all still-owned taken arguments, raw continuation,
router state/resume/clause callables and the reserved diagnostic, then runs
cleanup and traps. The prepared body
begins with a no-test binding
prologue derived from the already-selected MatchPlan, materializing its body
bindings from those owned inputs under Task 11's aggregate ownership rules;
it never reruns tests or the guard. A false pattern/guard takes nothing. The
final unhandled path preserves the same owned operation and staged arguments
without taking or fabricating either, then transactionally installs a fresh
deep handler boundary around that request before outward propagation. An
unexpected transactional-take failure releases the unchanged request and the
exact nonallocating descriptor-mismatch diagnostic, runs cleanup, and traps;
partial payload movement is unrepresentable.

It then performs one finite whole-CFG continuation normalization over a
deterministic FunctionId worklist containing every original function and every
prepared protected-try, catch, try-success, try-router, handler body, success,
entry, dispatch, handler-router, handler-clause, and resume function.
It never moves an arbitrary graph-reachable suffix.
First split blocks at every possibly-performing call and every Perform resume
program point, then form maximal straight-line segments. Before predeclaring a
function, compute one `ContinuationSegment` row for every segment by a backward
fixed point over the original CFG. Instruction transfer reads the exact
callee Function/FunctionType structural row, Perform uses its stored
`ResumeEffects`, branches union successor rows, and HandleRegion applies its
complete application mask; raise/cancel facts propagate with the same ACI
solver. No name lookup or LLVM/runtime inference occurs. Cross-check the entry
row against the containing Function/HandlerClause/HandleRegion semantic facts
and reject disagreement transactionally. Shared predecessors name the same
segment/row, and loop SCCs converge by canonical ACI interning. Persist the
segment records until finalization so closure conversion, operation
instantiation, and verifiers all consume the same row.

Reserve exactly one `FunctionId` per segment before moving any body, in
original function/block/instruction order. The source entry segment reuses its
original `FunctionId`, linkage, symbol, and declared signature; every non-entry
segment is a private prepared function. No unused wrapper or duplicate linked
entry remains. The complete segment graph may contain shared joins and SCCs:
every predecessor references the same reserved join function, and loop/
backedge SCCs become finite mutually recursive segment functions rather than
recursive cloning.

Suspension liveness never captures a bare Borrow. A Trivial carrier copies; a
Shareable managed value may retain an explicit owner; otherwise the analysis
must find a dominating Owned provenance value plus a deterministic projection
path and records `ReborrowFromOwner`. The frame captures that owner and
re-executes the projection on entry. A non-Shareable Borrow parameter with no
available owner is a ranged suspension-linearity error. The sole specialized
`BoundaryPinnedReborrow` is reserved for a field of the state loaned by
`InvokeHandledBodyOutcome` or `InvokeProtectedTryOutcome`. The invocation
carrier owns the stable HandlerRouterState/TryState while the body runs, and on
Performed atomically transfers that owner into the corresponding boundary
before the request may escape. Its raw body frames store only the static
boundary ID, field ordinal, and structural subprojection recipe and never a
state/field address or owner; on invocation the runtime supplies an ephemeral
view of the still-remaining continuation chain and the generated frame executes
`BoundaryPinnedCaptureBorrow` before replaying the path. Request release, take, matched
resume, unhandled propagation, catch/success routing, and every failure order
raw-frame destruction before the state owner. No generic callable may use this
capture kind. Tests use a linear captured field after perform in both a handled
body and a protected try body, then cover try Success/Raised/Cancelled and
abandonment with one balanced owner.

The same provenance rule handles effectful handler guards. Their suffix frame
moves the original Owned request and router state, then regenerates every
argument/projection borrow after resume; it never captures a payload pointer.
If the guard succeeds without performing, ownership returns along the local
edge; if it performs, that owner-bearing suffix is attached to the nested
request before propagation. Tests cover a Consume operation argument with a
performing guard that resumes into the next clause, a handled body that uses a
linear capture after `perform`, and rejection of an ownerless non-Shareable
Borrow across a perform or possibly-performing call.

A `ChainedFrame` has exactly one declared dynamic parameter: the operation or
callee result with its exact ownership. Every additional `ResumeTarget` block
argument, threaded owner, and live-in becomes a deterministic closure capture
sorted by BindingId then ValueId. A `SourceEntry` retains the original source
arity, and an `OrdinaryJoin` may retain multiple explicit block arguments, but
neither may be inserted into a runtime continuation chain. When a shared join
is also a suspension target, preparation uses its single-argument
`ChainedFrame` form and captures the trailing edge arguments at each closure
construction site. The verifier rejects any chain frame with arity other than
one. Every original block, value,
instruction, match/control/cleanup record, debug range, region edge, and source
range is rehomed and ID-remapped exactly once. Ordinary inter-segment edges
invoke the target frame and forward its complete outcome. A `Perform`
materializes the reserved ResumeTarget frame with its result parameter and
captured trailing edge state, then becomes `SuspendWithContinuation` holding
the open `OperationUse`, exact-suffix frame, and separately normalized initial
continuation FunctionType. A direct
or dynamic call whose semantic row may perform becomes
`InvokeWithContinuation`, carrying the reserved caller-suffix frame and the
complete boundary-normalized transition table derived from the callee's
closed row and caller boundary. A
`Resume` becomes `ResumeContinuationOutcome` over the closed
Continuation-convention FunctionType value and the containing function's
exact dominating boundary-context parameter. Calls proven operation-free remain
ordinary and Task 12 handles their
raise/cancel outcomes.

Continuation edges transfer, rather than exit, active cleanup obligations.
Effect preparation packs each live suffix into Task 10's one armed
`CleanupObligation` and moves it into the target frame; frame entry takes and
reconstitutes it before executing resumed code. A Performed outcome carrying
that frame is suspension, not a cleanup-region exit. If the request,
continuation, or source resume is abandoned, releasing the armed obligation
runs its precreated effect-free drop thunk exactly once. Raised/Cancelled and
ordinary returns after resumption instead traverse the reconstituted Task 12
cleanup edge. The verifier forbids the same logical obligation in both the
source edge and frame, requires raw-frame release before any externally pinned
router state it borrows, and checks
one owner segment per record, exact live-in dominance, edge/signature/effect
agreement, complete segment reachability, and no reference back to a foreign
source function. Preparation builds all segment functions and rewrites in
private storage, then commits once. Tests include a perform feeding a shared
if/else join, a perform inside a loop with a backedge, a possibly-performing
callee followed by caller work, mutually recursive segment SCCs, and nested
performs; function count is bounded by the recorded segment count.
Add source over-application where the first exact stage performs and resumes
with a callable before a second argument stage. The first continuation's
dynamic parameter is that callable, its frame owns the untouched later
arguments, and resumption executes the second exact call exactly once; Raised
and Cancelled at either stage release only the still-live stage owners.
Add a ResumeTarget shaped `{operation result, threaded Owned state}` and prove
the frame has one dynamic parameter, owns the threaded state in its closure,
and releases or transfers that state exactly once on every outcome.
Add two performs of the same operation in one handled body whose suffix rows
are respectively pure and independently effectful; their exact segment rows
remain distinct, both are subsets of `BodyEffects`, and both instantiate the
same group's normalized raw-continuation/resume closure descriptor.
Add caller-suffix and shared-join cases whose rows contain distinct closed
operation applications plus independent raise/cancel facts, and require every
generated CallableSignature to carry the exact semantic row before its runtime
slot is filled.

Every effect-generated function therefore exists, has a complete closed
semantic signature/effect row, and has empty `RuntimeEffects` before operation
instantiation starts. `EffectPrepared` forbids `Perform`, `Resume`, and all
match pseudo-terminators; it permits `SuspendWithContinuation` only with an
`OperationUse`, plus `InvokeWithContinuation`,
`ResumeContinuationOutcome`, `InvokeHandledBodyOutcome`,
`InvokeProtectedTryOutcome`, both state-creation forms, both cleanup-obligation
terminators, `BorrowOwnedSlotStateUniqueInst`, all three state-access forms
(`BoundaryCaptureBorrow`, `BoundaryCaptureTake`, and
`BoundaryPinnedCaptureBorrow`), `InvokeBoundaryFrameOutcome`, all four checked request queries,
`MakeHandlerResume`, all four handler/try append forms, and prepared request
takes, handler dispatch, handler/try boundary/state/cleanup descriptor records
and regions, and the
ordinary callable forms above. The verifier proves every prepared
function/region cross-link and requires every prepared resume's boundary
operand to be the exact in-function Borrow parameter with valid domain and
dominance,
semantic row, source range, and ownership contract. Preparation is
transactional and deterministic; failure leaves `CleanupPrepared` unchanged.
Tests cover a non-Shareable linear Consume argument, two same-operation
clauses whose first pattern fails and second matches with one successful
payload take, plus a final pattern failure that performs no take and
preserves the same operation/argument owners while transactionally installing
the fresh deep boundary required for outward propagation, without
manufacturing `MatchError`.

Immediately run Task 9's single generic `runClosureConversion` over the whole
`EffectPrepared` module. It closure-converts every source, continuation,
try protected/catch/success/boundary, handler,
handler-entry/dispatch/boundary, cleanup-drop, and prepared resume function and
creates every required universal adapter for a callable-reachable function with
its exact semantic row and an empty runtime-row slot. The resulting
`ClosureConverted` module is the immutable final `FunctionId` and callable-
descriptor set: no later pass may create a function, adapter, descriptor, or
closure environment.
PreparedBody/PreparedProtected unique-loan entries and the fixed-ABI
OutcomeRouter/CleanupDrop/key-operation adapters are analyzed and preserved
but are not callable-reachable and receive no universal adapter or
`CallableDescriptor`.

- [ ] **Step 5: Instantiate only closed runtime operations**

Expose `PassResult runOperationInstantiation(Module &)`. It accepts
`ClosureConverted`, visits every prepared suspension/handler `OperationUse`, validates
type/effect argument counts against the declaration-owned binder order,
substitutes the complete parameter/result/effect graph, and rejects any
residual TypeParameter/open row. It constructs a `ClosedEffectOperation` from
the declaration key, ordered concrete type/effect arguments, and complete
substituted parameter/result/residual-row signature. It interns a closed
`OperationInstance` by those complete `encodeClosedDescriptorGraph` bytes, computes its
`RuntimeFingerprint`, rewrites each reference to `OperationInstanceId`, and
then closes every reachable function/operation residual effect row. Each
`EffectOperationApplication` is resolved by its qualified key, its ordered
type/effect arguments are substituted through the same interner, and the
resulting `OperationInstanceId`s populate one `RuntimeEffectRow` in the row's
canonical application order. This includes effects declared on direct,
imported, and native callees: their v2 structural signatures carry complete
applications rather than bare keys, so no interprocedural guess is required.
Because preparation and closure conversion have frozen the final function and
callable sets, the pass sets
every reachable function's `RuntimeEffects`, rewrites each
operation instance's residual row to `RuntimeEffects`, and changes
each reachable `CallableSignature::RuntimeEffects` from empty to the final
`RuntimeEffectRowId` on the already-frozen descriptor set;
`SemanticEffects` remains available for diagnostics and cross-checking.
It atomically rewrites every `PreparedHandlerGroup::Operation` as well and
every `PreparedRequestTake::Operation`, every handler/try-boundary append
operation, and every `ContinuationTransition::Operation`; then it fills each
`HandlerBoundaryDescriptorPlan::RuntimeEffects` and
`TryBoundaryDescriptorPlan::RuntimeEffects` and rechecks each stored MatchEntry,
taken value, raw continuation, and prepared resume signature against the
closed instance and enclosing HandleRegion, and each protected/catch/success
signature against the solver-owned TryRegion types/rows. It also closes and interns every
`SuspendWithContinuation::InitialContinuationType` and
current/resulting type in every referenced `ContinuationTransitionTable`;
emitted
`YonaContinuationDescriptor`s therefore come only from these verified closed
FunctionTypes. It builds each table from the callee FunctionType's complete
closed operation applications and result plus the caller boundary's result/
row—never from callee-internal suspension sites. For each operation it interns
the boundary-normalized current and composed descriptors, sorts entries by
full operation/current bytes, and rejects duplicates. Imported, native, and
dynamic call boundaries use their v2 structural FunctionType through this
same algorithm. Descriptor/table graphs are reserved and published as one
SCC; missing operations, an exact suffix not contained in its boundary row,
open types, or non-compositional entries fail before commit. It never
reconstructs grouping, targets, or continuation types.

Runtime rows and operation instances are interned as one closed SCC: reserve
all IDs, fill application edges, derive canonical bytes/fingerprints, verify
the graph, and publish only after every edge is closed. Monomorphic
declarations take the same path with empty substitutions; no declaration
itself stores a runtime fingerprint. Builder gains checked instance/runtime-
row interning and lookup, and equality uses full bytes after the hash
prefilter. The pass computes the entire candidate graph and rewrite map in
private storage and commits once; any error leaves the input module
byte-for-byte unchanged. The phase verifier requires every reachable
prepared suspension/handler reference to be a valid closed instance, every reachable
function/callable/operation instance to reference a valid runtime row, and
rejects unused or mismatched instance/row records before `EffectOutlined`.

Tests prove alpha-equivalent open declarations share a declaration
fingerprint, two concrete instantiations have distinct runtime fingerprints,
monomorphic and generic instances use the same interner, forced runtime-hash
collisions compare bytes, one semantic row containing `E.op<Int>` differs from
one containing `E.op<String>`, imported/native residual rows resolve to exact
instance lists, mutually recursive rows publish atomically, and open
substitutions fail without mutating the module. Phantom type/effect arguments
that do not occur in the substituted signature still produce distinct
descriptors and select distinct forced-collision handler clauses. Task 15 adds round-trip/
specialization cases that close imported and local generic operations.

- [ ] **Step 6: Finalize each prepared suspension, try, and handler**

Operation instantiation has rewritten every prepared
`SuspendWithContinuation::Operation` and handler clause reference from
`OperationUse` to `OperationInstanceId`. `runEffectConversion` verifies those
closed references, replaces each `PreparedHandlerDispatch` with the ordinary
descriptor-switch/call CFG, materializes every persisted
`PreparedRequestTake`, wires the already-created entry, dispatch, handler, and
resume functions, wires every prepared protected/catch/success/try-router
record, and removes the prepared `TryRegion`, `HandleRegion`, and
`ContinuationSegment` records. It creates no
Function, semantic row, runtime row, match plan, callable descriptor, or
capture-field/environment layout; any
such insertion after `OperationInstantiated` is a verifier error.

Finalization emits only the four explicit checked query terminators. It uses
`TestEffectRequestOperation` for each deterministic fingerprint bucket and
full `YonaRuntimeEffectOperationEquivalent` collision check, then
`ValidateEffectRequestArgumentCount` and one
`BorrowEffectRequestArgument` per field. A selected path uses
`BorrowEffectRequestContinuation` only when its prepared action requires the
raw chain. On a match it materializes typed Trivial/Borrowed block arguments and branches
to the persisted MatchEntry with those values followed by the original Owned
request. No match branches to the persisted UnhandledEntry with that request.
Accessor/count/type corruption preserves or releases owners according to the
failed transaction, releases the exact nonallocating descriptor-mismatch
diagnostic, traverses cleanup, and traps; no ABI failure leaves dispatch as a
language outcome. The request owner dominates every
borrow and moves on the selected branch. Finalization may create only
same-function instances of these already-declared query terminators plus
switch/branch blocks; no raw descriptor pointer or `ControlOutcome` becomes an
SSA value. It cannot
create or rediscover a function, callable, row, match plan, group entry,
selection leaf, or unhandled target. It erases the prepared carrier/region only
after every stored target is wired and verified.

`runEffectConversion` consumes `OperationInstantiated` and produces `EffectOutlined`.
`EffectOutlined` permits `SuspendWithContinuation` and
`ForwardCallableOutcome`, `InvokeWithContinuation`, and
`ResumeContinuationOutcome`, `InvokeHandledBodyOutcome`,
`InvokeProtectedTryOutcome`, and the four closed handler/try-boundary append
operations. It also permits the closed state create/access, request-query,
`TakeEffectRequestPayload`, `MakeHandlerResume`, `RouteInnerOutcome`, and
detached-boundary router forms produced by preparation/finalization, but forbids `Perform`,
`Resume`, `PreparedHandlerDispatch`, and residual prepared-try/handle records.
An unprepared TryRegion is legal only when its stored `ProtectedEffects`
contains no operation application; it survives tail/accelerator passes
unchanged and Task 12 removes it through ordinary Raised routing. Any
perform-capable residual TryRegion is a verifier error.
The module remains closure-converted throughout finalization. Task 14 extends
these phase allowlists, visitors, and the existing operation-instantiation
transaction with its task/group forms, Promise/result runtime rows, immutable
async descriptors, and await transition tables; none of those later types is
referenced by the Task 13 implementation commit.
`EffectOutlined` proves there is no lexical reference, late callable
descriptor, or late adapter; all handler clause patterns were compiled
through the shared decision arena; and no `MatchDispatch`, `DecisionSwitch`,
or `MatchGuardYield` remains.
Task 13 extends Task 12's already-implemented `runControlOutcomeLowering`
visitor and verifier with these now-defined effect-outlined forms; the final
pipeline invokes that extended pass only after `EffectOutlined`, tail-call
lowering, and accelerator selection. It never asks the Task 12 implementation
to know a future record. The pass is an IR-to-IR rewrite and emits no C call:

- `SuspendWithContinuation` becomes `CreateEffectRequestRuntime`; its Success
  prefix is the one owned request consumed by a following `ReturnPerformed`.
- `ForwardCallableOutcome` becomes `InvokeOutcome` with four forwarding
  successors. `InvokeWithContinuation` becomes one `InvokeOutcome`; Success
  invokes/consumes the caller-suffix frame with ordinary `InvokeOutcome` when
  its ambient context contract satisfies every recipe, or with
  `InvokeBoundaryFrameOutcome` only when a required local recipe is absent
  from ambient context and the enclosing prepared body supplies its matching
  unique state loan. Performed enters
  `AppendContinuationFrameToRequest`, and Raised/Cancelled release the unused
  frame before forwarding.
- `ResumeContinuationOutcome` becomes `ResumeContinuationRuntime`, preserving
  its exact `BoundaryContext`; the runtime call receives it between the
  argument and execution-context operands. Its four produced outcome
  successors, unchanged-input `Failure`, and consumed-input `ConsumedFailure`
  edges are explicit and may never be merged.
- `InvokeHandledBodyOutcome` and `InvokeProtectedTryOutcome` each become one
  `InvokeBoundaryFunctionOutcome` plus their prepared ordinary routing CFG;
  the stable boundary ID, unique state loan, and parent context are preserved
  explicitly for LLVM's stack Direct construction. Initial handler
  Success/Performed move state to `PreparedSuccess`/`PreparedDispatch`; initial
  try Success/Raised move it to the success/catch functions. Their initial
  Performed edges use the checked append terminators. Neither initial path calls
  an OutcomeRouter because no detached boundary token exists. Installed nodes
  alone enter `RouteInnerOutcome`; each generated router consumes its implicit
  token with the release/reinstall terminators above, or with the distinct
  unchanged-input structural-failure cleanup before trapping.
- Effect finalization's `TakeEffectRequestPayload`, four request queries, state
  creation/access, `MakeHandlerResume`, and the four handler/try append forms
  are already the dedicated checked runtime terminators and remain unchanged.

The resulting low-level ABI set is therefore `CheckedRuntimeOp`,
`InvokeOutcome`, `RouteInnerOutcome`, the four `Return*` forms,
`CreateEffectRequestRuntime`, `AppendContinuationFrameToRequest`,
`TakeEffectRequestPayload`, `ResumeContinuationRuntime`, both state creations,
the unique-loan marker and all three state accesses, all four request queries,
`InvokeBoundaryFunctionOutcome`, `InvokeBoundaryFrameOutcome`,
`MakeHandlerResume`, all four
boundary appends, the detached-boundary release/reinstall forms, and Task 10's
checked cleanup-obligation forms. `ControlOutcomeLowered` forbids every
semantic/prepared effect form (`SuspendWithContinuation`,
`ForwardCallableOutcome`, `InvokeWithContinuation`,
`ResumeContinuationOutcome`, `InvokeHandledBodyOutcome`, and
`InvokeProtectedTryOutcome`) but deliberately admits this closed low-level
set. Task 14 extends the same rule with its closed async ABI terminators.
Task 10's `TryRetainRuntime` is added later, during ownership lowering after
representation selection; it is the sole additional checked ownership
terminator and is admitted only from `OwnershipLowered` onward.

Every low-level form has an exhaustive parser/printer, operand and successor
visitor, ValueId remapper, phase classifier, ownership transfer, cleanup edge,
and LLVM case. A produced Success prefix consists only of newly initialized
target block arguments; Borrow prefixes are tied to the dominating request or
state owner, while Move APIs transfer/clear precisely their documented owners
on true. A false result from a diagnostic-producing low-level form leaves all
precommit owners unchanged, releases its reserved diagnostic, follows
`TrapCompilerFailure::CleanupThenTrap`, and never rejoins
source control flow. Callee-owns callable/resume APIs additionally route a true
reserved diagnostic through their separate `ConsumedFailure` disposition,
where all Consume inputs are already cleared; it cannot share the unchanged-
input cleanup state. Every callable/resume invocation receives the current
dominating `YonaExecutionContext`; a different true ABI call may carry the
exact declared Raised outcome, but neither internal failure is a language edge.
`TryRetainRuntime` is the explicit exception: false leaves its source/backing
owner unchanged and carries no diagnostic outcome before the same cleanup and
trap disposition.

`RepresentationSelected` admits this set plus the pre-ownership `RetainInst`;
`OwnershipLowered`, `CleanupLowered`, and `LlvmReady` replace that intent with
`TryRetainRuntime` and otherwise continue to admit exactly these low-level
forms plus ordinary CFG;
their verifiers reject semantic/prepared operations, unexpanded ordinary
fallible instructions, malformed produced prefixes, or incomplete cleanup.
From `EffectPrepared` through `EffectOutlined`, prepared resume records must
carry the exact dominating boundary parameter; from
`ControlOutcomeLowered` through `LlvmReady`, the runtime resume form must carry
the same ValueId. Operand/remap/phase/ownership visitors preserve it as a
Borrow-only non-owning use and reject a missing, foreign, or nondominating
context.
`InvokeBoundaryFunctionOutcome` is the direct-FunctionId case and therefore
has no runtime structural-failure edge; its verifier proves the callee ABI,
boundary/state/loan identity, and four successor contracts before LLVM. LLVM
keeps the stack Direct context and unique loan live through the call, destroys
the view before routing any returned outcome, and never exposes its pointer as
a `ValueId`.
`LlvmBlockLowerer` is the sole layer that allocates C output/failure storage,
calls the corresponding Effect/Callable/OwnedSlotState API exactly once,
branches on its bool/outcome, clears moved slots, and emits the invariant trap.
Its dispatch over the low-level set is exhaustive with no default. No generic
"typed runtime call" instruction, raw descriptor-pointer SSA value, or
post-LLVM ownership inference exists.
ABI tests assert the exact continuation-resume argument order
`continuation, argument, outer-boundary, execution-context, outcome` on every
target.

- [ ] **Step 7: Verify operation, continuation, and environment agreement**

`verifyEffects` checks operation identity, argument count/types/ownership,
raw continuation input/body-result and source resume input/handle-result
types, exact frame semantic/runtime rows, subset inclusion in each normalized
boundary descriptor row, total/unique transition-table entries, exact typed
continuation descriptors,
one dynamic parameter per chained frame, and no free lexical
references, and no foreign task-group/arena/debug/value state. It also proves
only `InvokeHandledBodyOutcome`/handler-boundary nodes feed body or resumed
body outcomes into dispatch, while handler-clause and return-clause outcomes
use the bypassing `ForwardCallableOutcome` path. It separately proves that
only `InvokeProtectedTryOutcome`/try-boundary nodes route protected outcomes
to catch/success, and catch/success outputs bypass that same boundary. It proves
the FunctionId set and all Function semantic/runtime rows are byte-identical
before and after `runEffectConversion`. Task 13 installs preparation before
the single closure conversion, freezes functions/descriptors there, then runs
operation instantiation and effect finalization; the remaining suffix starts
with tail-call lowering.

- [ ] **Step 8: Emit the prepared handler entry and dispatch**

`EffectConversion` materializes the already-prepared entry/dispatch graph as ordinary verified Typed IR:
it switches on the closed instance's runtime descriptor fingerprint, confirms complete
operation-descriptor equivalence within the selected collision bucket,
borrows request arguments for ordered clause tests, transactionally takes the
payload only at the selected leaf, invokes the matching typed handler callable,
and gives it the frozen one-shot resume callable. Body/resumption Performed
outcomes call the dispatch function recursively; a selected clause or return
clause forwards its whole outcome without redispatch. LLVM lowering sees
only the resulting ordinary descriptor loads, switches, calls, and outcome
terminators; it must not inspect effect semantics or synthesize dispatch. Do
not add a runtime handler stack or C dispatcher. Exact execution tests cover
a non-identity return clause reached through resume, computation after a
resume call in the handler clause, and the distinction between a same-
operation perform in the resumed body (handled here) and in the handler or
return clause (forwarded to the next outer handler).

- [ ] **Step 9: Run typed effect and allocation gates**

```bash
python3 scripts/run-focused-tests.py ./out/build/x64-debug-linux/tests \
  -tc='Effect conversion*,Effect lowering*,Typed IR execution*effect*,Typed IR control outcomes*'
python3 scripts/run-focused-tests.py ./out/build/x64-debug-linux/tests \
  -tc='Fixture-based codegen tests'
python3 scripts/quality.py --build-dir out/build/x64-debug-linux \
  --preset x64-debug-linux --jobs 2 sanitize
git diff --check
```

Expected: typed effect fixtures pass at O0-O3, handler environments contain no
foreign values, and continuation/request owners balance.

Document this replacement-effect path in a clearly marked test-only section
of `docs/typed-ir.md`; shipped language/runtime documentation changes only in
Task 17.

- [ ] **Step 10: Commit effects and continuations**

```bash
git add include/yona/Runtime/Core/Effect.h src/Runtime/Core/Effect.c \
  include/yona/Runtime/Core/OwnedSlotState.h src/Runtime/Core/OwnedSlotState.c \
  test/Runtime/EffectTest.cpp \
  include/yona/TypedIr src/TypedIr src/Codegen/Llvm \
  test/TypedIr test/Codegen/EffectLoweringTest.cpp \
  test/Fixtures/TypedIr/Effects cmake/YonaComponents.cmake CMakeLists.txt \
  docs/typed-ir.md
git add docs/superpowers/plans/2026-09-01-typed-ir-codegen-rewrite.md
git commit -m "feat: lower effects to typed continuations"
```

### Task 14: Move all asynchronous work to callables and whole outcomes

**Files:**

- Create: `include/yona/Runtime/Concurrency/OutcomeTask.h`
- Create: `src/Runtime/Concurrency/OutcomeTask.c`
- Create: `src/Runtime/Concurrency/OutcomeTaskPosix.c`
- Create: `src/Runtime/Concurrency/OutcomeTaskWin32.c`
- Create: `include/yona/TypedIr/Async.h`
- Create: `include/yona/TypedIr/Passes/AsyncPlanning.h`
- Create: `src/TypedIr/Passes/AsyncPlanning.cpp`
- Modify: `include/yona/TypedIr/Passes/AsyncPreparation.h`
- Modify: `src/TypedIr/Passes/AsyncPreparation.cpp`
- Create: `include/yona/TypedIr/Verification/AsyncVerifier.h`
- Create: `src/TypedIr/Verification/AsyncVerifier.cpp`
- Modify: `include/yona/Runtime/Concurrency/Channel.h`
- Modify: `include/yona/Runtime/Gpu/Api.h`
- Modify: `include/yona/Runtime/Gpu/VulkanDevice.h`
- Modify: `include/yona/Runtime/Platform/Api.h`
- Modify: `include/yona/Runtime/Platform/IoContext.h`
- Modify: `include/yona/Runtime/Platform/IoUring.h`
- Modify: `include/yona/Runtime/Platform/Kqueue.h`
- Modify: `include/yona/Runtime/Platform/Windows.h`
- Create: `include/yona/Runtime/Platform/ResourceHandle.h`
- Create: `src/Runtime/Platform/ResourceHandle.c`
- Modify: `src/Runtime/Platform/IoContext.c`
- Modify: `src/Runtime/Platform/IoUringLinux.c`
- Modify: `src/Runtime/Platform/KqueueMacOs.c`
- Modify: `src/Runtime/Platform/FileLinux.c`
- Modify: `src/Runtime/Platform/FileMacOs.c`
- Modify: `src/Runtime/Platform/FileWindows.c`
- Modify: `src/Runtime/Platform/NetLinux.c`
- Modify: `src/Runtime/Platform/NetMacOs.c`
- Modify: `src/Runtime/Platform/NetWindows.c`
- Modify: `src/Runtime/Platform/OsLinux.c`
- Modify: `src/Runtime/Platform/OsMacOs.c`
- Modify: `src/Runtime/Platform/OsWindows.c`
- Modify: `src/Runtime/Concurrency/ChannelPosix.c`
- Modify: `src/Runtime/Concurrency/ChannelWin32.c`
- Modify: `src/Runtime/Stdlib/Native.c`
- Modify: `src/Runtime/Gpu/Stub.c`
- Create: `test/Runtime/AsyncOutcomeTest.cpp`
- Modify: `test/Runtime/GpuStubTest.cpp`
- Modify: `test/TypedIr/CleanupVerifierTest.cpp`
- Create: `test/Codegen/AsyncAbiTest.cpp`
- Create: `test/TypedIr/AsyncPreparationTest.cpp`
- Create: `test/TypedIr/AsyncPlanningTest.cpp`
- Create: `test/TypedIr/AsyncVerifierTest.cpp`
- Modify: `test/Semantics/SemanticModelTest.cpp`
- Modify: `test/Semantics/TypeCheckerTest.cpp`
- Modify: `test/Semantics/RuntimeEntryRegistryTest.cpp`
- Create: `test/Semantics/StructuralTypeProjectionTest.cpp`
- Create: `test/CMake/outcome_async_api_contract.py`
- Create: `test/Fixtures/TypedIr/Async/async_float_zero.yona`
- Create: `test/Fixtures/TypedIr/Async/async_float_zero.expected`
- Create: `test/Fixtures/TypedIr/Async/async_float_one.yona`
- Create: `test/Fixtures/TypedIr/Async/async_float_one.expected`
- Create: `test/Fixtures/TypedIr/Async/async_aggregate_many.yona`
- Create: `test/Fixtures/TypedIr/Async/async_aggregate_many.expected`
- Modify: `test/Runtime/TaskOwnershipTest.cpp`
- Modify: `include/yona/TypedIr/Callable.h`
- Modify: `include/yona/TypedIr/Instruction.h`
- Modify: `include/yona/TypedIr/TypedIr.h`
- Modify: `include/yona/TypedIr/Builder.h`
- Modify: `include/yona/TypedIr/AstLowering.h`
- Modify: `include/yona/TypedIr/Pipeline.h`
- Modify: `include/yona/Semantics/RuntimeEntryRegistry.h`
- Modify: `include/yona/Semantics/SemanticModel.h`
- Modify: `include/yona/Semantics/TypeChecker.h`
- Modify: `include/yona/Semantics/StructuralTypeProjection.h`
- Modify: `src/Semantics/SemanticModel.cpp`
- Modify: `src/Semantics/TypeChecker.cpp`
- Modify: `src/Semantics/StructuralTypeProjection.cpp`
- Modify: `src/TypedIr/AstLowering.cpp`
- Modify: `src/TypedIr/Analysis/OwnershipAnalysis.cpp`
- Modify: `src/TypedIr/Analysis/EscapeAnalysis.cpp`
- Modify: `src/TypedIr/Analysis/FreeVariables.cpp`
- Modify: `src/TypedIr/Passes/CleanupLowering.cpp`
- Modify: `src/TypedIr/Verification/OwnershipVerifier.cpp`
- Modify: `src/TypedIr/Verification/EscapeVerifier.cpp`
- Modify: `src/TypedIr/Verification/CleanupVerifier.cpp`
- Modify: `src/TypedIr/Builder.cpp`
- Modify: `src/TypedIr/TypedIr.cpp`
- Modify: `src/TypedIr/Verifier.cpp`
- Modify: `src/TypedIr/Printer.cpp`
- Modify: `src/TypedIr/Parser.cpp`
- Modify: `src/TypedIr/Pipeline.cpp`
- Modify: `src/TypedIr/Passes/GeneratorLowering.cpp`
- Modify: `src/TypedIr/Passes/ControlFlowLowering.cpp`
- Modify: `src/TypedIr/Passes/EffectPreparation.cpp`
- Modify: `src/TypedIr/Passes/ClosureConversion.cpp`
- Modify: `src/TypedIr/Passes/OperationInstantiation.cpp`
- Modify: `src/TypedIr/Passes/ControlOutcomeLowering.cpp`
- Modify: `src/TypedIr/Passes/OwnershipLowering.cpp`
- Modify: `src/TypedIr/Passes/CleanupPreparation.cpp`
- Modify: `src/TypedIr/Passes/CleanupLowering.cpp`
- Modify: `src/TypedIr/Verification/CallableVerifier.cpp`
- Modify: `src/TypedIr/Verification/EffectVerifier.cpp`
- Modify: `src/TypedIr/Verification/OwnershipVerifier.cpp`
- Modify: `src/TypedIr/Verification/CleanupVerifier.cpp`
- Modify: `src/Codegen/Llvm/CallableLowering.cpp`
- Modify: `src/Codegen/Llvm/BlockLowerer.cpp`
- Modify: `src/Codegen/Llvm/FunctionLowerer.cpp`
- Modify: `test/TypedIr/PrinterParserTest.cpp`
- Modify: `cmake/YonaComponents.cmake`
- Modify: `CMakeLists.txt`
- Modify: `docs/typed-ir.md`

**Interfaces:**

- Consumes: `PatternCanonical` GeneratorPlans, canonical `PromiseType`,
  semantic structured-concurrency facts, `NativeAsyncKind`, `YonaCallableRef`,
  `YonaAbiValue`, and `YonaControlOutcome`.
- Produces: `AsyncPlanned`, explicit task/group/submission/await Typed IR, then
  `AsyncPrepared`; one submission/completion/await contract for thread-pool,
  native, IO, channel, task-group, and GPU work at all arities and register
  classes, using the canonical internal `OutcomeTaskGroupType` carrier.
- Coexistence: replacement functions use the ABI-distinct
  `YonaOutcomeTaskRef` family and `*Outcome` platform/channel/GPU entry points.
  Legacy task layouts and calls remain byte-compatible for legacy objects
  until Task 17; old and new task handles can never cross APIs.

- [ ] **Step 1: Write red runtime move/lifetime tests**

Cover Success/Raised/Performed/Cancelled, null/repeated completion, consuming
await versus retaining await-keep, first source-index task non-Success during
post-join extraction, zero-argument submission, allocation/submission failure, cancellation
before execution, queued callable/argument destruction, non-shareable
await-keep rejection, release of every not-yet-extracted child outcome, and worker
execution without SJLJ/TLS exception state. Include task/group retain/release,
caller-dropping-group-before-completion, explicit task/group cancellation,
all-success `Success(Unit)`, and a refcount assertion proving no task/group
cycle survives terminal completion. Distinguish pre-commit allocation failure
(false/null/inputs unchanged) from forced post-commit OS/GPU submission
failure (true/owned task/inputs cleared/immediate typed terminal outcome):

Extend exact cleanup case `Typed IR control outcomes: with finalizer runs for
every control outcome` with its Cancelled rows.

```cpp
TEST_CASE("Async outcome repeated completion releases the rejected owner") {
  RcProbe First;
  RcProbe Rejected;
  auto Task = submitImmediate(First.successOutcome());
  auto Second = Rejected.successOutcome();
  CHECK_FALSE(YonaRuntimeOutcomeTaskCompleteMove(Task, &Second));
  CHECK(Second.Kind == YONA_OUTCOME_EMPTY);
  CHECK(Rejected.releaseCount() == 1);
  auto Result = awaitOutcome(Task);
  releaseOutcome(Result);
  CHECK(First.balanced());
}
```

GPU lifetime cases Consume the last external FloatArray owner, delay fence
completion, and prove the task owns storage until terminal Success returns the
mutated FloatArray exactly once. Cover submission failure leaving the owner
unchanged. Retain an alias before a second case: submission must allocate a
private output, the alias must remain byte-identical and readable while the
fence is outstanding, and the returned array alone contains the mutation.
Submit scale and mul2 concurrently from two aliases (the existing codegen
repro) and require distinct device-visible storage under TSan/owner probes.
For cancellation after a successful Vulkan
queue submit, hold the fence unsignalled, request cancellation, and assert that
the task is still non-terminal and its pin remains retained; signal/retire the
work, then assert `Cancelled(Unit)` and exactly one pin release. Cancellation
before queue ownership transfers may complete immediately and still releases
the pin exactly once.

Create top-level sentinels `Async ABI uses one descriptor for arities zero one
and many`, `Typed IR execution: async preserves all carrier classes`, `Typed
IR execution: channel cancellation returns a whole outcome`, and `Typed IR
execution: gpu pins storage through fence retirement`. These make every Task
14 async/channel/GPU prefix independently enumerable.

- [ ] **Step 2: Write the generated-adapter red ABI matrix**

Test async Unit, Bool, Byte, Char, Int, Float, Symbol, String, tuple, record, sequence, flat/heap
ADT, and closure results at arities 0, 1, and N. Inspect LLVM and reject any
typed-function-to-worker callback bitcast. Run each legal row at O0-O3 and
cover normal, raise, perform, and cancellation outcomes. Runtime tests submit
0/1/N exact calls and reject both under- and overapplication with every input
owner unchanged; lowering tests prove curried/overapplied source calls use one
exact generated wrapper.

- [ ] **Step 3: Run and confirm old int64/callback behavior fails**

```bash
cmake --build --preset build-debug-linux --target tests -j2
python3 scripts/run-focused-tests.py ./out/build/x64-debug-linux/tests \
  -tc='Async outcome*,Async ABI*,Typed IR control outcomes*'
```

Expected: compilation fails for the outcome APIs; legacy zero/one-argument
paths cannot satisfy the Float/aggregate tests.

- [ ] **Step 4: Make structured concurrency explicit before effects**

First make the semantic concurrency decisions durable. `SemanticModel` owns
the following AST-identity keyed facts; TypeChecker computes them from binding
identity/use edges, never from textual name comparison, and AstLowering must
copy every fact into canonical IR before the syntax tree can disappear:

```cpp
namespace yona::semantics {
struct LetBindingDependencyProjection {
  BindingId Binding;
  std::vector<BindingId> DependsOn;
  SourceRange Range;
};
struct IndependentLetProjection {
  const syntax::LetExpr *Owner;
  std::vector<LetBindingDependencyProjection> Bindings;
  SourceRange Range;
};
struct GeneratorConcurrencyProjection {
  const syntax::GeneratorExpr *Owner;
  bool Parallel;
  SourceRange Range;
};
struct PromiseDemandProjection {
  const syntax::ExprNode *Demand;
  compiler::typechecker::EffectRef ResumeEffects;
  SourceRange Range;
};
struct ProjectedPromiseDemand {
  const syntax::ExprNode *Demand;
  model::EffectRowId ResumeEffects;
  SourceRange Range;
};
}
```

Task 14 adds `std::span<const PromiseDemandProjection> PromiseDemands` to
Task 2's `SemanticProjectionBatch` and
`std::vector<ProjectedPromiseDemand> PromiseDemands` to
`ProjectedSemanticBatch`. They are staged, scoped, projected, and committed in
the same all-or-nothing `freezeSemanticBatch` transaction as function node
facts.

This task, rather than Task 11, extends `SemanticIntrinsicKind` after Task 12's
compiler-plan cancellation point with the next closed value `TaskSpawn = 16`
and adds:

```cpp
struct TaskSpawnIntrinsicCallProjection {
  compiler::typechecker::MonoTypePtr DeclaredFunctionType;
  compiler::typechecker::MonoTypePtr CallableType;
  compiler::typechecker::MonoTypePtr ResultType;
  compiler::typechecker::EffectRef WorkEffects;
  model::ResultOwnership ResultContract;
};
struct ProjectedTaskSpawnIntrinsicCall {
  model::TypeId FunctionType;
  model::TypeId CallableType;
  model::TypeId ResultType;
  model::EffectRowId WorkEffects;
  model::ResultOwnership ResultContract;
};
```

`NodeSemanticProjection`, `ProjectedNodeFact`, and `NodeSemantics` gain the
optional `TaskSpawnIntrinsicCall` field and `taskSpawnIntrinsicCallFor`
accessor here. The shared batch transaction projects it in the owning scope;
AST lowering consumes it into `TaskSpawnInst`, and direct/full-application,
provenance, arity, type/effect, parser/printer/remap, rollback, and phase tests
ship in this task. Task 11 therefore remains independently buildable without
mentioning Task 14 IR.

The dependency projection includes every RHS reference to another binding in
the same multi-binding let, rejects cycles with a ranged semantic diagnostic,
and preserves declaration order for otherwise independent nodes. The
generator projection is the sole source of the Serial/Parallel fact copied to
Task 11's `GeneratorPlan::Execution`. Missing, duplicate, foreign-node, or
range-mismatched projections are lowering errors; no pass falls back to
surface spelling or assumes serial execution.

Add the canonical/planned/prepared records:

```cpp
using IndependentLetPlanId = StrongId<struct IndependentLetPlanIdTag>;
using AsyncResultDescriptorId = StrongId<struct AsyncResultDescriptorIdTag>;
using ChannelDescriptorId = StrongId<struct ChannelDescriptorIdTag>;
enum class CheckedOutcomeOpcode : std::uint8_t {
  ChannelCreate = 0,
  ChannelSend = 1,
  ChannelReceive = 2,
  ChannelTryReceive = 3,
  FileWrite = 4,
  FileReadBytes = 5,
  FileDescriptorReadBytes = 6,
  FileDescriptorWriteBytes = 7,
  FileDescriptorWriteString = 8,
  FileDescriptorWriteStrings = 9,
  NetRecvBytes = 10,
  NetSendString = 11,
  NetSendBytes = 12,
  NetTcpAccept = 13,
  NetTcpConnect = 14,
  NetUdpRecvBytes = 15,
  NetUdpSendToBytes = 16,
  GpuFloatArrayMul2 = 17,
  GpuFloatArrayScale = 18
};
enum class TaskGroupPlanKind : std::uint8_t {
  IndependentLetWave = 0, ParallelComprehension = 1
};
enum class AsyncAwaitOwnership : std::uint8_t { Move = 0, Keep = 1 };
struct ChannelDescriptorPlan {
  ChannelDescriptorId Id;
  model::TypeId PayloadType;
  model::TypeId SenderType;
  model::TypeId ReceiverType;
  model::TypeId EndpointPairType;
  model::TypeId OptionPayloadType;
  model::TypeId ChannelErrorType;
  std::uint64_t InvalidCapacityConstructorTag;
  std::uint64_t ClosedConstructorTag;
};
struct AsyncResultContract {
  model::TypeId Type;
  model::ResultOwnership Ownership;
  model::EffectRowId Effects;
  std::optional<model::TypeId> RaisedConstraintType;
  std::optional<RuntimeEffectRowId> RuntimeEffects;
  std::optional<AsyncResultDescriptorId> RuntimeDescriptor;
};
struct AsyncResultDescriptorPlan {
  AsyncResultDescriptorId Id;
  model::TypeId SuccessType;
  model::ResultOwnership SuccessOwnership;
  RuntimeEffectRowId Effects;
  std::optional<model::TypeId> RaisedConstraintType;
};
struct AwaitPromiseInst {
  ValueId Promise;
  AsyncResultContract Result;
  model::EffectRowId ResumeEffects;
};
struct TaskSpawnInst {
  ValueId Callable;
  model::TypeId ResultType;
  model::ResultOwnership ResultContract;
  model::EffectRowId WorkEffects;
};
struct NativeAsyncCallInst {
  FunctionId WorkTarget;
  std::optional<CheckedOutcomeOpcode> OutcomeOpcode;
  std::vector<ValueId> Arguments;
  AsyncResultContract Result;
};
using AsyncSubmissionTarget = std::variant<FunctionId, ValueId>;
struct IndependentLetBindingPlan {
  semantics::BindingId Binding;
  FunctionId RhsFunction;
  ValueId Callable;
  AsyncResultContract Result;
  std::vector<std::uint32_t> DependencyIndices;
  SourceRange Range;
};
struct IndependentLetPlan {
  IndependentLetPlanId Id;
  FunctionId Function;
  std::vector<IndependentLetBindingPlan> Bindings; // declaration order
  std::vector<std::vector<std::uint32_t>> Waves;
  BlockId BodyEntry;
  SourceRange Range;
};
struct IndependentLetExprInst { IndependentLetPlanId Plan; };
struct FixedTaskGroupShape {
  std::vector<AsyncResultContract> Results; // source/declaration order
};
struct RepeatedTaskGroupShape {
  AsyncResultContract Element;
};
using TaskGroupResultShape =
    std::variant<FixedTaskGroupShape, RepeatedTaskGroupShape>;
struct TaskGroupPlan {
  TaskGroupPlanId Id;
  TaskGroupPlanKind Kind;
  std::optional<TaskGroupPlanId> Parent;
  TaskGroupResultShape Results;
  SourceRange Range;
};
struct CreateOutcomeTaskGroup {
  TaskGroupPlanId Plan;
  ProducedBranchTarget Success; // one Owned OutcomeTaskGroup prefix
  RuntimeFailureDisposition Failure;
};
struct SubmitOutcomeTask {
  AsyncSubmissionTarget Target;
  std::optional<CheckedOutcomeOpcode> OutcomeOpcode;
  std::vector<ValueId> Arguments;
  std::optional<ValueId> Group;
  AsyncResultContract Result;
  ProducedBranchTarget Success; // one Owned Promise/task prefix
  RuntimeFailureDisposition Failure;
};
struct AwaitOutcomeTask {
  ValueId Task;
  AsyncAwaitOwnership Ownership;
  AsyncResultContract Result;
  ProducedBranchTarget ResumeTarget; // exact Success prefix
  model::EffectRowId ResumeEffects;
};
struct AwaitOutcomeGroup {
  TaskGroupPlanId Plan;
  ValueId Group;
  AsyncResultContract WaitResult; // Trivial Unit; no raise/operations; MayCancel
  BranchTarget ResumeTarget;
};
struct TakeJoinedGroupResult {
  TaskGroupPlanId Plan;
  ValueId Group;
  AsyncResultContract Result;
  ProducedBranchTarget ResumeTarget; // normalized child Success prefix
  model::EffectRowId ResumeEffects;
};
struct ChannelCreateInst {
  ValueId Capacity;
  ChannelDescriptorId Descriptor;
  model::EffectRowId Effects;
};
struct ChannelSendInst {
  ValueId Channel;
  ValueId OwnedValue;
  ChannelDescriptorId Descriptor;
  model::EffectRowId Effects;
};
struct ChannelReceiveInst {
  ValueId Channel;
  ChannelDescriptorId Descriptor;
  model::EffectRowId Effects;
};
struct ChannelTryReceiveInst {
  ValueId Channel;
  ChannelDescriptorId Descriptor;
  model::EffectRowId Effects;
};
struct AwaitTaskSource {
  ValueId Task;
  AsyncAwaitOwnership Ownership;
};
struct JoinedGroupSource {
  TaskGroupPlanId Plan;
  ValueId Group;
};
using PreparedAwaitSource = std::variant<AwaitTaskSource, JoinedGroupSource>;
struct ReadyAwaitContinuationFrame { ValueId Frame; };
struct DeferredGroupContinuationFrame {
  FunctionId Function;
  std::vector<ValueId> Captures; // includes unique group owner at fixed ordinal
};
using PreparedAwaitContinuation =
    std::variant<ReadyAwaitContinuationFrame,
                 DeferredGroupContinuationFrame>;
struct AwaitTaskWithContinuation {
  PreparedAwaitSource Source;
  AsyncResultContract Result;
  PreparedAwaitContinuation Continuation;
  ContinuationTransitionTableId Transitions;
  ValueId BoundaryContext;
  std::optional<BoundaryInvocationOverlay> SuccessFrameOverlay;
};
struct CreateOutcomeTaskGroupRuntime {
  TaskGroupPlanId Plan;
  ProducedBranchTarget Success; // one Owned group prefix
  RuntimeFailureDisposition Failure;
};
struct SubmitOutcomeTaskRuntime {
  AsyncSubmissionTarget Target; // callable ValueId or exact dedicated Outcome FunctionId
  std::optional<CheckedOutcomeOpcode> OutcomeOpcode;
  std::vector<ValueId> Arguments;
  std::optional<ValueId> Group;
  AsyncResultDescriptorId Result;
  ProducedBranchTarget Success; // one Owned task prefix
  RuntimeFailureDisposition Failure;
};
struct AwaitOutcomeTaskRuntime {
  ValueId Task;
  AsyncAwaitOwnership Ownership;
  AsyncResultDescriptorId Result;
  ValueId ExecutionContext;
  ProducedBranchTarget Success;
  ProducedBranchTarget Raised;
  ProducedBranchTarget Performed;
  BranchTarget Cancelled;
  RuntimeFailureDisposition Failure;
};
struct AwaitOutcomeGroupRuntime {
  TaskGroupPlanId Plan;
  ValueId Group;
  ValueId ExecutionContext;
  BranchTarget Success; // Unit has no carrier prefix
  BranchTarget Cancelled;
  RuntimeFailureDisposition Failure;
};
struct TakeJoinedGroupResultRuntime {
  TaskGroupPlanId Plan;
  ValueId Group;
  AsyncResultDescriptorId Result;
  ProducedBranchTarget Success;
  ProducedBranchTarget Raised;
  ProducedBranchTarget Performed;
  BranchTarget Cancelled;
  RuntimeFailureDisposition Failure;
};
struct CreateChannelRuntime {
  ValueId Capacity;
  ChannelDescriptorId Descriptor;
  ProducedBranchTarget Success; // one Owned (Sender a, Receiver a) prefix
  ProducedBranchTarget Raised; // exact declared invalid-capacity value
  RuntimeFailureDisposition Failure; // false, inputs unchanged
  RuntimeFailureDisposition InvalidOutcome; // true row-invalid outcome
};
struct ChannelSendRuntime {
  ValueId Sender;
  ValueId OwnedValue;
  ChannelDescriptorId Descriptor;
  ValueId ExecutionContext;
  BranchTarget Success; // Unit has no carrier
  ProducedBranchTarget Raised;
  BranchTarget Cancelled;
  RuntimeFailureDisposition Failure; // false, value unchanged
  RuntimeFailureDisposition InvalidOutcome; // true, value consumed
};
struct ChannelReceiveRuntime {
  ValueId Receiver;
  ChannelDescriptorId Descriptor;
  ValueId ExecutionContext;
  ProducedBranchTarget Success;
  BranchTarget Cancelled;
  RuntimeFailureDisposition Failure;
  RuntimeFailureDisposition InvalidOutcome;
};
struct ChannelTryReceiveRuntime {
  ValueId Receiver;
  ChannelDescriptorId Descriptor;
  ProducedBranchTarget Success;
  RuntimeFailureDisposition Failure;
  RuntimeFailureDisposition InvalidOutcome;
};
PassResult runAsyncPlanning(Module &);
PassResult runAsyncPreparation(Module &);
```

`CheckedOutcomeOpcode` is a wire-stable closed enum: tests assert every one of
the 19 names has exactly the numeric value shown, reject every value `>= 19`,
and round-trip/remap all values without renumbering. Manifest generation must
cover each value exactly once; appending a future opcode requires a format/
registry update rather than reusing a number.

`Module::ChannelDescriptors` is a deterministic structurally interned arena.
Async preparation builds a plan from the exact instantiated Channel
declaration, including both companion endpoint types, pair, Option, error
nominal, and constructor tags; every prepared/runtime channel form stores only
that ID rather than an incomplete subset. An open plan is legal only inside a
canonical generic fragment and is remapped during specialization. Operation
instantiation requires executable plans closed, validates their error row and
resource policies, and freezes them. Canonical text, TIRF/v2 fragment codec,
clone/remap, phase, type/effect/ownership, and invalid-ID tests cover the
arena. `LlvmModuleLowerer` emits one private static
`YonaAbiChannelDescriptor` global per plan in ID order and validates every
nested descriptor/tag; `LlvmBlockLowerer` passes only that stored global to all
four calls. LLVM never reconstructs companion types or constructor tags from
module names, source spellings, or operand types.

In every `SubmitOutcomeTask` and `SubmitOutcomeTaskRuntime`, `FunctionId` is
legal if and only if it names the exact NativeExtern with `AsyncKind ==
DedicatedOutcome`, carries exactly one non-channel opcode in `4..18`, and the
manifest-authenticated opcode/declaration/symbol/signature/result/error tuple
matches. Every ordinary
Yona/RHS worker and every ThreadPool native uses an exact-arity callable
ValueId, the latter through its generated universal adapter, and has no
Outcome opcode. The four channel records structurally imply opcodes `0..3`
and never use `SubmitOutcomeTask`. A raw
Synchronous native FunctionId is lowered as an ordinary direct/outcome call
and is forbidden in either submit record; a structured-concurrency RHS may
still submit a generated Yona wrapper whose body calls that native
synchronously. AsyncPrepared-through-ControlOutcomeLowered verification
rejects every other discriminant or opcode combination. Tests forge a
Synchronous FunctionId, raw ThreadPool FunctionId, non-native FunctionId,
missing/duplicate/wrong-family opcode, and wrong callable ValueId, and
cover a valid synchronous wrapper, ThreadPool adapter, and dedicated Outcome entry.

AST lowering emits `IndependentLetPlan`/`IndependentLetExprInst`, generator
execution markers, `AwaitPromiseInst`, and `NativeAsyncCallInst` pseudo-
instructions from those semantic identities and facts, not surface spelling.
`NativeAsyncCallInst` is emitted only for a direct, saturated application of a
projected NativeExtern whose `AsyncKind` is `ThreadPool` or
`DedicatedOutcome`. Its
`WorkTarget` is that exact semantic declaration, its arguments preserve the
declared order and ownership, and its result is exactly
`PromiseType{B, asyncLift(e)}` when the underlying signature is
`Args -> B ! e`. Synchronous externs remain ordinary direct/outcome calls.
ThreadPool calls have no opcode; DedicatedOutcome calls carry exactly the
manifest row's opcode in `4..18`.
Private async leaves cannot be named as values, partially or over-applied,
returned, exported, or passed first-class; their public Yona wrappers are
ordinary first-class functions and are the only such source surface. A
`PromiseType` and every
async record agree on exact Success type/ownership and the complete
async-lifted await effect row. The only result contracts are Trivial and Owned,
and both pass through unchanged; Borrowed is not a callable result-contract
variant and malformed imported/hand-built IR is rejected before task creation.
Transparent Promise use inserts `AwaitPromiseInst` exactly where a
promised Success value is demanded; returning or passing a Promise as a
Promise does not await it. A native target's stored
`FunctionLinkage::{NativeRoute,AsyncKind}` plus its authenticated optional
opcode selects one of three disjoint paths and must satisfy the closed pairing
matrix above.
`Synchronous` is an ordinary call to the underlying declaration and never
creates `SubmitOutcomeTask`; `ThreadPool` resolves the
`NativeAsyncCallInst::WorkTarget` to an exact-arity callable ValueId backed by
its prepared adapter and submits that value; `DedicatedOutcome` resolves the source
declaration through its manifest row and submits only the exact dedicated
replacement Outcome FunctionId declared below. `runAsyncPlanning` consumes
every `NativeAsyncCallInst`; none survives `AsyncPlanned`. Outlined Yona RHS/element workers
likewise submit their exact callable ValueId. A raw Synchronous or ThreadPool
FunctionId in either submit record is verifier-invalid. Under/overapplied
source expressions first get an exact-arity generated wrapper.

Channel creation/send/receive are not hidden inside a direct FunctionId
`InvokeOutcome`. Async preparation recognizes only their resolved native
declaration identities and emits the four prepared `Channel*Inst` forms,
which carry closed result/effect facts but no execution-context ValueId.
Control-outcome lowering has already created the function's dominating
`ExecutionContextParameter` when it replaces those forms with the four
dedicated checked channel runtime terminators above.
`CreateChannelRuntime` models the false-before-publication edge. Send
separately models false with `OwnedValue` unchanged; all validation and
outcome storage are prepared before commit, so a true send has consumed the
value and can produce only Success, declared Raised, or Cancelled—never a
reserved ABI diagnostic. Receive forms model their
unchanged false edge and true invalid-outcome cleanup edge. Their ordinary
true Success/Raised/Cancelled successors are typed from the closed
declarations (Receive has no Raised edge; TryReceive has neither Raised nor
Cancelled). The prepared forms are
forbidden at `ControlOutcomeLowered`; the runtime forms survive through
`LlvmReady`, and `LlvmBlockLowerer` emits exactly one matching Channel ABI call
without inventing a bool branch, owner poststate, or source outcome. Parser,
printer, operand/successor/remap, phase, effect, ownership, and cleanup
visitors handle all four exhaustively.

All four runtime terminators own an `InvalidOutcome` trap disposition for a
structurally releasable true outcome outside their closed row. Create reaches
it with no input owner; Send reaches it only after `OwnedValue` was cleared and
therefore performs consumed-input cleanup; Receive/TryReceive leave their
borrowed endpoint unchanged. Each releases the complete unexpected outcome
before trapping. Test doubles return Performed, wrong Success types, forbidden
Raised/Cancelled tags, and reserved `AbiFailure` to exercise every ownership
poststate; no invalid true outcome is routed through the false `Failure` edge.

The four exact instantiated declaration rows are:

| Channel form | Operations/tail | MayRaise | MayCancel |
|---|---|---:|---:|
| Create | empty / none | true | false |
| Send | empty / none | true | true |
| Receive | empty / none | false | true |
| TryReceive | empty / none | false | false |

No channel form may carry a user operation. Create/TryReceive forbid an
execution-context operand and Cancelled successor; Send/Receive require the containing
function's exact `ExecutionContextParameter` and a Cancelled successor.
Create/Send require Raised; Receive/TryReceive forbid it. Operation-
instantiation and control-outcome verification reject any flipped bit, effect
tail, operation, missing/excess successor, or context.

The receive bool/outcome protocol is exact. `false` is exclusively precommit
and always leaves the borrowed channel and queue position unchanged. A null,
aliasing, non-Empty-output, or otherwise structurally unsafe caller contract
leaves output unchanged; a malformed/non-releasable queued carrier also
returns false with output Empty because it cannot be transferred safely. With
valid output storage, a descriptor/allocation/other diagnosable precommit
failure writes one structurally valid reserved `AbiFailure` into the sole
outcome slot. `Failure` accepts exactly those two false poststates, releases
the reserved diagnostic when present, then cleans and traps; it defines no
produced source owner. Before removing a queued value, the runtime validates
it, allocates and validates the complete `Option`/exception outcome, and
proves it structurally releasable. It must never return true with a malformed
tag, descriptor, or non-releasable payload. On a valid true Success,
Receive/TryReceive dequeue exactly one value for `Some` and dequeue none for
`None`; Receive cancellation dequeues none. A closed and drained queue is the
same successful `None` as an open empty nonblocking queue; only Send reports
the declared `ChannelClosed`. `InvalidOutcome` is the
defensive generated edge for a structurally valid owned outcome that
nevertheless violates the closed declaration row: Performed for either form,
Raised for Receive/TryReceive, Cancelled for TryReceive, Success with a non-
equivalent `OptionResultType`, or a reserved `AbiFailure` returned as source
Raised. It owns that complete
outcome, releases it exactly once, cleans every other live owner, and traps;
it never forwards it to source CFG. Runtime tests forge every tag/descriptor
case, prove malformed carriers take false/Empty without dequeue, exercise
false with a releasable reserved diagnostic, and use a
structurally valid row-invalid test double to prove release-before-trap on
`InvalidOutcome`.

Every demanded Promise occurrence has exactly one
raw `PromiseDemandProjection`; TypeChecker records its caller-suffix
`EffectRef` from the solver at that syntax point, and
`freezeSemanticBatch` projects it in the owning function scope into a
`ProjectedPromiseDemand`. AST lowering copies that owning fact into
`AwaitPromiseInst::ResumeEffects`. This row is distinct from
`Result::Effects`, which describes the task outcome. Async preparation copies
the stored suffix row into `AwaitOutcomeTask` when it splits the block and
never infers continuation effects from CFG reachability. Missing, duplicate,
foreign-node, or solver-foreign/ill-scoped demand facts are ranged errors. An
open suffix is legal inside a Canonical generic fragment; generic
specialization must close it, and async preparation rejects any executable
`AwaitPromiseInst` whose result or resume row remains open. A fixture encodes,
imports, specializes, and awaits an effect-polymorphic generic to prove the
suffix closes through the ordinary TIRF path.

AST lowering outlines each independent-let RHS exactly once into the private
`RhsFunction` recorded by its binding plan; the RHS is never also emitted in
the enclosing CFG. The function receives same-let dependencies as explicit
parameters in `DependencyIndices` order. References outside that let become
`MakeFunctionInst::AvailableBindings` at the let site, and `Callable` is the
resulting exact-arity function value. The verifier requires `Callable` to name that
`RhsFunction`, requires the explicit-parameter/capture partition to cover every
free binding exactly once, and rejects a body, capture, or dependency that is
not owned by the plan's enclosing function. Async planning submits this
callable; it never attempts to recover or outline an RHS from ordinary outer
instructions. Canonical plans contain no future SSA IDs: async planning keeps
a plan-local binding-index-to-extracted-`ValueId` table while rewriting waves
and fills each emitted `SubmitOutcomeTask::Arguments` only after every
dependency take has defined a dominating result.

For every submitted task, async lifting copies the callable/native work row's
closed operation applications and `MayRaise` fact and forces `MayCancel =
true`. The lifted row is used by `PromiseType`, `AwaitPromiseInst`, every
`AsyncResultContract`, and the eventual runtime descriptor. Await demand
therefore propagates cancellation into the enclosing semantic row even when
the underlying synchronous callable cannot cancel. Platform IO, channel-task,
and GPU submission apply the same lift. The verifier rejects a
submitted-task descriptor or Promise without `MayCancel`, and tests cover
worker-entry cancellation before the callable runs.

`runAsyncPlanning` consumes `PatternCanonical` and produces `AsyncPlanned`.
It validates the copied semantic projections, computes deterministic
topological waves, assigns group-plan IDs, and rewrites independent lets to
explicit create/submit/join/take operations. It also rewrites each
`TaskSpawnInst` to one exact callable `SubmitOutcomeTask`, preserving its
Promise TypeId and async-lifted result/effect contract; the explicit source
spawn is ungrouped unless the semantic structured-concurrency fact names the
current group. Each `NativeAsyncCallInst` becomes exactly one
`SubmitOutcomeTask`: ThreadPool selects the generated callable adapter, while
DedicatedOutcome selects the manifest-authenticated Outcome FunctionId. The
planner validates zero/one/N arity, argument ownership, exact Promise result,
closed work row, and mandatory lifted Cancel bit before publishing any
rewrite. It deliberately leaves each
Parallel `GeneratorPlan` intact with its assigned group-plan seed. The final
Task 11 `runGeneratorLowering` consumes `AsyncPlanned`, uses that seed while it
owns cursor/pattern lowering, outlines one exact element worker, and emits the
same explicit async operations; Serial plans use the ordinary cursor path.
Both forms are gone in `GeneratorLowered`, and ControlFlowLowering preserves
the explicit async operations and `AwaitPromiseInst` while eliminating
decision pseudo-control.
Concretely, `GeneratorPlan::GroupPlan` is empty through `PatternCanonical`;
`runAsyncPlanning` fills it exactly for Parallel plans and keeps it empty for
Serial plans. At `AsyncPlanned` the phase verifier requires that partition,
validates the ID in the module task-group arena and its parent/result shape,
and `runGeneratorLowering` consumes the referenced plan before erasing the
generator. Builder, canonical text, clone/remap, and phase tests cover a
foreign seed, a missing Parallel seed, a forbidden Serial seed, and successful
seed consumption with no residual GeneratorPlan.
`Module` owns deterministic arenas for independent-let and task-group plans.
Canonical, GenericPrepared, and PatternCanonical admit
`AwaitPromiseInst`, `TaskSpawnInst`, and `NativeAsyncCallInst`;
PatternCanonical also admits independent-let markers and
GeneratorPlans but no prepared task/group operation. AsyncPlanned admits
`AwaitPromiseInst`, GeneratorPlans, and explicit create/submit/join/take
operations but no independent-let marker, `TaskSpawnInst`, or
`NativeAsyncCallInst`; GeneratorLowered and ControlFlow
forbid all GeneratorPlans while preserving those forms. `runAsyncPreparation`
splits the containing block at each `AwaitPromiseInst`, selects its Move/Keep
mode, and replaces it with `AwaitOutcomeTask`; `AsyncPrepared` forbids every
`AwaitPromiseInst` and additionally requires every task/group value to have canonical
Promise/OutcomeTaskGroup type, every adapter to be predeclared, and every
await mode/result contract to be fixed. It admits exactly the four prepared
`Channel*Inst` forms and forbids every `Channel*Runtime` form. Printer/parser, clone/remap,
operand/successor/free-variable/ownership/cleanup visitors, phase verifier,
and LLVM block lowering cover every arena and form; no generic visitor may
silently skip an async-owned ValueId or FunctionId.
The TaskSpawn verifier requires a closed zero-argument callable whose result,
ownership, work row, and async lift exactly equal the instruction's Promise;
printer/parser, clone/remap, free-variable, phase, and async-planning tests
cover valid generic specialization plus wrong arity/type/effect/phase. The
Task 14 registry regression preserves `CancellationCheck=15` and also proves
`TaskSpawn=16` has no RuntimeEntry row or native lookup; the former is consumed
only into `CancellationPointInst` and the latter only into async planning.
The same exhaustive surfaces cover `NativeAsyncCallInst`; tests exercise
zero/one/N ThreadPool applications, direct dedicated-Outcome applications, pass/return
of their Promise, transparent demand, and a v2-imported declaration, plus
forged synchronous, first-class, partial, wrong-result, wrong-ownership, and
wrong-effect cases.
`AsyncResultContract::RuntimeEffects` and `RuntimeDescriptor` are empty through
ClosureConverted and are required from OperationInstantiated until the async
form is rewritten to its runtime terminator. Its semantic `Effects` field
remains immutable for diagnostics and verifier cross-checking. The Promise type
itself is not mutated; the module's checked `AsyncResultDescriptors` arena owns
one interned static descriptor plan for each reachable exact result/
ownership/runtime-row/Raised-constraint tuple.

Task 14 extends Task 13's phase allowlists and its existing
`runOperationInstantiation` transaction. `EffectPrepared` and
`ClosureConverted` admit the closed-semantic task/group operations and
`AwaitTaskWithContinuation`, plus `ChannelCreateInst`, `ChannelSendInst`,
`ChannelReceiveInst`, and `ChannelTryReceiveInst`; operation instantiation
requires each prepared channel form's semantic row to be closed and closes every reachable
`PromiseType::Effects` and `AsyncResultContract`, installs its
`RuntimeEffectRowId`, checks exact Success ownership plus mandatory
`MayCancel`, interns the immutable `YonaAsyncResultDescriptor` plan and stores
its `AsyncResultDescriptorId`, and closes every
await transition table from that result row plus the exact caller suffix.
`EffectOutlined` admits only those closed async forms and the four still-
prepared channel forms. `AsyncPrepared` through `AcceleratorSelected` preserve
the prepared forms. `ControlOutcomeLowered` forbids all four prepared channel
forms and admits only their four closed runtime terminators;
`RepresentationSelected` through `LlvmReady` preserve those runtime forms and
their source-outcome/internal-failure contracts unchanged. A Promise/async record
with an open row, missing cancellation, different result type/ownership, or a
Performed continuation whose final result differs from the Promise fails the
same atomic transaction before publication.

Async preparation copies each channel operation's exact instantiated
declaration row into its prepared record. That row is closed, has zero
operations and no tail; its `MayRaise` bit equals the declaration exactly,
and `MayCancel` is exactly true for blocking Send/Receive and false for
Create/TryReceive. Operation instantiation re-resolves the same declaration
identity and requires byte-for-byte equality of canonical row bytes plus both
bits—subset or superset compatibility is forbidden. A forged operation, tail,
raise bit, or cancel bit aborts the transaction before any runtime row or
descriptor is published. Tests cover all four valid rows and each one-field
forgery.

Independent RHSs of one multi-binding `let` are partitioned by the semantic
dependency DAG into deterministic topological waves. Each wave owns one scoped
group, submits its antichain in declaration order, joins, extracts successful
results, then enables the next wave; the body starts only after all required
waves succeed. `[| body for binding = source ]` uses
`ParallelComprehension`, submits one task per element, and extracts successful
values in source-index order regardless of completion order. Empty/one/N
groups, linear element transfer, duplicate dependency edges, and deterministic
ordering are explicit tests. Ordinary serial comprehensions keep Task 11's
cursor lowering.

Grouped submissions are issued sequentially in declaration/source-iteration
order even though their workers run concurrently. The runtime reserves and
publishes each accepted child at the next group ordinal in the same commit as
task publication; a rejected submission publishes neither task nor ordinal and
the compiler takes the plan's failure cleanup edge instead of joining a partial
result as success.

The group is the sole join/lifetime coordinator and ordered result owner; join
itself never claims a child outcome. Each grouped submission receives a task
owner only to satisfy the uniform submit API and immediately releases that
owner after the group's membership commit, leaving the group as the sole
result claimant. Each task records only an atomic child-local membership state
(`Unattached`, `Attached`, `GroupClaimed`, or `GroupDrained`) and source
ordinal; it never dereferences group storage. Individual AwaitMove/AwaitKeep
accepts only `Unattached`; `Attached` is rejected with owner and Empty output
unchanged both before and after group join, so only ordered group extraction
can claim that outcome. `GroupClaimed` and `GroupDrained` likewise remain
unawaitable. Group join waits for every child, marks the unique group
Joined without reading or consuming any child outcome, and returns
`Success(Unit)` while leaving the group owner live. Only afterward does
`TakeJoinedGroupResult` move the next child outcome and advance the group's
source-order cursor. A wait-context cancellation requests cancellation, joins
every child, marks the group Joined, and returns `Cancelled(Unit)` without
consuming it; compiler cleanup then releases the group and drains every
unclaimed outcome. Group destruction before or after join performs the same
cancel-if-needed, join, and ordered drain. This supports runtime-sized parallel
comprehensions and heterogeneous fixed let waves without cloning.

Extraction, not group join, owns four-way child routing. Fixed plans carry one
contract per submission; repeated plans carry one element contract and may
submit a runtime-sized count. `TakeJoinedGroupResult` validates the next
stored outcome against the expected contract before moving it. If it produces
Performed, EffectPreparation captures already-extracted results, accumulator/
loop state, and the one remaining group owner in the caller suffix, so
resumption continues with the group's advanced cursor without rerunning
siblings or losing a Success owner. Raised/Cancelled traverse that same suffix
cleanup and release the group exactly once. The first non-Success is therefore
deterministic by source index even when completion order differs.

A possibly-performing joined-result take does not build its continuation
closure before the take. `AwaitTaskWithContinuation` uses
`DeferredGroupContinuationFrame`: it records the already-predeclared frame
FunctionId and deterministic capture ValueIds, including the unique group
owner at a fixed ordinal, but no callable ValueId exists yet. The runtime take
holds an exclusive mutable loan of that owner, advances the cursor, and threads
the same owner plus all untouched captures as explicit arguments to each true
outcome successor; false leaves them unchanged. On Success the successor
transactionally constructs the frame and immediately invokes it with the child
value. On Performed it constructs the same frame and appends it to the request.
Raised/Cancelled construct no frame and clean the group/captures directly.
Thus the linear group moves into a frame only after the take has finished, no
retain is needed, and a failed frame allocation still has the request, group,
and captures available for exact cleanup. A ready ValueId frame is legal only
for `AwaitTaskSource`. Parser/printer/remap, effect, ownership, cleanup, and
phase verifiers preserve this tagged timing rule; tests cover Performed after
several successful takes and forced frame-allocation failure.

`runAsyncPreparation` consumes `ControlFlow` and produces `AsyncPrepared`. It
outlines every required exact worker/native/group wrapper function, registers
their universal-adapter requirements, rewrites
submission strategies, and records task/group ownership before cleanup/effect
preparation. The prepared channel forms remain explicit and are preserved by
cleanup preparation, effect preparation, closure conversion, operation
instantiation, effect finalization, tail-call lowering, and accelerator selection until
control-outcome lowering owns their context/outcome expansion. `Keep` is mandatory for every Keep-safe Promise observation:
the exact Success is Trivial or statically `ALWAYS_SHAREABLE` Owned and the
row has no operation and cannot raise. If that observation is also the local
last use, ordinary ownership lowering releases the still-owned task immediately
after the Keep; it does not substitute Move. `Move` is reserved for a
non-Keep-safe Promise handle, whose ABI `TryRetain` always fails and whose
linear provenance proves one external source owner. This makes a generated
Move's runtime `ExternalClaimCount == 1` check invariant even across function
boundaries; a merely local final-use proof is insufficient and forbidden. A
Keep of a linear/non-Shareable or perform/raise-capable result is rejected
because a one-shot request/exception outcome cannot be cloned. The Promise ABI
descriptor is `ALWAYS_SHAREABLE`, and task `TryRetain` can succeed, exactly for
the Keep-safe predicate; all other Promise handles are linear to generated
code. The task value is consumed only by Move. All async wrapper functions therefore
exist before cleanup preparation; Task 9's single closure conversion consumes
the registered requirements, creates the universal adapter functions, and
then freezes the function set. No later pass creates either kind.

EffectPreparation treats `AwaitOutcomeTask` and `TakeJoinedGroupResult` as
possibly-performing boundaries only when their exact result effect row
contains an operation. It splits that suffix and emits
`AwaitTaskWithContinuation` with the source kind/mode, tagged ready/deferred
continuation plan, transition table, and the containing function's exact
dominating `BoundaryContextParameter`; effect/phase verification requires
identity, type, domain, and dominance rather than accepting another pointer.
Operation-free task extraction and every `AwaitOutcomeGroup` survive to
control-outcome lowering; a group wait has exactly Trivial Unit Success and a
closed row with no raise or operation and mandatory `MayCancel = true`.
Async preparation propagates that cancellation fact into the containing
function's closed row, thereby requiring ExplicitOutcome and its exact
`ExecutionContextParameter`; verification rejects a group wait or enclosing
row that omits it.
Control-outcome lowering invokes each runtime await exactly once: Success
applies the suffix, Performed appends it using the stored transition table,
and Raised/Cancelled forward through prepared try/handler/cleanup boundaries.
Concretely, it replaces `CreateOutcomeTaskGroup` and `SubmitOutcomeTask` with
`CreateOutcomeTaskGroupRuntime` and `SubmitOutcomeTaskRuntime`; the latter
carries the already-interned result descriptor and either a callable ValueId
for `YonaRuntimeOutcomeTaskSubmitMove` (an outlined Yona worker or ThreadPool
native adapter), or an exact DedicatedOutcome NativeExtern FunctionId whose replacement
platform/channel/GPU Outcome ABI was fixed by async preparation. A
Synchronous native target is lowered as the ordinary underlying call and
never appears in either submit form. Both prepared and runtime verifiers
reject raw Synchronous/ThreadPool FunctionIds and reject a DedicatedOutcome target
hidden behind an arbitrary callable ValueId. It replaces operation-free
`AwaitOutcomeTask`, every `AwaitOutcomeGroup`, and operation-free
`TakeJoinedGroupResult` with their three explicit runtime terminators.
`AwaitTaskWithContinuation` becomes the matching task/take runtime terminator:
Success invokes the ready suffix frame or first constructs the deferred group
frame; Performed enters `AppendContinuationFrameToRequest` with the ready or
newly constructed frame; Raised/Cancelled release a ready unused frame, while
the deferred path constructs none and cleans its still-explicit captures. A
Success ordinary suffix `InvokeOutcome` forwards the record's
`BoundaryContext` whenever that ambient contract satisfies all frame recipes.
Only when a required local boundary is absent from ambient context and the
enclosing function separately owns its matching unique state loan does
lowering use `InvokeBoundaryFrameOutcome`, with the record value as
`ParentBoundaryContext`. The blocking task wait receives only its
separate `ExecutionContext`; the nonblocking joined take receives neither
context, because the boundary value belongs to its successor call. On Performed the frame is appended and
later receives the resume loop's Chain view. These rules apply identically to
Ready and DeferredGroup continuation frames; no async pass infers a parent
from a callable environment.
Group wait exposes only Success(Unit) and Cancelled; it has
no Raised/Performed successor and never manufactures a child outcome.
In the same transaction it replaces `ChannelCreateInst`, `ChannelSendInst`,
`ChannelReceiveInst`, and `ChannelTryReceiveInst` with their four runtime
forms, supplies the already-dominating execution-context parameter to the two
blocking calls, and materializes their exact source-outcome plus internal
failure poststates. It does not create a context in async preparation or LLVM.
The same transaction fills `AwaitOutcomeTaskRuntime::ExecutionContext` and
`AwaitOutcomeGroupRuntime::ExecutionContext` from the containing function's
already-dominating `ExecutionContextParameter`. Operand/remap and phase
verification require those exact in-function Borrowed values; neither LLVM nor
the runtime may rediscover an ambient context. Joined-result extraction does
not block and therefore has no execution-context operand. Tests place group
wait in an otherwise pure function and require the inferred ExplicitOutcome
ABI plus its exact context operand; a forged `MayCancel=false` WaitResult or
containing row is rejected before lowering.

After the rewrite, `ControlOutcomeLowered` forbids `CreateOutcomeTaskGroup`,
`SubmitOutcomeTask`, `AwaitOutcomeTask`, `AwaitOutcomeGroup`,
`TakeJoinedGroupResult`, `AwaitTaskWithContinuation`, and all four prepared
`Channel*Inst` forms, but admits the five
closed OutcomeTask `*Runtime` terminators plus the four checked channel
terminators through `RepresentationSelected`,
`OwnershipLowered`, `CleanupLowered`, and `LlvmReady`. Their parser/printer,
operand/successor/remap visitors, phase/async/ownership/cleanup verifiers, and
`LlvmBlockLowerer` cases are exhaustive. Each runtime terminator emits exactly
one matching OutcomeTask/platform/channel/GPU ABI call; async
submit/await/take forms use only their stored `AsyncResultDescriptorId`, while
the channel forms use only their exact stored `ChannelDescriptorId`. Every form
initializes results solely as produced successor
arguments, and follows `TrapCompilerFailure` on false with every precommit
owner unchanged. LLVM does not rediscover `NativeAsyncKind`, select a backend,
invent an outcome edge, or lower any high-level async form.

The async verifier checks group dominance/lifetime, one claim, exact result
contracts under the closed Trivial/Owned result-ownership rule, await ownership mode,
dependency-wave order, parallel result indices, `AsyncKind` selection,
cleanup on every non-Success, and phase legality of every async form. It
requires `TrapCompilerFailure` for compiler-created group/scheduler
infrastructure and every other runtime-false edge. Reserved `AbiFailure`,
wrong-kind/type, or malformed diagnostics are released before that trap. A
backend's declared source error is instead a true-returning task submission
whose eventual outcome has the exact public nominal exception type. Thus
forced OOM and a declared backend failure at the same site cannot be confused.
For a
Fixed shape it proves one successful take per listed contract in order;
for a Repeated shape it proves every take uses the element contract and the
submission/extraction loop carries the same runtime count. Join dominates all
takes, no task handle from a grouped submission escapes, and every path either
exhausts or releases the unique group claimant. Required regressions cover
prepared/runtime channel phase snapshots; early runtime and late prepared-form
rejection; missing/foreign/wrong task/group/channel wait contexts; exact LLVM C
argument forwarding; and the rule that blocking Send/Receive require the
containing `ExecutionContextParameter` while Create/TryReceive forbid one. They also cover
perform → await → outer-handler resume, raise/cancel while awaiting, a
try catch after resumed await, group first-failure ordering, independent-let
dependencies, and parallel-comprehension Performed/Raised/Cancelled paths.

- [ ] **Step 5: Define whole-outcome async contracts**

Add final APIs in the ABI-distinct `OutcomeTask.h` alongside the still-frozen
legacy implementation:

```c
typedef struct YonaOutcomeTask *YonaOutcomeTaskRef;
typedef struct YonaOutcomeTaskGroup *YonaOutcomeTaskGroupRef;
typedef struct YonaAsyncResultDescriptor {
  uint32_t AbiVersion;
  uint32_t Reserved;
  const YonaAbiTypeDescriptor *SuccessType;
  YonaAbiResultOwnership SuccessOwnership;
  const YonaAbiEffectRowDescriptor *Effects;
  const YonaAbiTypeDescriptor *RaisedConstraintType;
} YonaAsyncResultDescriptor;

bool YonaRuntimeOutcomeTaskGroupCreate(
    YonaOutcomeTaskGroupRef *EmptyOutput,
    YonaControlOutcome *EmptyFailure);
void YonaRuntimeOutcomeTaskGroupRetain(YonaOutcomeTaskGroupRef Group);
void YonaRuntimeOutcomeTaskGroupRelease(YonaOutcomeTaskGroupRef Group);
bool YonaRuntimeOutcomeTaskGroupCancel(YonaOutcomeTaskGroupRef Group);
bool YonaRuntimeOutcomeTaskTryRetain(YonaOutcomeTaskRef Task);
void YonaRuntimeOutcomeTaskRelease(YonaOutcomeTaskRef Task);
bool YonaRuntimeOutcomeTaskCancel(YonaOutcomeTaskRef Task);
bool YonaRuntimeOutcomeTaskSubmitMove(
    YonaCallableRef *OwnedCallable,
    YonaAbiArgument *Arguments,
    uint64_t ArgumentCount,
    const YonaAsyncResultDescriptor *BorrowedResult,
    YonaOutcomeTaskGroupRef BorrowedGroup,
    YonaOutcomeTaskRef *EmptyOutput,
    YonaControlOutcome *EmptyFailure);
bool YonaRuntimeOutcomeTaskCompleteMove(
    YonaOutcomeTaskRef Task,
    YonaControlOutcome *OwnedOutcome);
bool YonaRuntimeOutcomeTaskAwaitMove(
    YonaOutcomeTaskRef *OwnedTask,
    const YonaAsyncResultDescriptor *BorrowedExpected,
    const YonaExecutionContext *BorrowedContext,
    YonaControlOutcome *EmptyOutput);
bool YonaRuntimeOutcomeTaskAwaitKeep(
    YonaOutcomeTaskRef Task,
    const YonaAsyncResultDescriptor *BorrowedExpected,
    const YonaExecutionContext *BorrowedContext,
    YonaControlOutcome *EmptyOutput);
bool YonaRuntimeOutcomeTaskGroupAwaitAll(
    YonaOutcomeTaskGroupRef BorrowedGroup,
    const YonaExecutionContext *BorrowedContext,
    YonaControlOutcome *EmptyOutput);
bool YonaRuntimeOutcomeTaskGroupTakeNextMove(
    YonaOutcomeTaskGroupRef BorrowedUniqueJoinedGroup,
    const YonaAsyncResultDescriptor *BorrowedExpected,
    YonaControlOutcome *EmptyOutput);
```

`GroupRetain` exists for C hosts/runtime tests only. The Typed IR async verifier
forbids generated calls to it; generated code has one linear external group
claimant. `GroupTakeNextMove` additionally checks that unique external claim
before moving an outcome and returns false unchanged if a C host retained an
alias.

Task storage has two disjoint reference domains. One atomic claimant word
stores `(ExternalClaimCount, MoveReserved)`. The count covers only
public/value owners returned by submission or added by
`YonaRuntimeOutcomeTaskTryRetain`; scheduler, worker, IO-request, and group
lifetime references use runtime-private retain/release helpers and never alter
that word. `TryRetain` succeeds only with the CAS
`(n, false) -> (n + 1, false)` for `n >= 1` without overflow and when the
immutable result contract is Keep-safe (Trivial or statically
`ALWAYS_SHAREABLE` Owned Success, no Raise, no operation); it can never
resurrect a zero-claim task. Otherwise it returns false without mutation. It is the Promise ABI
descriptor's `TryRetain` callback.

Before claimant reservation, context polling, waiting, or any mutation, both
await APIs require a non-null, well-formed `BorrowedExpected` and prove full
structural equivalence to the task's immutable stored result descriptor:
Success type, result ownership, and the complete operation/MayRaise/MayCancel
row. Pointer or fingerprint equality is insufficient. A mismatch returns
false with the Move owner or Keep handle and Empty output unchanged.
`AwaitOutcomeTaskRuntime::Result` lowers to exactly that C argument; LLVM may
not rediscover or substitute a descriptor. ABI-order and forced-same-hash
tests vary type, ownership, operation identity, MayRaise, and MayCancel one at
a time.

After that structural owner/output/descriptor validation, `AwaitMove` must
first reserve the unique public claim with the atomic transition `(1, false)
-> (1, true)` before it waits. No retain or Keep observation can start while that reservation is
set. Alias/reservation failure returns false with owner/output unchanged. If a
Poisoned completion or any post-wait ABI validation fails, AwaitMove rolls
back `(1, true) -> (1, false)` before returning false, again preserving the
owner and Empty output. A true terminal/cancellation commit, while holding the
task lock and one runtime-private transient lifetime reference, has exactly
two forms. A terminal-move commit observes a valid published terminal state,
moves that outcome to the validated output, clears the caller slot, and changes
`(1, true) -> (0, false)` before releasing the transient reference. An
observation-cancel commit is legal only while the state remains Pending: it
sets the task cancellation request, writes a fresh `Cancelled(Unit)` to the
output, clears the caller slot, and performs the same claimant transition,
but neither reads nor clears terminal storage. Before the waiter releases its
transient reference, the runtime transfers that reference to the completion/
reaper path or proves that path already owns one, so the eventual terminal
outcome is published and destroyed after the last private reference even
though no external claimant remains. If terminal publication and context
cancellation race, the task lock is the linearization point: a terminal state
already published when the waiter owns the lock takes the terminal-move path;
a waiter that owns the lock while Pending takes the observation-cancel path,
and the later publisher may only store and retire its result. The task lock
orders terminal storage with these transitions; there is no check-then-wait
window in which `TryRetain` can add an alias. Internal
references therefore cannot make a valid unique Move look aliased. Runtime
tests retain a Keep-safe task from C, race Move/Keep/cancel, and prove Move
rejects an existing external alias without touching either owner; a barrier
then starts `TryRetain` after Move has reserved and requires the retain to fail
until commit. A Poisoned post-wait case proves reservation rollback permits a
later legal retain. Linear-result task retain always fails unchanged.
`OutcomeTask.c` also defines private, exact-signature descriptor adapters
`bool outcomeTaskAbiTryRetain(YonaAbiWord)` and
`void outcomeTaskAbiRelease(YonaAbiWord)`. They validate/decode the carrier and
call the typed public functions; the Promise type descriptor points to these
adapters, never to a cast of `YonaRuntimeOutcomeTaskTryRetain` or Release.
`YonaRuntimeAbiValueClone`/Release tests exercise both Keep-safe and linear
Promise words through those callbacks.

`nullptr, 0` is the valid zero-arity argument form. Submit is all-or-nothing:
it first requires `ArgumentCount ==
YonaRuntimeCallableRemainingArity(*OwnedCallable)`. Under- and overapplication
are rejected before staging with unchanged owners and descriptor-mismatch;
the async lowering pass creates an exact-arity generated wrapper whenever the
source expression would otherwise curry or overapply. Runtime submission
never tries to discover descriptors from an intermediate result. It first
validates the explicit immutable `BorrowedResult` against the callable
descriptor: exact Success type/ownership and precisely the
callable row's async lift (same operations and `MayRaise`, forced
`MayCancel`). It then uses Task 9's argument stage to validate/copy/
clone without moving Consume inputs, then allocates the task and queue node and
reserves publication. Async preparation has already required every escaping Borrow argument type to
be statically `ALWAYS_SHAREABLE`; an instance-sensitive Borrow is a compile
error, not a possible runtime submission branch. On success it infallibly
commits the argument stage and
moves/clears the callable immediately before publishing the queued task,
returns one owned task reference, and sets the child-local `Attached` state and
ordinal when a borrowed group atomically adopts the child reference; on
validation/non-Shareable-Borrow/OOM/queue failure
it discards the uncommitted stage, leaves all inputs unchanged, and stores null
in `EmptyOutput`, while writing the exact nonallocating descriptor-mismatch, OOM,
or submission-failure outcome to the initially Empty failure output. Success
leaves that output Empty. Generated async lowering branches on the bool and
requires the sole `RuntimeFailureDisposition`, `TrapCompilerFailure`, for every
false result. A source-declared backend failure instead occurs after successful
publication and completes the owned task with the exact admitted Raised
outcome. No source-level Promise operation can observe a null task, a
status-only error, or the C diagnostic. Task-group creation follows the same
cleanup-and-trap rule for allocation failure. Every handle-producing creation/submission API
requires a non-null initially-null output and a non-null initially-Empty
failure output. Those outputs, the callable owner, the argument-record array,
every mutable Consume slot, and every Trivial/Borrow source are pairwise
byte-range distinct except that immutable sources may alias one another.
Structural storage rejection returns false without reading a carrier or
mutating/writing any slot; all other false returns leave owners unchanged and
write the exact failure outcome. They never inspect or release an arbitrary
preinitialized handle.

`YonaAsyncResultDescriptor` has static module lifetime and is the sole task
outcome contract. The compiler emits it after async lifting and passes its
address to universal callable submission: Success type/ownership are
normalized, operations and `MayRaise` are copied, and `MayCancel` is
unconditionally set. `RaisedConstraintType` is null exactly when `MayRaise`
is false. For an ordinary Yona or effect-polymorphic callable it is the
builtin `ExceptionValue` existential descriptor; for a manifest-owned fixed-
error Outcome leaf it is the exact closed FileError, NetError, GpuError, or
other declared nominal descriptor. Descriptor interning, canonical text,
parser/printer, clone/remap, TIRF/v2 codec, LLVM constant emission, and runtime
equivalence include this field. Submission validates that exact relation against the
callable descriptor; the runtime never synthesizes or substitutes a result
descriptor.
Its ABI version is the shared current runtime ABI version, its reserved word is
zero, and exact 32/64-bit size/offset assertions make the layout identical on
all targets. Runtime validation rejects a wrong version/reserved value or any
invalid referenced type/effect descriptor before publication.
It rejects every unknown/third result-ownership discriminant and any
submitted-task descriptor that omits cancellation. Platform IO,
channel-task, and GPU submission receive the exact generated
async-lifted descriptor explicitly and validate it against their fixed
operation result before publication; no backend guesses an effect row from a
symbol or patches it after task creation.

Completion validates every Raised payload with
`YonaRuntimeAbiValueConforms(BorrowedResult->RaisedConstraintType, Actual)`.
The `ExceptionValue` descriptor admits any correctly flagged exception
nominal; a fixed nominal constraint admits only that exact structural type. A
missing constraint with `MayRaise`, a non-null constraint without it, or a
Raised payload outside the constraint is a malformed internal outcome, not a
source-visible failure. Tests cover exact-error acceptance, wrong error
family, a valid general exception under the existential, null/bit mismatch,
and v2/LLVM descriptor round-trip.

Each task stores the complete closed Promise contract derived from its
callable or native/IO creation site: exact Success descriptor/ownership
and runtime effect row plus Raised constraint. Completion always moves/clears
its source. It accepts Success only with that exact descriptor/ownership,
Raised only when `MayRaise` and the payload conforms to that constraint,
Performed only for an operation contained in the row and a continuation whose
final result is that Promise contract, and Cancelled only when `MayCancel`.
A null or already-terminal task releases the rejected outcome and returns
false. A first terminal outcome with a mismatched contract is also released;
the task atomically enters a distinct terminal Poisoned state with no language
outcome, wakes waiters, and returns false, so an internal adapter bug cannot
leave a Promise pending or manufacture an undeclared Raised effect. AwaitMove
on a Poisoned task returns false with its owner unchanged and Empty output, as
do AwaitKeep, an AwaitMove with `ExternalClaimCount != 1`, and all
structural/alias rejections; group take returns false
without advancing. Generated lowering maps these
verified-unreachable false returns to its internal descriptor-mismatch cleanup
edge, releases every remaining live owner, and ends in a non-returning
ABI-invariant `llvm.trap`; the edge cannot rejoin source control flow or
manufacture a Raised outcome. Tests cover every accepted kind and
each rejected type/ownership/effect-row cell.

Every parameter named `EmptyFailure` or `EmptyOutput` must already contain its
documented null/Empty sentinel and is never released on entry. Await output is
non-null, initially Empty, and byte-range disjoint from its task/group owner
slot and all reachable mutable carrier slots; invalid or overlapping storage
returns false without mutation. On true, AwaitMove uses exactly one of the two
infallible locked orders above: terminal-move transfers the published outcome,
while observation-cancel creates an independent Cancelled observation and
leaves later terminal storage to the private completion/reaper path; both
clear the task-owner slot and atomically consume the reserved sole external
claim.
AwaitKeep requires a Keep-safe descriptor, leaves the task owner unchanged, and
copies only a verifier-approved Trivial/statically-Always-Shareable Success or trivial Cancelled
outcome; a runtime contract violation returns false with Empty output.

All blocking awaits query `BorrowedContext` at entry and immediately before
and after condition-variable waits bounded to at most 10ms; no task, group, or
channel registers a cancellation waiter or relies on a wakeup side channel.
Context cancellation during AwaitMove uses the observation-cancel commit:
while Pending it requests target cancellation, consumes the caller's task
owner and reserved claimant, and returns `Cancelled(Unit)` without claiming a
later terminal outcome. The transferred/existing completion-path private
reference keeps the task alive and eventually releases that outcome. Tests
separately cover terminal-move and observation-cancel commits, assert the
claimant word reaches `(0, false)` in both, and force a nontrivial later
Success/Raised outcome after observation cancellation to prove exact teardown.
AwaitKeep does not cancel a shared target and
returns `Cancelled(Unit)` for this observation only. GroupAwait cancellation
requests group cancellation, joins every child, marks the unique borrowed
group Joined, and returns `Cancelled(Unit)` without consuming it. On its normal
path GroupAwait joins every child without inspecting outcomes, marks the group
Joined, and returns `Success(Unit)`. `GroupTakeNextMove` then validates and
moves one outcome at a time in submission order and advances after every
structurally valid true return, whether the moved kind is Success, Raised,
Performed, or Cancelled. False before join/after exhaustion or on aliasing,
contract mismatch, or a Poisoned child leaves cursor and output unchanged.
Runtime tests take each non-Success kind followed by the next child and final
exhaustion. Releasing the group cancels/joins if necessary
and drains/releases all remaining child outcomes and references. Explicit
task/group cancellation is idempotent; queued work
completes as Cancelled, while running work observes an atomic cooperative
cancellation flag at its declared cancellation points. No API raises in C.
An acquire-true `YonaRuntimeExecutionCancellationRequested` query takes the
generated `ReturnCancelled` cleanup edge; a false query continues. Tests gate
a long-running callable after worker start, cancel its task from another
thread, release the gate, and require the in-flight query—not merely a pre- or
post-call worker check—to return `Cancelled(Unit)` with all owners balanced.
Nested direct and universal calls see the same cancellation state, while two
concurrent tasks' explicit contexts remain isolated.
Task/group reference counts and the pending-to-complete transition are atomic;
outcome storage is protected by the task mutex/condition variable so complete,
await, cancellation, and group join are data-race-free on POSIX and Windows.
The group owns each child reference until ordered extraction/destruction. A
child never retains, releases, or points at its group. Group join/take/drain
updates the child-local membership state under the task lock before releasing
the group's child reference: successful extraction stores `GroupClaimed`, and
destruction stores `GroupDrained` after releasing any unclaimed outcome. A
publicly retained child can therefore outlive group storage safely; subsequent
await returns false unchanged, cancel is an idempotent no-op on an already
terminal/drained child, and release remains valid. Generated code never
releases a group from one of its workers, so the destruction barrier cannot
self-join. This ownership direction admits no task/group strong-reference
cycle even when the caller drops its group owner before completion. ASan/TSan
tests retain an attached child, destroy its group, then concurrently await,
cancel, and release it without dereferencing freed storage. A second race test
retains a child, joins the group, and races child await against group take;
child await remains false while group take alone claims the outcome.

- [ ] **Step 6: Make workers invoke only universal callables**

Define replacement worker records with one owned `YonaCallableRef`, one
committed `YonaAbiArgumentStageRef`, and one runtime-private task lifetime
reference acquired by accepted publication. The worker constructs descriptor-matching
`YonaAbiArgument[]` views over the stage: Trivial/Borrow records point to the
stage-owned values, while Consume records point to their mutable owner slots.
That private reference does not change `ExternalClaimCount`. The worker
constructs a stack
`YonaExecutionContext` whose probe acquire-loads that task's cancel flag, and
the C worker queries it before touching the callable.
If cancelled it releases the callable/staged owners, completes
`Cancelled(Unit)`, wakes waiters, and only then releases the private task
reference;
otherwise this true async root calls
`YonaRuntimeCallableApplyMove(&Worker->Callable, Arguments, ArgumentCount,
NULL, &Context, &Outcome)`.
Every nested explicit-outcome call receives that same pointer; no TLS or
ambient current-task state is installed. After apply returns, the context is
no longer reachable. The worker releases the committed stage so Borrow clones
and any carrier not consumed by apply are released exactly once, checks the
bool and outcome kind, and keeps the private task reference throughout. A
defensive false return (which verified staging makes unreachable) releases the
still-owned callable and stage and terminates with the non-returning
ABI-invariant trap. A true reserved `AbiFailure` is the distinct callee-owns
poststate: callable/Consume inputs are already clear, so the worker releases
the diagnostic and any remaining stage carriers and takes the same trap. Only
a declared Success/Raised/Performed/Cancelled outcome is moved into
`YonaRuntimeOutcomeTaskCompleteMove`; after completion publishes and wakes
waiters, the worker releases its private task reference. It never submits an
`AbiFailure` outcome to normal task completion. A gated regression drops the
sole external task owner immediately after publication and proves the private
reference keeps the task live through completion and wakeup; pre-entry
cancellation and both defensive apply failures cover the other release orders.
It
does not install setjmp, inspect a CType, or translate `(result, IsError)`.

Generated adapters wrap every extern arity, including 0 and 1, and are the
only code that crosses typed native register classes. They are ordinary Typed
IR adapter `FunctionId`s created by Task 9's `runClosureConversion` while the
module is `EffectPrepared`, included in the frozen `ClosureConverted`
function set, and assigned runtime rows by operation instantiation.
`CallableLowering` only lowers those verified functions; it never invents an
LLVM-only or post-instantiation adapter.

- [ ] **Step 7: Normalize platform/channel/GPU completion explicitly**

Add ABI-distinct whole-outcome entry points to IO/kqueue/io_uring/Windows,
channel, native, and GPU code without rewriting the legacy entry points.
No variadic or raw-callback convenience API exists. The following declarations
are the complete replacement public surface; there are no implicit
"analogous" functions:

Task 14 also introduces the platform-neutral internal resource object needed
by these APIs in `Runtime/Platform/ResourceHandle.{h,c}`; Task 15's
source-visible File/Net wrappers reuse it rather than defining another handle.
It stores `YonaPlatformResourceKind::{File,Socket}`, a platform-width native
handle, an atomic owner count, pin count, closing/closed state, and the one
platform close callback. `YonaPlatformResourceCreate` publishes an owned
RESOURCE `YonaAbiValue`; `YonaPlatformResourceTryPin` first validates the
AbiValue family and exact internal kind, then increments the pin only while not
closing; `YonaPlatformResourceUnpin` decrements once and performs a deferred OS
close when the last pin retires; `YonaPlatformResourceCloseMove` validates and
atomically marks closing, clears exactly one consumed source owner on true, and
defers the native close until pins reach zero. False leaves every word/state
unchanged and writes only the reserved diagnostic. Descriptor Release follows
the same owner-retirement path and is infallible. The native handle never
appears in a source value or crosses as `Int`. Dedicated races test close vs
accepted submission, rejected submission vs close, double close, wrong
File/Socket kind, last-owner release with pins, and exactly one OS close on
POSIX and Win32. This internal substrate makes Task 14 independently buildable;
Task 15 adds only checked stdlib entry wrappers and manifest descriptors.

```c
/* Platform/Api.h: every borrowed Yona String/FileHandle/Socket is an exact
 * descriptor-checked YonaAbiValue. String bytes are copied during the call;
 * a FileHandle/Socket acquires a runtime-private resource pin during prepare.
 * OwnedBytes moves only when request preparation commits. Every task output
 * is initially null and becomes one caller-owned reference on an accepted
 * request. BorrowedResult has static lifetime. */
bool YonaRuntimePlatformSubmitFileWriteOutcome(
    const YonaAbiValue *BorrowedPathString,
    const YonaAbiValue *BorrowedContentString,
    const YonaAsyncResultDescriptor *BorrowedResult,
    YonaOutcomeTaskGroupRef BorrowedGroup,
    YonaOutcomeTaskRef *EmptyOutput,
    YonaControlOutcome *EmptyFailure);
bool YonaRuntimePlatformSubmitFileByteReadOutcome(
    const YonaAbiValue *BorrowedPathString,
    const YonaAsyncResultDescriptor *BorrowedResult,
    YonaOutcomeTaskGroupRef BorrowedGroup,
    YonaOutcomeTaskRef *EmptyOutput,
    YonaControlOutcome *EmptyFailure);
bool YonaRuntimePlatformSubmitFileDescriptorByteReadOutcome(
    const YonaAbiValue *BorrowedFileHandle, int64_t Count,
    const YonaAsyncResultDescriptor *BorrowedResult,
    YonaOutcomeTaskGroupRef BorrowedGroup,
    YonaOutcomeTaskRef *EmptyOutput,
    YonaControlOutcome *EmptyFailure);
bool YonaRuntimePlatformSubmitFileDescriptorByteWriteOutcomeMove(
    const YonaAbiValue *BorrowedFileHandle,
    YonaAbiValue *OwnedBytes,
    const YonaAsyncResultDescriptor *BorrowedResult,
    YonaOutcomeTaskGroupRef BorrowedGroup,
    YonaOutcomeTaskRef *EmptyOutput,
    YonaControlOutcome *EmptyFailure);
bool YonaRuntimePlatformSubmitFileDescriptorStringWriteOutcome(
    const YonaAbiValue *BorrowedFileHandle,
    const YonaAbiValue *BorrowedString,
    const YonaAsyncResultDescriptor *BorrowedResult,
    YonaOutcomeTaskGroupRef BorrowedGroup,
    YonaOutcomeTaskRef *EmptyOutput,
    YonaControlOutcome *EmptyFailure);
bool YonaRuntimePlatformSubmitFileDescriptorStringsWriteOutcome(
    const YonaAbiValue *BorrowedFileHandle,
    const YonaAbiValue *BorrowedFirstString,
    const YonaAbiValue *BorrowedSecondString,
    const YonaAsyncResultDescriptor *BorrowedResult,
    YonaOutcomeTaskGroupRef BorrowedGroup,
    YonaOutcomeTaskRef *EmptyOutput,
    YonaControlOutcome *EmptyFailure);

bool YonaRuntimePlatformSubmitNetRecvBytesOutcome(
    const YonaAbiValue *BorrowedSocket, int64_t Count,
    const YonaAsyncResultDescriptor *BorrowedResult,
    YonaOutcomeTaskGroupRef BorrowedGroup,
    YonaOutcomeTaskRef *EmptyOutput,
    YonaControlOutcome *EmptyFailure);
bool YonaRuntimePlatformSubmitNetSendOutcome(
    const YonaAbiValue *BorrowedSocket,
    const YonaAbiValue *BorrowedString,
    const YonaAsyncResultDescriptor *BorrowedResult,
    YonaOutcomeTaskGroupRef BorrowedGroup,
    YonaOutcomeTaskRef *EmptyOutput,
    YonaControlOutcome *EmptyFailure);
bool YonaRuntimePlatformSubmitNetSendBytesOutcomeMove(
    const YonaAbiValue *BorrowedSocket, YonaAbiValue *OwnedBytes,
    const YonaAsyncResultDescriptor *BorrowedResult,
    YonaOutcomeTaskGroupRef BorrowedGroup,
    YonaOutcomeTaskRef *EmptyOutput,
    YonaControlOutcome *EmptyFailure);
bool YonaRuntimePlatformSubmitTcpAcceptOutcome(
    const YonaAbiValue *BorrowedListenerSocket,
    const YonaAsyncResultDescriptor *BorrowedResult,
    YonaOutcomeTaskGroupRef BorrowedGroup,
    YonaOutcomeTaskRef *EmptyOutput,
    YonaControlOutcome *EmptyFailure);
bool YonaRuntimePlatformSubmitTcpConnectOutcome(
    const YonaAbiValue *BorrowedHostString, int64_t Port,
    const YonaAsyncResultDescriptor *BorrowedResult,
    YonaOutcomeTaskGroupRef BorrowedGroup,
    YonaOutcomeTaskRef *EmptyOutput,
    YonaControlOutcome *EmptyFailure);
bool YonaRuntimePlatformSubmitUdpRecvBytesOutcome(
    const YonaAbiValue *BorrowedSocket, int64_t Count,
    const YonaAsyncResultDescriptor *BorrowedResult,
    YonaOutcomeTaskGroupRef BorrowedGroup,
    YonaOutcomeTaskRef *EmptyOutput,
    YonaControlOutcome *EmptyFailure);
bool YonaRuntimePlatformSubmitUdpSendToBytesOutcomeMove(
    const YonaAbiValue *BorrowedSocket,
    const YonaAbiValue *BorrowedHostString, int64_t Port,
    YonaAbiValue *OwnedBytes,
    const YonaAsyncResultDescriptor *BorrowedResult,
    YonaOutcomeTaskGroupRef BorrowedGroup,
    YonaOutcomeTaskRef *EmptyOutput,
    YonaControlOutcome *EmptyFailure);

/* Channel.h: the descriptor bundle has static lifetime and is generated from
 * one closed Sender/Receiver instantiation. Role discriminants are normative.
 * The runtime uses distinct typed refs after checked YonaAbiValue extraction. */
typedef enum YonaChannelEndpointRole {
  YONA_CHANNEL_ENDPOINT_SENDER = 0,
  YONA_CHANNEL_ENDPOINT_RECEIVER = 1
} YonaChannelEndpointRole;
typedef struct YonaRuntimeChannelSender *YonaChannelSenderRef;
typedef struct YonaRuntimeChannelReceiver *YonaChannelReceiverRef;
typedef struct YonaAbiChannelDescriptor {
  uint32_t AbiVersion;
  uint32_t Reserved;
  uint64_t StructuralFingerprint;
  const uint8_t *CanonicalBytes;
  uint64_t CanonicalByteCount;
  const YonaAbiTypeDescriptor *PayloadType;
  const YonaAbiTypeDescriptor *SenderType;
  const YonaAbiTypeDescriptor *ReceiverType;
  const YonaAbiTypeDescriptor *EndpointPairType;
  const YonaAbiTypeDescriptor *OptionPayloadType;
  const YonaAbiTypeDescriptor *ChannelErrorType;
  uint64_t InvalidCapacityConstructorTag;
  uint64_t ClosedConstructorTag;
} YonaAbiChannelDescriptor;

/* Creation returns a whole outcome. On a structurally valid call, negative
 * capacity is true + Raised(ChannelInvalidCapacity); Success owns one exact
 * (Sender a, Receiver a) pair. Send always consumes/clears OwnedValue after
 * commit and can Raise only ChannelClosed. Receive and tryReceive never Raise;
 * a closed, drained queue returns Success(None). Blocking calls poll the
 * explicit context. Result outputs are initially Empty and storage-distinct. */
bool YonaRuntimeChannelCreateOutcome(
    int64_t Capacity, const YonaAbiChannelDescriptor *BorrowedDescriptor,
    YonaControlOutcome *EmptyOutput);
bool YonaRuntimeChannelSendOutcomeMove(
    const YonaAbiChannelDescriptor *BorrowedDescriptor,
    const YonaAbiValue *BorrowedSender, YonaAbiValue *OwnedValue,
    const YonaExecutionContext *BorrowedContext,
    YonaControlOutcome *EmptyOutput);
bool YonaRuntimeChannelReceiveOutcome(
    const YonaAbiChannelDescriptor *BorrowedDescriptor,
    const YonaAbiValue *BorrowedReceiver,
    const YonaExecutionContext *BorrowedContext,
    YonaControlOutcome *EmptyOutput);
bool YonaRuntimeChannelTryReceiveOutcome(
    const YonaAbiChannelDescriptor *BorrowedDescriptor,
    const YonaAbiValue *BorrowedReceiver,
    YonaControlOutcome *EmptyOutput);

/* Gpu/Api.h: every entry consumes one FloatArray/buffer owner only at commit.
 * Success(FloatArray) returns the mutated owner after fence retirement. A bool
 * false leaves owners unchanged and writes one complete failure outcome. */
bool YonaStdGpuFloatArrayMul2OutcomeAsync(
    YonaAbiValue *OwnedFloatArray,
    const YonaAsyncResultDescriptor *BorrowedResult,
    YonaOutcomeTaskGroupRef BorrowedGroup,
    YonaOutcomeTaskRef *EmptyOutput,
    YonaControlOutcome *EmptyFailure);
bool YonaStdGpuFloatArrayScaleOutcomeAsync(
    double Scale, YonaAbiValue *OwnedFloatArray,
    const YonaAsyncResultDescriptor *BorrowedResult,
    YonaOutcomeTaskGroupRef BorrowedGroup,
    YonaOutcomeTaskRef *EmptyOutput,
    YonaControlOutcome *EmptyFailure);
bool YonaRuntimeGpuVulkanFloat64BufferScaleOutcomeAsync(
    double *BorrowedElementsFromOwner, uint32_t Count, double Scale,
    YonaAbiValue *OwnedElementsOwner,
    const YonaAsyncResultDescriptor *BorrowedResult,
    YonaOutcomeTaskGroupRef BorrowedGroup,
    YonaOutcomeTaskRef *EmptyOutput,
    YonaControlOutcome *EmptyFailure);
bool YonaRuntimeGpuVulkanFloat64BufferMultiply2OutcomeAsync(
    double *BorrowedElementsFromOwner, uint32_t Count,
    YonaAbiValue *OwnedElementsOwner,
    const YonaAsyncResultDescriptor *BorrowedResult,
    YonaOutcomeTaskGroupRef BorrowedGroup,
    YonaOutcomeTaskRef *EmptyOutput,
    YonaControlOutcome *EmptyFailure);
```

Every bool-returning task handle producer in this surface uses the same two-output
contract: the handle slot is non-null and initially null, the failure slot is
non-null and initially Empty, and both are byte-range distinct from one
another and from every input owner/source range they could overwrite. A
structural-storage rejection changes nothing; descriptor, allocation, copy,
clone, or prepublication submission failure returns false with owned inputs
unchanged and writes its exact nonallocating outcome; success publishes one
owned handle and leaves the failure Empty. Channel creation instead uses its
dedicated whole-outcome contract: a valid nonnegative capacity publishes
`Success(Owned (Sender a, Receiver a))`, a negative source capacity returns true with the
exact declared Channel error in `Raised`, and an invalid payload descriptor,
bad storage, or OOM is the internal false/diagnostic edge with no source
outcome. The compiled `Std\Channel` surface and its declaration agree on that
Raised row; no source-controlled capacity reaches cleanup-and-trap merely for
being negative.

Every `Borrowed*String` is a `YonaAbiValue` whose descriptor must be the exact
canonical String descriptor. The adapter snapshots its length and bytes during
the call. A path or host containing embedded NUL is source data: after all
structural checks, the platform API publishes a completed task containing the
exact declared file/network error as `Raised` and returns true. Only a bad
String/result descriptor, invalid storage, copy/allocation failure, or other
prepublication infrastructure failure returns false for cleanup-and-trap;
ordinary file content and network-send strings copy and preserve the exact
byte length, including embedded NUL, and never use `strlen`. Each platform/GPU
submit validates `BorrowedResult` against its fixed Promise Success type,
exact structurally derived ownership, and closed effect row and installs that same descriptor
in the task/IO request.
Every FileHandle/Socket input is a borrowed, exact resource `YonaAbiValue`.
During precommit the runtime validates family, canonical descriptor, live
state, and operation permissions, then acquires a runtime-private pin on the
resource object; this internal lifetime hold is not the source-visible
TryRetain operation and does not make a linear value Shareable. A false return
releases a staged pin and leaves the source owner unchanged. Once publication
commits, the IO request owns that pin through kernel/backend retirement on
Success, Raised, or Cancelled. Source `close` consumes and clears the sole
owner, marks the object closing, rejects later submissions, and defers the
native close until all committed pins retire. Thus close-versus-in-flight is
safe without duplicating or exposing an fd/SOCKET. Native handles stay inside
the runtime resource object at their platform width and never pass through a
Yona Int or public C ABI integer. `TcpConnect` and `TcpAccept` Success own an
exact `Std\Net.Socket` resource; all other result families are descriptor-
checked exactly. ABI/runtime tests cover wrong-family resources, close during
each pending operation, cancellation retirement, a synthetic high-bit Win64/
ARM64 SOCKET stored internally without truncation, and no descriptor reuse
after OS handle recycling.

`YonaAbiChannelDescriptor` is valid only when reserved is zero, its canonical
bytes/fingerprint equal the complete ordered nested-type/tag tuple, all six
descriptors are non-null and canonical, the five payload-dependent descriptors
belong to the same closed instantiation, Sender/Receiver are ResourceTypes
with the matching ABI kind, the pair is exactly `(Sender a, Receiver a)`, and
Option is exactly `Option a`. `ChannelErrorType` is exactly the closed
`Std\Channel.ChannelError` nominal with nullary constructor tags 0 and 1, and
the two stored tag fields must equal those declarations. Channel runtime code
constructs Raised values only through
`YonaRuntimeAbiAggregateBuildNominalMove(ChannelErrorType, storedTag, ...)`;
it never hard-codes an unvalidated aggregate layout. `YonaChannelEndpointRole`
is encoded as Sender=0 and Receiver=1;
the runtime rejects every value `>= 2` before inspecting queue storage.
Creation allocates one shared queue core plus two distinct typed endpoint
objects and moves both into the pair. A send requires a Sender descriptor and
runtime role, while receive/tryReceive require Receiver; all validate the
payload/Option descriptor against the same bundle. Wrong role, endpoint
family, or element type takes the structural false/unchanged path.

Each channel outcome call requires a non-null initially Empty output; Send
also requires its full output byte range to be disjoint from the mutable value
owner, and all calls validate endpoint/descriptor/context before reading a
carrier. A structural false leaves owner and output unchanged. Once validation
succeeds, Send consumes/clears its owner on every true-return path and writes
one complete outcome; receive/try-receive likewise write exactly once.
Blocking send/receive query the passed context at entry, immediately before
and after every condition-variable wait bounded to at most 10ms. They do not
register a waiter or require cancellation to signal the channel condition;
the next bounded poll observes individual task cancellation even when the
group remains live. Generated IR forwards the same context in the one channel
terminator and emits no point after a possibly committed Send. Runtime tests
instrument the exact entry/pre-wait/post-wait C polls; lowering tests verify
the context operand and absence of synthetic cancellation instructions around
the ABI call. Tests alias Send's
owner/output and require unchanged rejection, then cancel an individual task
blocked in both send and receive and require `Cancelled(Unit)` without a group
cancel. Additional tests pass a Receiver to send, a Sender to receive, a
same-layout endpoint for another element type, an enum role `2`, and a
mismatched pair/Option bundle; each rejects without dequeueing or consuming
the sent owner.

The Std entry points require the exact FloatArray descriptor and implement
functional unique-or-copy submission. During preparation they inspect atomic
uniqueness: an rc==1 array is staged for in-place transfer; an aliased array is
copied into private storage while the original owner remains unchanged. Only
after task/queue publication is reserved does commit clear/release the
caller's owner and give the unique original or private copy to the task.
Existing aliases therefore retain an immutable original and cannot race device
writes. The low-level entry points require a non-Empty, rc==1 managed owner
whose storage contains the borrowed element range; an aliased owner is a
precommit descriptor failure rather than an unsafe pin. Request preparation
clears an accepted owner and publishes the task before Vulkan submission can
race completion. A pre-commit failure leaves the owner unchanged and output
null and writes exact descriptor-mismatch, OOM, or submission failure to the
initially Empty failure output; success leaves it Empty. A post-commit Vulkan
or stub backend failure completes the returned task with
`Raised(complete GpuError)` and the public submit returns `true`; it never
tries to restore the owner or encode failure as a successful integer.
Cancellation before queue submission may complete the task immediately.
After a successful queue submit, cancellation only sets the request's atomic
cancel flag: it must not publish a terminal outcome or release the pin while
the device may still access the borrowed range. The fence worker waits until
the fence signals, or until a device-loss/queue-idle result provides the same
no-further-access guarantee, and only then completes the task with
`Cancelled(Unit)` and releases the array exactly once. Terminal Success moves
the mutated FloatArray owner into the task outcome; failure releases it after
retirement. The operation descriptor and source `Promise` type therefore have
result `FloatArray`, not an integer status. No async API relies on a raw pointer
outliving its owner.

`IoContext.h` is the only internal completion seam used by file, net, OS,
io_uring, kqueue, IOCP, and native submission adapters:

```c
typedef struct YonaOutcomeIoRequest *YonaOutcomeIoRequestRef;

bool YonaRuntimeOutcomeIoRequestCreateMove(
    YonaAbiValue *OwnedPinnedValues, uint64_t PinnedValueCount,
    const YonaAsyncResultDescriptor *BorrowedResult,
    YonaOutcomeTaskGroupRef BorrowedGroup,
    YonaOutcomeIoRequestRef *EmptyRequestOutput,
    YonaOutcomeTaskRef *EmptyTaskOutput,
    YonaControlOutcome *EmptyFailure);
bool YonaRuntimeOutcomeIoRequestIsCancellationRequested(
    YonaOutcomeIoRequestRef BorrowedRequest);
bool YonaRuntimeOutcomeIoRequestCompleteMove(
    YonaOutcomeIoRequestRef *OwnedRequest,
    YonaControlOutcome *OwnedOutcome);
bool YonaRuntimeOutcomeIoRequestCancelMove(
    YonaOutcomeIoRequestRef *OwnedRequest);
```

Creation is the prepare/commit boundary: validate the static
`BorrowedResult` descriptor against the operation and allocate first, then
atomically publish the task/request and move/clear all pins before any OS
submission. Failure before that commit leaves pins unchanged and both outputs
null and writes the exact failure to the initially Empty failure output;
success leaves it Empty. Both output slots must be non-null, initially null, mutually
storage-distinct, and nonoverlapping with the pin array and every pin owner;
`EmptyFailure` is non-null, initially Empty, and byte-range distinct from all
of them. Invalid/overlapping slots are rejected without mutation or a failure
write. Once committed, an
io_uring/kqueue/IOCP submission failure constructs a
typed platform failure outcome, completes the already-returned task, and the
public submit returns `true`; no path restores moved inputs. The request owns
its task reference, exact closed result contract, and pins until its single
terminal move. Completion consumes/clears both owners and delegates to the
task's whole-outcome validator in `YonaRuntimeOutcomeTaskCompleteMove`, so a
backend cannot store a wrong Success type or an undeclared
Raised/Performed/Cancelled outcome. `CancelMove` always consumes/clears the
caller's request owner and atomically requests cancellation. If no backend owns
the operation yet, it constructs exactly one `Cancelled(Unit)` outcome and may
complete immediately. Once io_uring, kqueue, IOCP, a worker, or a GPU queue owns
the operation, an internal request reference and all pins survive until the
backend reports cancellation/completion, or an equivalent teardown barrier
proves that no kernel, worker, or device can access them. Only that retirement
path constructs the single terminal `Cancelled(Unit)` outcome and releases the
pins. A cancellation request is therefore not itself a lifetime boundary.
Low-level backends carry only the opaque request ID/pointer in
`user_data`/kqueue/IOCP state and must construct a typed
`YonaControlOutcome` before calling the sink. They never store a language
function pointer, result `int64_t`, or `IsError` callback.
Accordingly every platform/GPU public `bool` submit returns `false` only for a
pre-commit validation/allocation/copy failure with unchanged owned inputs and
null output plus its exact nonallocating failure outcome. Generated Yona
lowering always releases that diagnostic and cleanup-traps, never manufacturing
a null Promise or an undeclared Raised outcome. Source-visible validation or
backend errors must instead publish a completed task carrying the exact
declared Raised value (or return that typed outcome synchronously) and report
ABI success. After
task publication it returns `true` even when the backend
submission immediately completes that task with a typed failure/status.
Synchronous net/OS native adapters construct and return the same outcome
directly. ThreadPool natives submit only their callable adapters through
`YonaRuntimeOutcomeTaskSubmitMove`; DedicatedOutcome natives call only their
declaration's dedicated task-producing platform Outcome API. No second
generic native-task API is declared.

`test/CMake/outcome_async_api_contract.py` owns an exact
`REQUIRED_OUTCOME_APIS` manifest containing all thirteen platform functions
(six file and seven net), four channel functions, four GPU functions, four
IO-request functions, and all thirteen public task/group functions:

```text
YonaRuntimePlatformSubmitFileWriteOutcome
YonaRuntimePlatformSubmitFileByteReadOutcome
YonaRuntimePlatformSubmitFileDescriptorByteReadOutcome
YonaRuntimePlatformSubmitFileDescriptorByteWriteOutcomeMove
YonaRuntimePlatformSubmitFileDescriptorStringWriteOutcome
YonaRuntimePlatformSubmitFileDescriptorStringsWriteOutcome
YonaRuntimePlatformSubmitNetRecvBytesOutcome
YonaRuntimePlatformSubmitNetSendOutcome
YonaRuntimePlatformSubmitNetSendBytesOutcomeMove
YonaRuntimePlatformSubmitTcpAcceptOutcome
YonaRuntimePlatformSubmitTcpConnectOutcome
YonaRuntimePlatformSubmitUdpRecvBytesOutcome
YonaRuntimePlatformSubmitUdpSendToBytesOutcomeMove
YonaRuntimeChannelCreateOutcome
YonaRuntimeChannelSendOutcomeMove
YonaRuntimeChannelReceiveOutcome
YonaRuntimeChannelTryReceiveOutcome
YonaStdGpuFloatArrayMul2OutcomeAsync
YonaStdGpuFloatArrayScaleOutcomeAsync
YonaRuntimeGpuVulkanFloat64BufferScaleOutcomeAsync
YonaRuntimeGpuVulkanFloat64BufferMultiply2OutcomeAsync
YonaRuntimeOutcomeIoRequestCreateMove
YonaRuntimeOutcomeIoRequestIsCancellationRequested
YonaRuntimeOutcomeIoRequestCompleteMove
YonaRuntimeOutcomeIoRequestCancelMove
YonaRuntimeOutcomeTaskGroupCreate
YonaRuntimeOutcomeTaskGroupRetain
YonaRuntimeOutcomeTaskGroupRelease
YonaRuntimeOutcomeTaskGroupCancel
YonaRuntimeOutcomeTaskTryRetain
YonaRuntimeOutcomeTaskRelease
YonaRuntimeOutcomeTaskCancel
YonaRuntimeOutcomeTaskSubmitMove
YonaRuntimeOutcomeTaskCompleteMove
YonaRuntimeOutcomeTaskAwaitMove
YonaRuntimeOutcomeTaskAwaitKeep
YonaRuntimeOutcomeTaskGroupAwaitAll
YonaRuntimeOutcomeTaskGroupTakeNextMove
```

The 38-name manifest scans the public headers, requires one implementation on
each applicable platform (or the shared runtime), and rejects undeclared
backend-only outcome entry points. Register it as a `ci-contract` CTest and
execute it in every native job.

Replacement channel/native failures return outcomes rather than calling
`YonaRuntimeRaise`. Audit every replacement completion path in `Gpu/Stub.c`:

```text
completed kernel -> Success(FloatArray owner)
task/group cancellation after safe retirement -> Cancelled(Unit)
backend/device/submission failure after commit -> Raised(complete GpuError)
```

Never mechanically map legacy `IsError != 0` to Raised.
`Channel.h` and `Gpu/Api.h` expose complete replacement signatures using only
ABI descriptors/outcomes; their old `YonaTypeDescriptor` declarations remain
legacy-only until deletion in Task 17.

Task 14 stops at the runtime and Typed IR boundary. Task 15, after adding the
compiler-stdlib manifest/resource syntax, rewrites every private leaf in
`Std\Io`, `Std\Channel`, `Std\Task`, and `Std\Gpu` with an explicit
replacement identity naming only the ABI-distinct declarations above (or its
exact Std wrapper). There is no compiler-side spelling map and no symbol is
allowed to expose both signatures. The checked-in v1 interfaces keep
production on the frozen legacy aliases; Task 15's temporary v2 generation
reads the rewritten sources and proves each native linkage resolves to a
replacement function-pointer signature. Its new `Std\File` and `Std\Net`
sources likewise name the thirteen explicit platform Outcome symbols. Task
17 switches the checked-in interfaces atomically, then deletes the now-
unreferenced aliases.

- [ ] **Step 8: Run runtime, ABI, platform, and sanitizer gates**

```bash
python3 scripts/run-focused-tests.py ./out/build/x64-debug-linux/tests \
  -tc='Async outcome*,Async ABI*,task *,Typed IR control outcomes*'
python3 scripts/run-focused-tests.py ./out/build/x64-debug-linux/tests -tc='Typed IR execution*async*,*channel*,*gpu*'
ctest --preset unit-tests-linux -R '^outcome_async_api_contract$' \
  --output-on-failure --no-tests=error
python3 scripts/quality.py --build-dir out/build/x64-debug-linux \
  --preset x64-debug-linux --jobs 2 sanitize
scripts/test-arm64-qemu.sh \
  -tc='*async outcome*,*async ABI*,*channel*,*control outcomes*'
git diff --check
```

Expected: all arity/type/outcome cells pass, no replacement worker references
SJLJ, and queued managed arguments/results balance on x64 and ARM64 QEMU.
Cross-compile the replacement sources for macOS ARM64 and Windows x64/ARM64
where local toolchains exist; Task 16's pre-cutover push requires native Debug
jobs for all three targets before production can switch.

Add a clearly marked test-only outcome-task ABI section to
`docs/typed-ir.md`; public async behavior remains unchanged until Task 17.

- [ ] **Step 9: Commit the async boundary**

```bash
git add include/yona/Runtime/Concurrency include/yona/Runtime/Platform \
  include/yona/Runtime/Gpu/Api.h include/yona/Runtime/Gpu/VulkanDevice.h \
  src/Runtime/Concurrency src/Runtime/Platform src/Runtime/Stdlib/Native.c \
  src/Runtime/Gpu/Stub.c \
  include/yona/Semantics/RuntimeEntryRegistry.h \
  include/yona/Semantics/SemanticModel.h include/yona/Semantics/TypeChecker.h \
  include/yona/Semantics/StructuralTypeProjection.h \
  src/Semantics/SemanticModel.cpp src/Semantics/TypeChecker.cpp \
  src/Semantics/StructuralTypeProjection.cpp \
  include/yona/TypedIr/Async.h include/yona/TypedIr/Callable.h \
  include/yona/TypedIr/Instruction.h include/yona/TypedIr/TypedIr.h \
  include/yona/TypedIr/Builder.h include/yona/TypedIr/AstLowering.h \
  include/yona/TypedIr/Pipeline.h include/yona/TypedIr/Passes/AsyncPlanning.h \
  include/yona/TypedIr/Passes/AsyncPreparation.h \
  include/yona/TypedIr/Verification/AsyncVerifier.h \
  src/TypedIr/AstLowering.cpp src/TypedIr/Builder.cpp src/TypedIr/TypedIr.cpp \
  src/TypedIr/Verifier.cpp src/TypedIr/Printer.cpp src/TypedIr/Parser.cpp \
  src/TypedIr/Pipeline.cpp src/TypedIr/Passes/AsyncPlanning.cpp \
  src/TypedIr/Passes/AsyncPreparation.cpp \
  src/TypedIr/Passes/GeneratorLowering.cpp \
  src/TypedIr/Passes/ControlFlowLowering.cpp \
  src/TypedIr/Passes/EffectPreparation.cpp \
  src/TypedIr/Passes/OperationInstantiation.cpp \
  src/TypedIr/Passes/ControlOutcomeLowering.cpp \
  src/TypedIr/Passes/OwnershipLowering.cpp \
  src/TypedIr/Passes/CleanupPreparation.cpp \
  src/TypedIr/Passes/CleanupLowering.cpp \
  src/TypedIr/Passes/ClosureConversion.cpp \
  src/TypedIr/Analysis/OwnershipAnalysis.cpp \
  src/TypedIr/Analysis/EscapeAnalysis.cpp \
  src/TypedIr/Analysis/FreeVariables.cpp \
  src/TypedIr/Verification/AsyncVerifier.cpp \
  src/TypedIr/Verification/CallableVerifier.cpp \
  src/TypedIr/Verification/EffectVerifier.cpp \
  src/TypedIr/Verification/OwnershipVerifier.cpp \
  src/TypedIr/Verification/EscapeVerifier.cpp \
  src/TypedIr/Verification/CleanupVerifier.cpp \
  src/Codegen/Llvm/CallableLowering.cpp src/Codegen/Llvm/BlockLowerer.cpp \
  src/Codegen/Llvm/FunctionLowerer.cpp \
  test/Runtime test/Semantics/SemanticModelTest.cpp \
  test/Semantics/TypeCheckerTest.cpp \
  test/Semantics/RuntimeEntryRegistryTest.cpp \
  test/Semantics/StructuralTypeProjectionTest.cpp \
  test/Codegen/AsyncAbiTest.cpp \
  test/TypedIr/AsyncPlanningTest.cpp test/TypedIr/AsyncPreparationTest.cpp \
  test/TypedIr/AsyncVerifierTest.cpp test/TypedIr/CleanupVerifierTest.cpp \
  test/TypedIr/PrinterParserTest.cpp \
  test/Fixtures/TypedIr/Async \
  test/CMake/outcome_async_api_contract.py \
  cmake/YonaComponents.cmake CMakeLists.txt docs/typed-ir.md
git add docs/superpowers/plans/2026-09-01-typed-ir-codegen-rewrite.md
git commit -m "feat: move async work through control outcomes"
```

### Task 15: Version interfaces and specialize verified generic IR

**Files:**

- Create: `include/yona/Interface/Version.h`
- Create: `include/yona/Interface/Schema.h`
- Create: `include/yona/Interface/FormatReader.h`
- Create: `include/yona/Interface/FormatWriter.h`
- Create: `include/yona/Interface/Documentation.h`
- Create: `src/Interface/Schema.cpp`
- Create: `src/Interface/FormatReader.cpp`
- Create: `src/Interface/FormatWriter.cpp`
- Create: `src/Interface/Documentation.cpp`
- Create: `include/yona/TypedIr/InterfaceCodec.h`
- Create: `src/TypedIr/InterfaceCodec.cpp`
- Modify: `include/yona/Semantics/RuntimeEntryRegistry.h`
- Modify: `include/yona/Semantics/RuntimeEntryRegistry.def`
- Modify: `src/Semantics/RuntimeEntryRegistry.cpp`
- Create: `include/yona/TypedIr/Specialization.h`
- Create: `src/TypedIr/Specialization.cpp`
- Create: `include/yona/TypedIr/GenericPreparation.h`
- Create: `src/TypedIr/GenericPreparation.cpp`
- Create: `include/yona/Semantics/TypedInterfaceCatalog.h`
- Create: `src/Semantics/TypedInterfaceCatalog.cpp`
- Create: `include/yona/Semantics/TraitResolution.h`
- Create: `src/Semantics/TraitResolution.cpp`
- Create: `include/yona/Semantics/StructuralSchemeImporter.h`
- Create: `src/Semantics/StructuralSchemeImporter.cpp`
- Create: `include/yona/Semantics/StdlibManifest.h`
- Create: `src/Semantics/StdlibManifest.cpp`
- Modify: `include/yona/Model/Types.h`
- Modify: `include/yona/Model/InferType.h`
- Modify: `include/yona/Model/TypeArena.h`
- Modify: `include/yona/Model/TypeEnv.h`
- Modify: `src/Model/TypeArena.cpp`
- Modify: `src/Model/TypeEnv.cpp`
- Modify: `include/yona/Semantics/Unification.h`
- Modify: `src/Semantics/Unification.cpp`
- Modify: `include/yona/Syntax/Ast.h`
- Modify: `include/yona/Syntax/AstVisitor.h`
- Modify: `include/yona/Syntax/AstVisitorImpl.h`
- Modify: `include/yona/Syntax/Lexer.h`
- Modify: `include/yona/Syntax/Parser.h`
- Modify: `src/Syntax/Ast.cpp`
- Modify: `src/Syntax/Lexer.cpp`
- Modify: `src/Syntax/Parser.cpp`
- Modify: `src/Syntax/ParserModule.cpp`
- Modify: `src/Syntax/ParserImpl.h`
- Modify: `src/Syntax/ParserType.cpp`
- Modify: `src/Syntax/YonaStyle.cpp`
- Modify: `site/grammars/yona.tmLanguage.json`
- Modify: `editors/vscode/syntaxes/yona.tmLanguage.json`
- Modify: `editors/vscode/src/test/run.ts`
- Modify: `scripts/check-yona-grammar.sh`
- Modify: `include/yona/Semantics/StructuralTypeProjection.h`
- Modify: `src/Semantics/StructuralTypeProjection.cpp`
- Modify: `include/yona/Semantics/TypeChecker.h`
- Modify: `src/Semantics/TypeChecker.cpp`
- Modify: `include/yona/Semantics/LinearityChecker.h`
- Modify: `src/Semantics/LinearityChecker.cpp`
- Modify: `include/yona/Semantics/SemanticModel.h`
- Modify: `src/Semantics/SemanticModel.cpp`
- Modify: `include/yona/TypedIr/Derivation.h`
- Modify: `src/TypedIr/Derivation.cpp`
- Modify: `include/yona/Support/SourceManager.h`
- Modify: `src/Support/SourceManager.cpp`
- Modify: `include/yona/TypedIr/Instruction.h`
- Modify: `include/yona/TypedIr/AstLowering.h`
- Modify: `include/yona/TypedIr/TypedIr.h`
- Modify: `include/yona/TypedIr/Passes/AsyncPlanning.h`
- Modify: `include/yona/TypedIr/Passes/RuntimeFailureNormalization.h`
- Modify: `src/TypedIr/AstLowering.cpp`
- Modify: `src/TypedIr/Verifier.cpp`
- Modify: `src/TypedIr/Printer.cpp`
- Modify: `src/TypedIr/Parser.cpp`
- Modify: `src/TypedIr/Pipeline.cpp`
- Modify: `src/TypedIr/Passes/AsyncPlanning.cpp`
- Modify: `src/TypedIr/Passes/RuntimeFailureNormalization.cpp`
- Modify: `include/yona/Codegen/Llvm/ModuleLowerer.h`
- Modify: `include/yona/Codegen/Llvm/BlockLowerer.h`
- Modify: `include/yona/Codegen/Llvm/TypeLowering.h`
- Modify: `src/Codegen/Llvm/ModuleLowerer.cpp`
- Modify: `src/Codegen/Llvm/BlockLowerer.cpp`
- Modify: `src/Codegen/Llvm/TypeLowering.cpp`
- Create: `test/Interface/InterfaceV2Test.cpp`
- Create: `test/Interface/InterfaceDocumentationTest.cpp`
- Create: `test/Fixtures/Interface/v2_complete.yonai`
- Create: `test/Fixtures/Interface/v2_complete.sha256`
- Create: `test/TypedIr/InterfaceCodecTest.cpp`
- Create: `test/TypedIr/GenericSpecializationTest.cpp`
- Create: `test/TypedIr/CrossModuleSpecializationTest.cpp`
- Create: `test/TypedIr/LocalGenericPreparationTest.cpp`
- Modify: `test/TypedIr/AsyncPlanningTest.cpp`
- Modify: `test/TypedIr/RuntimeFailureNormalizationTest.cpp`
- Modify: `test/Semantics/RuntimeEntryRegistryTest.cpp`
- Modify: `test/Codegen/LlvmLoweringTest.cpp`
- Modify: `test/Codegen/TypedIrExecutionTest.cpp`
- Create: `lib/stdlib-manifest.toml`
- Create: `scripts/generate_stdlib_manifest.py`
- Create: `test/CMake/stdlib_manifest_contract.py`
- Create: `include/yona/Runtime/Stdlib/Prelude.h`
- Create: `src/Runtime/Stdlib/Prelude.c`
- Create: `include/yona/Runtime/Stdlib/PlatformConstants.h`
- Create: `src/Runtime/Stdlib/PlatformConstants.c`
- Create: `include/yona/Runtime/Stdlib/Convert.h`
- Create: `src/Runtime/Stdlib/Convert.c`
- Create: `include/yona/Runtime/Stdlib/Channel.h`
- Create: `src/Runtime/Stdlib/Channel.c`
- Create: `include/yona/Runtime/Stdlib/Crypto.h`
- Create: `src/Runtime/Stdlib/Crypto.c`
- Create: `include/yona/Runtime/Stdlib/Encoding.h`
- Create: `src/Runtime/Stdlib/Encoding.c`
- Create: `include/yona/Runtime/Stdlib/File.h`
- Create: `src/Runtime/Stdlib/File.c`
- Create: `include/yona/Runtime/Stdlib/Io.h`
- Create: `src/Runtime/Stdlib/Io.c`
- Create: `include/yona/Runtime/Stdlib/Log.h`
- Create: `src/Runtime/Stdlib/Log.c`
- Create: `include/yona/Runtime/Stdlib/Net.h`
- Create: `src/Runtime/Stdlib/Net.c`
- Create: `include/yona/Runtime/Stdlib/Process.h`
- Create: `src/Runtime/Stdlib/Process.c`
- Create: `include/yona/Runtime/Stdlib/Random.h`
- Create: `src/Runtime/Stdlib/Random.c`
- Create: `include/yona/Runtime/Stdlib/Time.h`
- Create: `src/Runtime/Stdlib/Time.c`
- Create: `include/yona/Runtime/Stdlib/Utf16.h`
- Create: `src/Runtime/Stdlib/Utf16.c`
- Modify: `include/yona/Runtime/Stdlib/String.h`
- Modify: `src/Runtime/Stdlib/String.c`
- Modify: `include/yona/Runtime/Codecs/Json.h`
- Modify: `src/Runtime/Codecs/Json.c`
- Modify: `include/yona/Runtime/Codecs/Regex.h`
- Modify: `src/Runtime/Codecs/Regex.c`
- Modify: `include/yona/Runtime/Gpu/Api.h`
- Modify: `include/yona/Runtime/Gpu/VulkanDevice.h`
- Modify: `src/Runtime/Gpu/Cpu.c`
- Modify: `src/Runtime/Gpu/Stub.c`
- Modify: `src/Runtime/Gpu/VulkanDevice.c`
- Modify: `src/Runtime/Gpu/VulkanOperations.c`
- Create: `test/Runtime/StdlibLeafAbiTest.cpp`
- Create: `test/Runtime/FileIteratorAbiTest.cpp`
- Create: `test/Runtime/ChannelResourceAbiTest.cpp`
- Modify: `test/Runtime/JsonAbiTest.cpp`
- Modify: `lib/Prelude.yona`
- Modify: `lib/Std/Constants/Platform.yona`
- Modify: `lib/Std/Convert.yona`
- Modify: `lib/Std/Iterator.yona`
- Modify: `lib/Std/Json.yona`
- Modify: `lib/Std/Math.yona`
- Modify: `lib/Std/Parallel.yona`
- Modify: `lib/Std/Regex.yona`
- Modify: `lib/Std/Stream.yona`
- Modify: `lib/Std/Utf16.yona`
- Modify: `lib/Std/Channel.yona`
- Modify: `lib/Std/Gpu.yona`
- Modify: `lib/Std/Http.yona`
- Modify: `lib/Std/Io.yona`
- Modify: `lib/Std/Task.yona`
- Create: `lib/Std/ByteArray.yona`
- Create: `lib/Std/Crypto.yona`
- Create: `lib/Std/Dict.yona`
- Create: `lib/Std/Encoding.yona`
- Create: `lib/Std/File.yona`
- Create: `lib/Std/FloatArray.yona`
- Create: `lib/Std/Format.yona`
- Create: `lib/Std/IntArray.yona`
- Create: `lib/Std/Log.yona`
- Modify: `lib/Std/List.yona`
- Create: `lib/Std/Net.yona`
- Create: `lib/Std/Path.yona`
- Create: `lib/Std/Process.yona`
- Create: `lib/Std/Random.yona`
- Create: `lib/Std/Set.yona`
- Create: `lib/Std/String.yona`
- Create: `lib/Std/Time.yona`
- Create: `lib/Std/Types.yona`
- Modify: `test/Semantics/InterfaceCatalogTest.cpp`
- Create: `test/Semantics/StructuralSchemeImporterTest.cpp`
- Create: `test/Semantics/TraitResolutionTest.cpp`
- Modify: `test/Semantics/TraitTest.cpp`
- Modify: `test/Semantics/TypeCheckerTest.cpp`
- Modify: `test/Semantics/SemanticModelTest.cpp`
- Create: `test/Semantics/LinearityCheckerTest.cpp`
- Modify: `test/Syntax/AstTest.cpp`
- Modify: `test/Syntax/LexerTest.cpp`
- Modify: `test/Syntax/YonaStyleTest.cpp`
- Modify: `test/TypedIr/DerivationTest.cpp`
- Modify: `cmake/YonaComponents.cmake`
- Modify: `CMakeLists.txt`
- Modify: `docs/typed-ir.md`
- Create: `docs/interface-v2-format.md`

**Interfaces:**

- Consumes: model structural types and verified Typed IR printer/parser.
- Produces: acyclic `.yonai` v2 schema, deterministic reader/writer, opaque
  generic-fragment codec, structural specialization cache, and a v2 semantic
  catalog used only by the replacement pipeline until Task 17.
- Dependency rule: `yona_interface` validates fragment length/framing but does
  not parse or include Typed IR; codec code lives in `yona_typed_ir`.

- [ ] **Step 1: Write red v2 framing/version tests**

Cover exact header, deterministic round trip, complete structural signatures,
byte-count framing, every section/record golden offset, insertion-order
canonical reindexing, randomized raw type/effect binder IDs producing the same
golden bytes, permuted constraint/instance insertion producing the same
canonical order, randomized source IDs/insertion with rewritten fragment
ranges, permuted imported symbols/methods/constructors, duplicate nested keys,
malformed UTF-8/path/endianness/order/counts,
truncated/oversized fragments, unversioned v1, and future versions. Require
stable rebuild diagnostics and the same golden SHA-256 on every native target:

Include open type-parameter and open-row operation declarations: alpha-renamed
raw binder IDs serialize identically, wrong declaration hashes reject for open
and closed signatures, and round-trip never invents a runtime fingerprint.
Specialization tests close an open operation and require Task 13's directly
encoded runtime bytes/fingerprint; `EffectOutlined` rejects any reachable use
that was not closed into an instance.

```cpp
TEST_CASE("Interface v2 rejects unversioned source interfaces") {
  const auto Parsed = yona::interface::v2::parseModule(
      asBytes("MODULE Old\\Module\nFN value FN 0 -> INT\n"), "old.yonai");
  REQUIRE_FALSE(Parsed.has_value());
  CHECK(joinMessages(Parsed.error()).find(
            "incompatible .yonai interface format (found unversioned v1, "
            "expected 2); rebuild this module with the current yonac") !=
        std::string::npos);
}

TEST_CASE("Interface v2 generic body is length framed") {
  auto Module = sampleV2Interface();
  const auto CanonicalFragment = bytes(
      "YONA-TIRF 1\nsources 0\nroot 0\nir 5\nabcde");
  Module.GenericFunctions.front().TypedIrFragment = CanonicalFragment;
  const auto Text = serializeModule(Module);
  REQUIRE(Text.has_value());
  const auto RoundTrip = parseModule(*Text, "roundtrip.yonai");
  REQUIRE(RoundTrip.has_value());
  CHECK(RoundTrip->GenericFunctions.front().TypedIrFragment ==
        CanonicalFragment);
}
```

Create top-level sentinels `Typed IR interface codec: canonical fragments are
ID and source self-contained`, `Generic specialization: identical keys commit one SCC`, and
`Typed IR cross-module specialization: public helper declarations and linkage
are retained`.
They make the codec, specialization, and `*cross-module*` prefixes reachable;
the existing trait suite remains an independently enumerated baseline gate.

Assert serialized output contains no `GENFN_BEGIN`, `GENFN_END`, `GENFN_DEP`,
`GENFN_CTOR`, or source body.

- [ ] **Step 2: Run and confirm v2 APIs are absent**

```bash
cmake --build --preset build-debug-linux --target tests -j2
python3 scripts/run-focused-tests.py ./out/build/x64-debug-linux/tests \
  -tc='Interface v2*,Typed IR interface codec*,Generic specialization*'
```

Expected: compile failures for `Interface/Schema.h` and `InterfaceCodec.h`.

- [ ] **Step 3: Define the model-only v2 schema and format**

Use `namespace yona::interface::v2` and exact version `2`:

```cpp
inline constexpr std::uint32_t CurrentFormatVersion = 2;

enum class Visibility : std::uint8_t {
  Private = 0, Module = 1, Public = 2
};
enum class InterfaceKind : std::uint8_t { Skeleton = 0, Complete = 1 };
enum class LinkageKind : std::uint8_t {
  YonaDefinition = 0, YonaImport = 1, NativeExtern = 2
};
enum class NativeAsyncKind : std::uint8_t {
  Synchronous = 0, ThreadPool = 1, DedicatedOutcome = 2
};
enum class NativeBoundaryRoute : std::uint8_t {
  CheckedDirectV2 = 0, CheckedOutcomeV2 = 1, StableExternal = 2
};
struct Linkage {
  LinkageKind Kind;
  std::optional<model::SymbolIdentity> Target;
  std::optional<std::string> NativeSymbol;
  std::optional<NativeAsyncKind> AsyncKind;
  std::optional<NativeBoundaryRoute> NativeRoute;
};
struct Function {
  std::string Name;
  model::TypeId Signature;
  Visibility Access;
  Linkage SymbolLinkage;
};
struct TraitConstraint {
  model::TypeParameterId TypeParameter;
  model::NominalTypeKey Trait;
  std::vector<model::TypeId> Arguments;
};
using TargetApplicationTemplate = model::TraitTargetApplication;
struct Constructor {
  std::string Name;
  std::uint32_t Tag;
  std::vector<model::TypeId> Fields;
};
struct NominalDeclaration {
  model::NominalTypeKey Key;
  std::vector<model::TypeParameterId> TypeParameters;
  std::vector<Constructor> Constructors;
  Visibility Access;
  bool Opaque;
};
enum class ResourceShareability : std::uint8_t {
  Linear = 0, AlwaysShareable = 1
};
enum class ResourceAbiKind : std::uint8_t {
  GenericResource = 0, ChannelSender = 1, ChannelReceiver = 2
};
struct ResourceDeclaration {
  model::NominalTypeKey Key;
  std::vector<model::TypeParameterId> TypeParameters;
  ResourceAbiKind AbiKind;
  ResourceShareability Shareability;
  std::optional<std::string> TryRetainNativeSymbol;
  std::string ReleaseNativeSymbol;
  Visibility Access;
};
struct ImportedSymbol {
  std::string SourceName;
  std::string LocalName;
  bool Reexported;
};
struct Import {
  model::ModuleIdentity Module;
  std::optional<std::string> Alias;
  std::vector<ImportedSymbol> Symbols;
  bool Wildcard;
};
struct TraitMethod {
  std::string Name;
  model::TypeId Signature;
  std::optional<TargetApplicationTemplate> DefaultTarget;
};
struct Trait {
  model::NominalTypeKey Key;
  std::vector<model::TypeParameterId> TypeParameters;
  std::vector<TraitConstraint> Superclasses;
  std::vector<TraitMethod> Methods;
  Visibility Access;
};
struct TraitMethodBinding {
  std::string Method;
  TargetApplicationTemplate Target;
};
struct TraitInstance {
  model::NominalTypeKey Trait;
  std::vector<model::TypeParameterId> TypeParameters;
  std::vector<model::TypeId> Arguments;
  std::vector<TraitConstraint> Constraints;
  std::vector<TraitMethodBinding> Methods;
  Visibility Access;
};
struct GenericCapture {
  std::uint32_t Ordinal;
  std::string DebugName;
  model::TypeId Type;
  model::ParameterOwnership Ownership;
};
using GenericBinderEnvironment = model::GenericBinderEnvironment;
struct GenericFunction {
  std::string Name;
  model::TypeId Signature;
  GenericBinderEnvironment Binders;
  std::vector<TraitConstraint> Constraints;
  std::vector<GenericCapture> Captures;
  Visibility Access;
  Linkage SymbolLinkage;
  std::optional<std::vector<std::byte>> TypedIrFragment;
};
struct OperationDeclaration {
  model::EffectOperationKey Key;
  std::vector<model::TypeParameterId> TypeParameters;
  std::vector<model::EffectVariableDeclaration> EffectVariables;
  std::vector<TraitConstraint> Constraints;
  std::vector<model::FunctionParameter> Parameters;
  model::TypeId ResultType;
  model::ResultOwnership ResultContract;
  model::EffectRowId Effects;
  std::uint64_t DeclarationFingerprint;
  Visibility Access;
};
struct SourceFile {
  std::uint32_t Id;
  std::string ProducerPath;
  std::string ContentDigest;
  std::uint64_t ByteLength;
  std::vector<std::uint64_t> LineStarts;
};
struct InterfaceModule {
  model::ModuleIdentity Identity;
  InterfaceKind Kind;
  model::TypeTable Types;
  std::vector<NominalDeclaration> Nominals;
  std::vector<ResourceDeclaration> Resources;
  std::vector<Import> Imports;
  std::vector<Trait> Traits;
  std::vector<TraitInstance> Instances;
  std::vector<OperationDeclaration> Operations;
  std::vector<SourceFile> Sources;
  std::vector<Function> Functions;
  std::vector<GenericFunction> GenericFunctions;
};
```

`SourceManager.h` provides the single source-ID normalization primitive used
by both the interface writer and Typed IR fragment codec:

```cpp
namespace yona::support {
struct SourceNormalizationError {
  std::uint32_t OriginalId;
  std::string Message;
};
struct CanonicalSourceInput {
  std::uint32_t OriginalId;
  std::string ProducerPath;
  std::string ContentDigest;
  std::uint64_t ByteLength;
  std::vector<std::uint64_t> LineStarts;
};
struct SourceIdMapping {
  std::uint32_t OriginalId;
  std::uint32_t CanonicalId;
};
struct CanonicalSourceRecord {
  std::uint32_t CanonicalId;
  std::string ProducerPath;
  std::string ContentDigest;
  std::uint64_t ByteLength;
  std::vector<std::uint64_t> LineStarts;
};
struct CanonicalSourceSet {
  std::vector<CanonicalSourceRecord> Sources;
  std::vector<SourceIdMapping> Remap;
};
std::expected<CanonicalSourceSet, SourceNormalizationError>
canonicalizeSourceMetadata(std::span<const CanonicalSourceInput> Sources);
} // namespace yona::support
```

It normalizes producer paths, validates lowercase SHA-256 metadata, sorts by
normalized path, rejects duplicate paths or IDs and conflicting metadata,
assigns dense IDs `0..N-1`, validates strictly increasing line starts beginning
at zero and not exceeding byte length, and returns `Remap` sorted by original
ID. When source content is available, `SourceManager` first normalizes it to LF
and derives digest/length/line starts before calling this primitive. No writer
or fragment codec implements its own source ordering.

The first eight bytes are exactly `YONAI 2\n`; the remainder is one normative
binary envelope documented completely in `docs/interface-v2-format.md`:

```text
magic[8]
payload_size:u64le
section_count:u32le
section := tag:u32le byte_count:u64le payload[byte_count]
tags := identity(1), sources(2), types(3), effects(4), nominals(5),
        resources(6), imports(7), traits(8), instances(9), operations(10),
        functions(11), generics(12)
```

`payload_size` is exactly the byte count beginning at `section_count` and
ending after the final generic-section payload; it excludes the eight-byte
magic and the `payload_size` field itself. `section_count` is exactly 12 in
v2. Sections occur exactly once in tag order. Within payloads, integers/enums are
fixed-width little-endian, booleans are one byte `0`/`1`, strings and opaque
bytes are `u64le length + bytes`, vectors are `u64le count + elements`, and
options are `u8 present + value`. The format document gives the field order
for every schema record above, every invalid/reserved value, and worked hex
offsets for the golden fixture, but it must reproduce—not choose—the following
normative v2 constants and ordering:

```text
section payloads:
  identity  := InterfaceModule.Identity, InterfaceModule.Kind
  sources   := vector<SourceFile>
  types     := encoding_version:u32le, binder_count:u32le,
               binder_records[binder_count], type_count:u32le,
               canonical type_records[type_count]
  effects   := encoding_version:u32le, effect_count:u32le,
               canonical effect_records[effect_count]
  nominals  := vector<NominalDeclaration>
  resources := vector<ResourceDeclaration>
  imports   := vector<Import>
  traits    := vector<Trait>
  instances := vector<TraitInstance>
  operations:= vector<OperationDeclaration>
  functions := vector<Function>
  generics  := vector<GenericFunction>

interface enum u8 discriminants:
  InterfaceKind    Skeleton=0 Complete=1
  Visibility       Private=0 Module=1 Public=2
  LinkageKind       YonaDefinition=0 YonaImport=1 NativeExtern=2
  NativeAsyncKind   Synchronous=0 ThreadPool=1 DedicatedOutcome=2
  NativeBoundaryRoute CheckedDirectV2=0 CheckedOutcomeV2=1 StableExternal=2
  ResourceAbiKind   GenericResource=0 ChannelSender=1 ChannelReceiver=2
  ResourceShareability Linear=0 AlwaysShareable=1

model enum u8 discriminants:
  BinderOwnerKind    Nominal=0 Trait=1 Function=2 Instance=3 Operation=4
                     Resource=5
  PrimitiveType     Unit=0 Bool=1 Int=2 Float=3 Char=4 String=5 Symbol=6 Byte=7
  ParameterOwnership Trivial=0 Borrow=1 Consume=2
  ResultOwnership   Trivial=0 Owned=1
  EffectVariableKind Flexible=0 Opaque=1
  OwnedSlotStateKind HandlerRouter=0 TryBoundary=1 CleanupObligation=2
  ResourceAbiKind   GenericResource=0 ChannelSender=1 ChannelReceiver=2
  ResourceShareability Linear=0 AlwaysShareable=1
  CallingConvention DirectYona=0 ClosureEntry=1 Continuation=2
                    EffectOperation=3 AsyncAdapter=4 NativeExtern=5 ExportedC=6
  AbiOpaqueKind     ExceptionValue=0 EffectRequest=1 ControlOutcome=2
                    ExecutionContext=3 CallableInvocationEnvironment=4
                    ContinuationBoundaryContext=5

StructuralType variant u8 tags:
  Primitive=0 TypeParameter=1 Tuple=2 Sequence=3 Set=4 Dictionary=5
  Record=6 Nominal=7 Array=8 Channel=9 Promise=10 Cursor=11 CursorStep=12
  OutcomeTaskGroup=13 Resource=14 AbiOpaque=15 OwnedSlotState=16 Function=17
  Sum=18

schema record field order (each field uses its rule above):
  Linkage              := Kind, Target, NativeSymbol, AsyncKind, NativeRoute
  Function             := Name, Signature, Access, SymbolLinkage
  TraitConstraint      := TypeParameter, Trait, Arguments
  TargetApplicationTemplate := Target, TypeArguments, EffectArguments
  Constructor          := Name, Tag, Fields
  NominalDeclaration   := Key, TypeParameters, Constructors, Access, Opaque
  ResourceDeclaration  := Key, TypeParameters, AbiKind, Shareability, TryRetain,
                           Release, Access
  ImportedSymbol       := SourceName, LocalName, Reexported
  Import               := Module, Alias, Symbols, Wildcard
  TraitMethod           := Name, Signature, DefaultTarget
  Trait                 := Key, TypeParameters, Superclasses, Methods, Access
  TraitMethodBinding    := Method, Target
  TraitInstance         := Trait, TypeParameters, Arguments, Constraints,
                           Methods, Access
  GenericCapture        := Ordinal, DebugName, Type, Ownership
  GenericBinderEnvironment := InheritedTypeParameters,
                           InheritedEffectVariables, DeclaredTypeParameters,
                           DeclaredEffectVariables
  GenericFunction       := Name, Signature, Binders, Constraints, Captures,
                           Access, SymbolLinkage, TypedIrFragment
  OperationDeclaration := Key, TypeParameters, EffectVariables,
                           Constraints, Parameters, ResultType,
                           ResultContract, Effects, DeclarationFingerprint,
                           Access
  SourceFile            := Id, ProducerPath, ContentDigest, ByteLength,
                           LineStarts
```

`CallableInvocationEnvironment=4` and
`ContinuationBoundaryContext=5` are reserved internal model discriminants so
canonical in-memory tables and diagnostics remain total. An interface writer
rejects either in every outer declaration and TIRF type graph, and a reader
rejects an encoded occurrence; closure/effect conversion alone creates the
hidden types after interface/specialization boundaries have closed. They
therefore never imply a public ABI descriptor despite stable enum values.
In-memory canonical-enum tests round-trip both exact values; v2 negative tests
reject either internal occurrence and every unknown value `>= 6` before
constructing a structural record.
`OwnedSlotStateKind` round-trips all three exact values; the reader rejects
every value `>= 3` before decoding fields or a DropIdentity.
`NativeBoundaryRoute` likewise round-trips all three exact values, rejects
every value `>= 3` before publishing Linkage, and has an explicit byte offset
in the golden Complete fixture plus malformed/truncated route cases.

`TypeParameters` fields in `NominalDeclaration`, `ResourceDeclaration`,
`Trait`, `TraitInstance`, `GenericFunction.Binders`, and `OperationDeclaration`, plus
`FunctionType.TypeParameters`, encode only their
declaration-order `u32le` count; their raw IDs are represented solely by Task
2's sorted binder table and `TypeParameterRef`s.
`FunctionType.EffectVariables`, `GenericFunction.Binders`, and
`OperationDeclaration.EffectVariables` likewise encode only the
declaration-order vector of `EffectVariableKind:u8`; raw effect-variable IDs
appear only through `EffectVariableRef`s. `TraitConstraint.TypeParameter`
encodes one such ref. Type/effect IDs are `u32le`; raw `TypeParameterId` and
open-row IDs are never serialized. IDs inside a generic Typed IR fragment are
the canonical printer's decimal UTF-8 tokens, not binary integers.
Constructor/source IDs are `u32le`; tags shown above are `u8`;
`DeclarationFingerprint`, byte lengths, line starts, general vector counts, and
framed lengths are `u64le`. Every nested model record uses Task 2's exact
`CanonicalEncoding` v2 field order and discriminants. Unknown enum/variant
values, nonzero reserved bytes, invalid option tags, and IDs/binder refs
outside the canonical tables are errors. `GenericFunction.TypedIrFragment`
is therefore provably the final field of its record. Its explicit option tag
is absent for every Skeleton generic and present with length-framed canonical
UTF-8 `YONA-TIRF 1` data for every Complete generic; the IR payload is
canonical Typed IR text.
There is no legacy generic-source record or source-body delimiter; NUL/newline string
data is represented by the printer's canonical escapes.

`GenericFunction.Captures` is declaration ordered and its ordinals must be
dense `0..N-1`; each entry records only the external capture contract, never a
producer-local `BindingId` or value. Complete interfaces reject nonempty
captures because an exported/imported generic has no lexical environment at
its consumer. The same schema value is also used internally for a private
same-module generic, where Task 15 pairs it with producer binding IDs and the
fragment root's alpha-renamed capture bindings. A Module/Public generic with
captures is rejected during interface construction instead of being emitted
with an unusable ABI.

`GenericFunction.Binders` preserves the effective environment explicitly.
For an exported top-level generic, inherited lists are empty. Internal local
records and decoded fragments may have inherited binders in
outermost-ancestor declaration order followed by the function's own declared
binders. The signature and record must reference exactly those IDs, including
phantoms; a local declaration identity or nonempty inherited list is forbidden
in the outer public v2 slice.

`GenericFunction.Constraints` must be the duplicate-free canonical/remapped
exact vector from its referenced FunctionType: identical trait keys,
type-parameter refs, and ordered argument graphs. Every constraint parameter
belongs to the effective type environment, including inherited positions. The
decoded lifted root must match it byte-for-byte, and specialization discharges
only this validated vector. Malformed-v2 and outer/fragment constraint
mismatch tests reject before mutation.

`ResourceDeclaration` is disjoint from `NominalDeclaration`: it has no
constructors, tags, pattern form, or public construction operation. Its type
parameters are declaration ordered and own the corresponding structural
`ResourceType`; `AbiKind` has exactly the three discriminants above.
ChannelSender/ChannelReceiver are legal only for the two reserved
`Std\Channel` one-parameter keys, select ABI kind CHANNEL, and require
AlwaysShareable plus the exact common endpoint callbacks. GenericResource
selects ABI kind RESOURCE. `Shareability` has only the two discriminants above. Linear
requires absent TryRetain; AlwaysShareable requires a present exact TryRetain.
Release is mandatory. Both fields are explicit C symbol strings—never derived
from a semantic name—and take the descriptor callback signatures fixed in
Task 7; they are included in the public ABI slice even when no source function
names them. Reader/writer sort
resources by qualified key, reject duplicate Nominal/Resource keys, local
empty/legacy callback symbols, unknown AbiKind, wrong policy/callback combinations, and a ResourceType
without an equal declaration. Skeletons carry the same resource declarations;
ordinary imports recreate the exact opaque ResourceType and descriptor policy.
Resource declarations participate in the canonical binder table with
`BinderOwnerKind::Resource`; writer and reader validate the owner key, exact
type-parameter count/order, and zero effect variables. Round-trip tests cover
phantom generic resource binders, wrong owner kind, wrong arity, and a resource
key that deliberately matches a nominal key before the duplicate-key
validator rejects the module. The descriptor matrix covers both Linear and
AlwaysShareable resources and rejects a callback/flag combination copied from
the other policy.
`ProjectedInterfaceSeed.Resources` is the only semantic-to-interface source
of these rows. Compiler-stdlib checking supplies one
`ResourceDeclarationProjection` per admitted declaration and creates a
`ProjectedDeclarationScope` with `BinderOwnerKind::Resource`, projects the
declaration-ordered type parameters and explicit ABI/shareability/callback
facts into `ProjectedResourceDeclaration`, and stages the row in the same
batch as its `ResourceType`. Freeze/remap/rollback traverses this vector and
publishes neither the resource row nor its binder on any failure. It rejects
a missing/foreign scope, key mismatch, different parameter order/count
between the projection and scope, any effect binder, or policy/callback
violation before commit. The v2
builder maps the model enums one-to-one to the schema enums; it never
reconstructs policy from a module or source name.

`Linkage.AsyncKind` and `Linkage.NativeRoute` are required exactly for
`NativeExtern`, even for the Synchronous value, and forbidden otherwise.
Reader/writer and Typed IR import map them one-to-one with
`FunctionLinkage::{AsyncKind,NativeRoute}`; unknown values, a native linkage
without either field, or an invalid pair are hard format errors. The same
pairing matrix applies: StableExternal/Synchronous,
CheckedOutcomeV2/DedicatedOutcome, or CheckedDirectV2 with
Synchronous/ThreadPool. Cross-module tests cover every legal pair, reject all
other combinations and the removed raw-native-pointer value, and prove Tasks
8/14 select distinct lowering paths.

Before serialization, validate UTF-8 identifiers, normalize producer paths to
relative forward-slash form with no `.`/`..`/absolute prefix, normalize source
line endings to LF for byte lengths/line starts, and record lowercase SHA-256
of those normalized bytes. Construct Task 2 binder declarations in source/
in-memory order; a generic function's binder symbol is exactly
`Module.Identity + GenericFunction.Name` and must equal its signature's
`SourceIdentity` Symbol variant. A local generic instead uses the model-level
Local variant with its canonical lexical declaration path; it is legal only in
an internal fragment, never as an exported v2 name. An operation binder identity is its fully qualified
`EffectOperationKey`, and its declared type/effect variables must own every
open reference in that signature. Call `encodeInterfaceTables` once, then serialize trait
instances through its `InstanceRemap`, schema constraints through its
`TypeParameterRemap`, and type/effect references through their returned
remaps. This one call owns alpha-normalization, instance sorting, binder
sorting, and expanded-key type/effect ordering; the writer duplicates none of
those rules. Serialize `Sources` from
`support::canonicalizeSourceMetadata().Sources` and rewrite every top-level
source reference with its remap. Sort all other identity-keyed
declarations/imports/traits/operations/functions independently of insertion
order. Sort every `TraitConstraint`/`TypeConstraint` vector by Task 2's
expanded key and reject duplicates; declaration-order type-parameter lists
remain ordered. Within records, sort imported symbols by
`(SourceName, LocalName)`, trait methods and instance method bindings by method
name, and constructors by numeric tag; reject duplicate import pairs/method
names/constructor names or tags, and require constructor tags to be dense
`0..N-1`. Constructor field order remains positional and is preserved. IDs on
disk are these canonical indices, never in-memory
insertion IDs. Readers reject unknown/duplicate/
out-of-order sections, noncanonical order or indices, invalid UTF-8/paths,
size/count overflow, trailing bytes, and any golden reserialization mismatch.
The writer calls `encodeInterfaceTables`; operation records reuse the shared
record codec with its returned remaps. The reader rebuilds each operation's
canonical binder declaration, calls `encodeOperationDeclarationGraph`, and
recomputes/validates its stored declaration fingerprint for open and closed
signatures alike. V2 never stores a runtime fingerprint: specialization plus
Task 13 operation instantiation produces that closed-domain value. The reader
never forces an open declaration through `encodeClosedDescriptorGraph` or
compares a module-table byte envelope directly with a descriptor graph.
All records above remain concrete `Schema.h` value types; the interface
component must not forward-declare or embed a Typed IR record.

The exact format APIs are pure/reentrant and return owning values:

```cpp
inline constexpr std::uint64_t MaxInterfaceBytes = 64ull * 1024 * 1024;
inline constexpr std::uint64_t MaxGenericFragmentBytes = 16ull * 1024 * 1024;
struct FormatError {
  std::string SourceName;
  std::uint64_t ByteOffset;
  std::string Message;
};
using ParseInterfaceResult =
    std::expected<InterfaceModule, std::vector<FormatError>>;
using SerializeInterfaceResult =
    std::expected<std::vector<std::byte>, std::vector<FormatError>>;

ParseInterfaceResult parseModule(std::span<const std::byte> Bytes,
                                 std::string SourceName);
ParseInterfaceResult readModule(const std::filesystem::path &Path);
SerializeInterfaceResult serializeModule(const InterfaceModule &Module);
std::expected<void, std::vector<FormatError>>
writeModuleAtomically(const std::filesystem::path &Path,
                      const InterfaceModule &Module);

std::expected<std::string, std::vector<FormatError>>
renderDocumentationJson(const InterfaceModule &Module);
```

`renderDocumentationJson` is the sole machine-readable API-signature
projection. It accepts only Complete v2, merges `Functions` and
`GenericFunctions`, and independently merges `Nominals`, `Resources`, and
`Traits`; it keeps exactly `Visibility::Public`. Structural aliases are
expanded before v2 construction and therefore are neither ABI declarations nor
documentation type records. The renderer rejects duplicate names within or
across the function/type documentation namespaces, any function root that is
not a structural FunctionType, and every unresolved binder. It sorts functions
and types independently by UTF-8 name and formats every complete
source-visible signature/declaration through the model's one canonical type
formatter. A type record has exactly the keys `name`, `kind`,
`type_parameters`, `declaration`, and `opaque`: `kind` is `nominal`,
`resource`, or `trait`; `type_parameters` preserves declaration order;
`declaration` is the canonical source spelling including visible constructors
or trait method signatures; `opaque` is true for a Resource and for an opaque
Nominal, false for a visible Nominal or Trait. It never exposes IDs, linkage
spellings, resource callback symbols/policy, TIRF, or binary fields. The output
is one newline-terminated canonical JSON object with this exact top-level key
order:

```json
{"schema":"yona.interface_docs","version":1,"interface_version":2,"module":"Std\\Stream","types":[{"name":"Stream","kind":"nominal","type_parameters":["a"],"declaration":"opaque type Stream a","opaque":true}],"functions":[{"name":"allMatch","signature":"(a -> Bool) -> Stream a -> Bool"}]}
```

The JSON string is an interchange artifact, not semantic identity. Tests
cover public closed/generic/nested-arrow/open-effect signatures, visible and
opaque Nominals, parameterized File/Channel Resources, Traits, private and
Module filtering, independent UTF-8 ordering/escaping, and deterministic
rejection of a Skeleton, duplicate same-kind or cross-kind export, malformed
function/type root, or unresolved structural binder. Task 17 exposes this
existing renderer as the mutually exclusive CLI
mode `yonac --emit-doc-signatures <complete-v2.yonai>` before source loading;
JSON is the only stdout and diagnostics use stderr/nonzero.

Readers reject total-size/fragment-size limits, fixed-width integer/count/
length overflow, truncated
records, duplicate IDs, and trailing bytes before allocation. The catalog
searches exact canonical relative paths under module roots in request order,
rejects two different files claiming one identity, and owns parsed modules;
no returned record borrows the input byte buffer. Concurrent reads share no
mutable parser state, while catalog mutation requires its documented lock.
Skeleton interfaces contain declarations/linkage only, are accepted solely by
the bootstrap compilation mode in Task 16, and are rejected by ordinary
catalog loads and package consumers.
Every Skeleton generic has an absent `TypedIrFragment`; every Complete generic
has a present, canonical TIRF fragment. The reader rejects either opposite
combination before catalog insertion. Skeletons may contain only explicitly
typed declarations—never inferred bodies, specialization artifacts, or
runtime descriptors.

- [ ] **Step 4: Encode/decode and verify ID/source-self-contained Typed IR fragments**

Add:

```cpp
enum class InterfaceCodecErrorCode : std::uint8_t {
  InvalidInputPhase = 0, InvalidRootFunction = 1, MissingDependency = 2,
  InvalidSourceMetadata = 3, SizeLimit = 4, MalformedEnvelope = 5,
  InvalidUtf8 = 6, TypedIrParse = 7, TypedIrVerification = 8,
  NonCanonicalEncoding = 9
};
struct InterfaceCodecSourceRange {
  std::string ProducerPath;
  std::uint64_t ByteOffset;
  std::uint64_t ByteLength;
  std::uint64_t Line;
  std::uint64_t Column;
  friend bool operator==(const InterfaceCodecSourceRange &,
                         const InterfaceCodecSourceRange &) = default;
};
struct InterfaceCodecError {
  InterfaceCodecErrorCode Code;
  std::optional<std::uint64_t> FragmentByteOffset;
  std::optional<InterfaceCodecSourceRange> ProducerRange;
  std::string Message;
  friend bool operator==(const InterfaceCodecError &,
                         const InterfaceCodecError &) = default;
};
using FragmentResult =
    std::expected<std::vector<std::byte>, InterfaceCodecError>;
struct FragmentGenericDefinition {
  model::FunctionDeclarationIdentity Declaration;
  FunctionId Function;
  model::GenericBinderEnvironment Binders;
  std::vector<interface::v2::TraitConstraint> Constraints;
  std::vector<interface::v2::GenericCapture> Captures;
};
FragmentResult encodeGenericFragment(
    const Module &, FunctionId,
    std::span<const FragmentGenericDefinition> LocalGenericDefinitionIndex,
    std::span<const interface::v2::GenericFunction>
        ImportedGenericDeclarations);

struct DecodedGenericFragment {
  Module Ir;
  FunctionId RootFunction;
  std::vector<FragmentGenericDefinition> GenericDefinitions;
};
using DecodeFragmentResult =
    std::expected<DecodedGenericFragment,
                  std::vector<InterfaceCodecError>>;
DecodeFragmentResult
decodeGenericFragment(std::span<const std::byte> Bytes);

enum class SpecializationErrorCode : std::uint8_t {
  InvalidDestinationPhase = 0, InvalidSpecializationKey = 1,
  FragmentDecodeFailed = 2, FragmentContractMismatch = 3,
  MissingDependency = 4, RemapFailed = 5, VerificationFailed = 6,
  RecursiveDefinitionConflict = 7, TransactionFailed = 8,
  InvalidDestinationCache = 9, InvalidAcceleratorCandidate = 10
};
struct SpecializationError {
  SpecializationErrorCode Code;
  model::ModuleIdentity Module;
  std::string GenericName;
  std::optional<InterfaceCodecSourceRange> ProducerRange;
  std::string Message;
  std::vector<InterfaceCodecError> Causes;
  friend bool operator==(const SpecializationError &,
                         const SpecializationError &) = default;
};
```

These records own all strings/location data. The encoder returns its first
deterministic error; the decoder returns parse/verification errors in stable
range/code/message order. A `Failed` specialization-cache tombstone stores the
complete copyable `SpecializationError`, and every waiter/retry receives an
equality-identical value after temporary source managers are destroyed.

The fragment bytes have this exact canonical UTF-8 grammar; `uint` is decimal
with no sign or leading zero except the single digit `0`:

```text
"YONA-TIRF 1\n"
"sources " source_count:uint "\n"
source[source_count] :=
  "source " canonical_id:uint " " path_byte_count:uint " "
             lowercase_sha256[64] " " byte_length:uint " "
             line_count:uint "\n"
  normalized_path[path_byte_count] "\n"
  ("line " line_start:uint "\n")[line_count]
"root " root_function_id:uint "\n"
"ir " ir_byte_count:uint "\n"
canonical_typed_ir_utf8[ir_byte_count]
```

Source IDs are dense and records are path-sorted as above; normalized paths
are UTF-8 and contain no control characters. The IR payload consumes the
remainder exactly—no trailing byte is permitted. The canonical Typed IR module
inside that payload owns a `FragmentGenericDefinitions` arena and the core
printer/parser emits/parses its declaration identity, remapped FunctionId,
binder environment, constraints, and capture schema in canonical declaration-
identity order. This arena is the sole encoded source of the generic index;
TIRF does not carry an unframed side table. A codec-level pre-parser
reads this envelope, calls `SourceManager::addVirtualSource` for each record,
then invokes existing `typed_ir::parseModule(IrText, Sources)`. The core
Printer/Parser grammar remains source-metadata-free; only
`InterfaceCodec.cpp` owns this envelope.

Before printing, `encodeGenericFragment` validates a duplicate-free canonical
declaration-identity-to-open-`FunctionId` index built from every
`LocalGenericSeed`/function `SourceIdentity`, computes the root function's
exact transitive ordinary-and-generic function-reference closure, and rebuilds
it in a fresh Module; it
never prints a storage slice from the input. The closure visitor is exhaustive
over every `FunctionId`-bearing instruction, terminator, value, linkage,
pattern/control/cleanup/generator arena, callable/resume/router descriptor, and
first-class `MakeFunctionInst`, not merely direct call edges. It also visits
every `GenericCallInst` and `MakeGenericFunctionInst`. A key in the local index
recursively includes its open body and emits exactly one remapped
`FragmentGenericDefinition` entry in the rebuilt module arena. A key in
`ImportedGenericDeclarations` emits one declaration-only generic dependency
with exact SymbolIdentity, signature, effective binders, constraints, and
linkage; its fragment remains absent and specialization later obtains the body
from that producer's Complete catalog record. A key in neither set, a
duplicate/conflicting key, or mismatch between local index, seed, and function
source identity is an encoding error. Every reachable local open generic
definition and same-module Private non-generic helper is included with its body. A
same-module Module/Public non-generic helper is encoded declaration-only under
its producer linkable symbol, exactly like an imported or native callee; the
producer object remains its sole definition. An unresolved declaration is a
hard error. This body-versus-declaration policy applies recursively and is
tested with private, module-visible, public, imported, native, and mutually
recursive helpers. Apply Task 2's binder/type/effect remaps, the canonical source remap,
and canonical dense remaps for every remaining strong-ID domain: imports by module identity,
nominals by fully qualified key (constructors by tag), operations by fully
expanded declaration key/signature, closed operation instances by declaration
key plus complete closed bytes, globals by symbol, and functions by
`(definition kind, canonical symbol/native symbol/name, expanded signature,
normalized source range)`. Reject duplicate function keys. Within each
function, order reachable blocks by deterministic reverse postorder from Entry,
then unreachable-but-valid blocks by normalized range and structural opcode
key; assign parameters, block arguments, and instruction results in that
order. Rebuild any referenced match-plan/pattern/decision/control/generator/cleanup/
callable arenas in first-use preorder with structural-key tie breaking and
reject unreachable arena records. Canonicalize `semantics::BindingId` as its
own module-wide ID domain. Seed the map with the root function's
`GenericCaptures` in declared ordinal order, then walk the already
canonicalized function/arena records in printed field order and assign
remaining dense IDs at first definition or reference. Rewrite every
`GenericCaptureBinding`, `LexicalRefInst`, `LexicalBindingValue`, pattern
binding/as-pattern, handler resume binding, and future exhaustive visitor
occurrence. Every free lexical reference in the root and included dependency
closure must resolve through the root's declared capture list; a helper may
not smuggle in a hidden ambient capture. This alpha-renaming deliberately
erases producer-local binding numbers while preserving equality links and
rejects dangling, duplicate, or undeclared capture bindings. Rewrite every
operand, successor, range, declaration, root ID, and dependency through those
maps.

The internal result uses the already declared `DecodedGenericFragment` record.
Verify its `Ir`, including the fragment-generic arena, print it, parse it, and
require byte-identical reprinting before framing. On decode, copy the verified
arena into `GenericDefinitions` and return it with the owned Module and
validated root ID; the copy must compare equal to the parsed arena and is not
reconstructed from name/body ancestry.
`SpecializationContext::specialize(Generic, Key)` immediately compares that
root's complete structural signature, ownership/effects, generic binders,
capture ordinal/type/ownership schema, and linkage identity with the enclosing v2 `GenericFunction`
record before opening its mutation transaction. Tests build alpha-equivalent modules with
randomized insertion order and every ID family, including `BindingId`,
remapped and require identical fragment bytes. Dedicated fixtures make a
generic root call same-module Module/Public helpers and require
declaration-only records with exact producer symbols and no helper bodies in
the fragment; the producer object must provide exactly one matching
definition. A missing/duplicate producer or mismatch between outer
signature/captures and root is a hard error. Random producer capture IDs must
produce identical bytes, while a hidden dependency capture is rejected.
Add a generic that returns a private first-class helper without calling it and
require the helper body and all of its function-bearing arena dependencies in
the fragment.

Encode an ID/type/source-self-contained module fragment containing its
type/nominal/import/transitive dependency records plus normalized producer filenames, content
digests, byte lengths, line-start tables, and source-range mappings. Encoding
collects exactly the referenced source metadata, calls
`support::canonicalizeSourceMetadata`, clones the fragment, and rewrites
`Module.Source`, every instruction/block/terminator `SourceRange`, and every
compiler-generated origin range through the returned dense source-ID remap.
It then runs the canonical Typed IR printer and stores its UTF-8 bytes verbatim
in the length frame; it rejects invalid UTF-8/NUL and proves parse-print
stability before returning. Decode validates UTF-8 and dense path-sorted source
IDs, invokes the canonical Typed IR text parser (never the Yona source parser),
reconstructs an owned virtual source map from that metadata, and runs the
phase verifier before returning. It never attaches imported ranges to the
importing source manager. Invalid IDs, phase, dependencies, source ranges,
noncanonical reserialization, or nominal references are hard errors.
“Self-contained” deliberately excludes imported generic bodies: their
declaration-only Symbol records are complete enough to verify/remap the TIRF,
but specializing such a call requires the matching Complete producer record.
Skeleton catalogs are accepted only while encoding Complete SCC members and
can supply these declarations; ordinary specialization rejects an absent
producer fragment.
Add `SourceManager::addVirtualSource(Path, ByteLength, LineStarts, Digest)`;
virtual sources support range validation and path/line/column formatting but
explicitly report that excerpts are unavailable.

`encodeGenericFragment` accepts only a fully verified `ModulePhase::Canonical`
module/function and emits a fragment whose printed phase is Canonical.
`decodeGenericFragment` rejects every other encoded phase and returns only a
verified Canonical module. `SpecializationContext::specialize` likewise
requires a Canonical destination, appends Canonical functions transactionally,
and must complete before `runTypedIrPipeline` advances the destination. Add
wrong-source-phase, forged-fragment-phase, and progressed-destination tests.

Decoding also builds a validated fragment-local generic-definition index for
every embedded open function, keyed by `FunctionDeclarationIdentity` and
carrying root `FunctionId`, effective binder environment, constraints, and
capture schema. `GenericDefinitions` contains every open embedded Definition
exactly once in canonical-key order; each Function signature's SourceIdentity
equals the record declaration and its `GenericBinders` equals the record
partition. Declaration-only imported generic functions are validated against
their dependency record but do not enter `GenericDefinitions`; other
Imported/Native bodies, missing local open-indexed functions, and duplicate
records are errors. Nested lookup order is the current transaction/fragment
index, local module definitions, then the catalog; a producer-private generic
helper is never required in the consumer catalog. Duplicate keys from an outer
record, embedded fragment, or catalog must have byte-identical canonical
contracts/bodies or fail `RecursiveDefinitionConflict`. Mutually recursive
embedded generics reserve one provisional SCC before cloning. Tests cover a
private nested generic helper and nested mutual recursion after import.

- [ ] **Step 5: Extract local generics and resolve every generic use**

Add the shared local/imported vocabulary:

```cpp
struct GenericFunctionKey {
  model::FunctionDeclarationIdentity Declaration;
  friend bool operator==(const GenericFunctionKey &,
                         const GenericFunctionKey &) = default;
};
using EffectiveGenericBinderEnvironment = model::GenericBinderEnvironment;
struct GenericInstantiation {
  // Ordered inherited arguments followed by this declaration's arguments.
  std::vector<model::TypeId> TypeArguments;
  std::vector<model::EffectRowId> EffectArguments;
  std::vector<model::ParameterOwnership> ParameterOwnerships;
  model::ResultOwnership ResultContract;
  model::CallingConvention Convention;
};
struct GenericCallInst {
  GenericFunctionKey Callee;
  GenericInstantiation Instantiation;
  std::vector<LexicalBindingValue> AvailableBindings;
  std::vector<ValueId> Arguments;
};
struct MakeGenericFunctionInst {
  GenericFunctionKey Callee;
  GenericInstantiation Instantiation;
  std::vector<LexicalBindingValue> AvailableBindings;
};
namespace yona::semantics {
enum class TraitResolutionErrorCode : std::uint8_t {
  InvalidArity, InvalidEvidence, MissingInstance, OverlappingInstances,
  UnsatisfiedConstraint, InvalidTargetTemplate, LocalTargetEscapes
};
struct TraitResolutionError {
  TraitResolutionErrorCode Code;
  SourceRange Range;
  std::string Path;
  std::string Message;
};
class TraitResolver {
public:
  virtual ~TraitResolver() = default;
  virtual const model::TypeTable &types() const noexcept = 0;
  virtual std::expected<model::TraitTargetApplication, TraitResolutionError>
  resolve(const model::TraitResolutionRequest &, SourceRange) const = 0;
};
class TypedImportSource {
public:
  virtual ~TypedImportSource() = default;
  virtual const interface::v2::Function *
  findFunction(const model::SymbolIdentity &) const = 0;
  virtual const interface::v2::GenericFunction *
  findGeneric(const model::SymbolIdentity &) const = 0;
  virtual const interface::v2::OperationDeclaration *
  findOperation(const model::EffectOperationKey &) const = 0;
  virtual const interface::v2::NominalDeclaration *
  findNominal(const model::NominalTypeKey &) const = 0;
  virtual const interface::v2::ResourceDeclaration *
  findResource(const model::NominalTypeKey &) const = 0;
  virtual const interface::v2::Trait *
  findTrait(const model::NominalTypeKey &) const = 0;
  virtual std::span<const interface::v2::TraitInstance>
  findInstances(const model::NominalTypeKey &) const = 0;
  virtual std::optional<model::SymbolIdentity>
  resolveImportedSymbol(const model::ModuleIdentity &From,
                        std::string_view VisibleName) const = 0;
  virtual const TraitResolver &traits() const noexcept = 0;
};
enum class CatalogSessionMode : std::uint8_t {
  CompleteOnly = 0, BootstrapSkeletonDeclarations = 1
};
enum class CatalogSessionErrorCode : std::uint8_t {
  MalformedInterface = 0, ForeignTypeDomain = 1, RemapFailed = 2,
  AllocationFailed = 3, InvalidLocalOverlay = 4
};
struct CatalogSessionError {
  CatalogSessionErrorCode Code;
  std::string Path;
  std::string Message;
};
struct SemanticInterfaceSeed {
  model::ModuleIdentity Identity;
  ProjectedInterfaceSeed Declarations;
  std::vector<interface::v2::Import> Imports;
  std::vector<interface::v2::Function> Functions;
  struct GenericDeclarationSeed {
    model::FunctionDeclarationIdentity Declaration;
    std::string Name;
    model::TypeId Signature;
    model::GenericBinderEnvironment Binders;
    std::vector<model::TypeConstraint> Constraints;
    interface::v2::Visibility Access;
    interface::v2::Linkage SymbolLinkage;
  };
  std::vector<GenericDeclarationSeed> Generics;
  std::vector<interface::v2::SourceFile> Sources;
};
class TypedInterfaceCatalog {
public:
  virtual ~TypedInterfaceCatalog() = default;
  virtual std::expected<std::unique_ptr<TypedImportSource>,
                        std::vector<CatalogSessionError>> createSession(
      model::TypeTable &Destination,
      const SemanticInterfaceSeed &LocalOverlay,
      CatalogSessionMode Mode = CatalogSessionMode::CompleteOnly) const = 0;
};
} // namespace yona::semantics
struct TraitMethodUse {
  model::TraitResolutionRequest Request;
  SourceRange Range;
};
struct TraitCallInst {
  TraitMethodUse Callee;
  std::vector<ValueId> Arguments;
};
struct MakeTraitMethodInst { TraitMethodUse Callee; };
struct GenericDefinition {
  GenericFunctionKey Key;
  EffectiveGenericBinderEnvironment Binders;
  interface::v2::GenericFunction Record;
  std::vector<semantics::BindingId> ProducerCaptureBindings;
};
struct LocalGenericSeed {
  FunctionId Function;
  GenericFunctionKey Key;
  EffectiveGenericBinderEnvironment Binders;
};
class PreparedInterfaceRoots;
using PreparedInterfaceRootsRef =
    std::shared_ptr<const PreparedInterfaceRoots>;
class PreparedSkeletonInterfaceRoots;
using PreparedSkeletonInterfaceRootsRef =
    std::shared_ptr<const PreparedSkeletonInterfaceRoots>;
struct GenericPreparationInput {
  Module Canonical;
  semantics::SemanticInterfaceSeed InterfaceSeed;
  std::vector<LocalGenericSeed> LocalGenerics;
  PreparedInterfaceRootsRef InterfaceRoots; // null only before root preparation
};
struct GenericPreparation {
  Module RuntimeModule;
  std::vector<GenericDefinition> LocalDefinitions;
  semantics::SemanticInterfaceSeed InterfaceSeed;
  PreparedInterfaceRootsRef InterfaceRoots;
};
struct GenericInterfaceExtraction {
  std::vector<GenericDefinition> LocalDefinitions;
  semantics::SemanticInterfaceSeed InterfaceSeed;
  PreparedInterfaceRootsRef InterfaceRoots;
};
using GenericInterfaceExtractionResult =
    std::expected<GenericInterfaceExtraction,
                  std::vector<SpecializationError>>;
GenericInterfaceExtractionResult extractGenericDefinitions(
    const GenericPreparationInput &Input);
using GenericPreparationResult =
    std::expected<GenericPreparation, std::vector<SpecializationError>>;
GenericPreparationResult prepareGenericModule(
    GenericPreparationInput Input,
    const semantics::TypedInterfaceCatalog &Catalog,
    SpecializationCache &Cache);
```

`TypedImportSource` here means one immutable, destination-bound model import
session, not the process/catalog store itself. `TypedInterfaceCatalog` creates
it from a destination `model::TypeTable` plus a read-only local
`semantics::SemanticInterfaceSeed` overlay. The session remaps every returned structural
record into that table, and `traits().types()` is exactly the same table;
resolver input IDs from any other table/domain are `InvalidEvidence`. One
session supplies functions, generics, operations, nominals/constructors,
resources, traits/defaults/superclasses, instances, imports, and reexports for
the whole preparation transaction. `findResource` returns the destination-
remapped declaration including exact `AbiKind`, shareability, callback
symbols, and generic argument arity; it never falls back to `findNominal`.

`createSession` is fallible and runs every interface decode/remap plus local-
overlay validation inside one destination `TypeTable` append transaction. It
publishes the immutable session only after all records and indexes validate;
malformed v2, foreign IDs, remap/allocation failure, or a bad overlay returns
the complete stable `CatalogSessionError` set and rolls the destination back
byte-for-byte. Callers propagate that expected value—there is no exception or
partially populated session path. Forced corrupt-interface, foreign-domain,
allocation, and late-remap failures assert unchanged destination and seed.

A session is an immutable snapshot of the supplied local overlay; it never
observes later vector mutation. The normal frontend therefore creates and
destroys sessions at each seed-commit boundary: session A is used only by
`runDerivations`; after derivation commits its generated functions/generics/
instances, session B is rebuilt and used only by `prepareInterfaceAbiRoots`.
`prepareGenericModule` accepts the catalog—not a caller-created session—and
creates its own destination-bound session only after the final
`GenericPreparation::RuntimeModule` and immutable overlay exist. The session
is destroyed before the preparation result moves/returns and is never stored.
Read-only `buildCompleteInterfaceModule` and `buildSkeletonInterfaceModule`
accept no catalog and create no session; each only remaps and validates its
already-prepared ABI-root closure. Session B
uses immutable imported lookups while `prepareInterfaceAbiRoots` reads and
promotes local definitions directly from mutable `Input`; if a future
promotion needs a new local-overlay lookup, that stage must commit the seed,
destroy B, and create a fresh session before continuing.
`BootstrapSkeletonDeclarations` is legal only for Task 16 Complete-against-
Skeleton interface emission; ordinary compilation/specialization requests
`CompleteOnly`. Tests derive a local instance, then specialize a local generic
trait call that resolves that just-derived instance through the internal
preparation session, and prove a deliberately retained session A cannot see
it.
Catalog/importer tests round-trip a cross-module `Sender a` and `Receiver a`,
prove their CHANNEL roles remain distinct after remapping, and reject an
unknown/corrupt `ResourceAbiKind`, wrong resource binder arity, or a catalog
implementation that attempts to answer either endpoint through the nominal
lookup.

Task 15 changes module `lowerModule` to return `GenericPreparationInput`. It
copies `ProjectedSemanticBatch::InterfaceSeed`, exact import/export/source and
function metadata, and maps every `ProjectedFunction::Scope` to its
final `FunctionId` and declaration identity. `LocalGenerics` preserves every
effective inherited/declared binder, including phantoms, so preparation never
rediscovers ancestry from reachable `FunctionType` occurrences. Expression
lowering uses an empty interface/local-generic seed around its Canonical
module. A regression extracts a child open only through an ancestor phantom or
effect-row binder even when neither occurs in the child's own signature.

Before returning, `lowerModule` maps every projected resource row to the
interface-independent `model::ResourceDeclaration` and inserts it through
`Builder::addResource`. Imported resource policies reachable through
signatures are copied by the destination-bound import session during generic/
module closure construction. `Input.Canonical.Resources` and
`Input.InterfaceSeed.Declarations.Resources` must be exact visibility-erased
counterparts for local rows. Any missing, extra, or conflicting row aborts the
same lowering transaction.

Generic openness means any reachable free type parameter, record-row
parameter, or Flexible effect variable, not merely a nonempty declaration list
on that function's `FunctionType`. For each local root, lowering computes its
`EffectiveGenericBinderEnvironment` from Task 2's projected declaration forest
in outermost-ancestor order followed by the root's own declaration order.
Fragment construction lifts every inherited binder to a fresh root-owned
fragment binder and exhaustively remaps the root's transitive lexical
dependency closure. The outer definition and every generic use carry
arguments for this effective list: inherited arguments are the caller's
current structural binder references and own arguments come from the semantic
instantiation fact. Enclosing specialization substitutes inherited positions
before deriving a nested child key. Every function open only through an
ancestor is omitted from `RuntimeModule` and can materialize only through its
enclosing fragment.

AST lowering identifies every such open function and emits
`GenericCallInst`/`MakeGenericFunctionInst` keyed by its semantic
`FunctionDeclarationIdentity`; it never emits DirectCall/MakeFunction targeting an open
definition. Add both generic instructions to exhaustive visitors and the
canonical parser/printer/verifier. `AvailableBindings` contains exactly one
entry per declared capture, in capture ordinal order. For a local definition,
`GenericDefinition::ProducerCaptureBindings` has the same cardinality and
retains the pre-canonicalization binding IDs needed to match those entries;
an imported definition has no captures and an empty vector. The verifier
rejects missing, duplicate, reordered, wrongly typed, or wrongly owned
capture values. `prepareGenericModule` requires verified
Canonical input and performs this exact order:

After all function bodies and module-owned arenas exist, AST lowering calls
Task 9's phase-neutral `analyzeFreeVariables` for each open definition and
stores the resulting binding/type/ownership tuples as
`Function::GenericCaptures`; it then filters each local generic use's
`AvailableBindings` to that exact order. This is the only capture discovery
algorithm. Fragment encoding and generic preparation revalidate the stored
schema but never run a second AST-oriented capture walk. `DebugName` comes
from the semantic binding table and is diagnostic-only; matching and cache
identity use ordinal, type, and ownership.

`extractGenericDefinitions` is the read-only interface path and performs steps
1-2 below against a verified Canonical input. `prepareGenericModule` first
calls it exactly once and then performs steps 3-8. Thus Skeleton completion can construct
Complete generic fragments without rebuilding a runtime module, invoking the
specialization cache, or advancing the Typed IR phase.

1. Snapshot each local open definition's effective binder environment,
   complete capture schema, and producer binding order, then encode it with
   `encodeGenericFragment`.
2. Materialize `GenericDefinition` records for interface output, including
   unused exported generics; reject any exported generic with captures and
   omit private captured definitions from the serialized interface.
3. Move `Input.Canonical` once into
   `GenericPreparation::RuntimeModule`, retaining its shared TypeTable arena,
   then build a fresh executable function/module-owned arena transactionally
   in that same Module object; omit every open definition body, remap all
   FunctionIds/references, and return the total old-to-new FunctionId map as
   part of the unpublished transaction. Before commit, use that map plus
   declaration identity to construct a replacement `PreparedInterfaceRoots`
   bound to `RuntimeModule`: every rooted snapshot must resolve exactly once
   and retain the same signature, visibility, linkage/symbol, generic binders,
   and body/declaration status. Then reseal its append-only TypeTable generation
   and rooted-prefix fingerprint. A missing/duplicate/drifted root aborts and
   rolls back the entire preparation. Do not replace or move the TypeTable
   again. `extractGenericDefinitions` stays on the original Canonical token and
   performs no FunctionId remap.
4. Create a `TypedImportSource` internally from `Catalog`,
   `*Preparation.RuntimeModule.Types`, and the now-stable read-only
   `Preparation.InterfaceSeed`; then resolve local/imported generic calls,
   function values, trait uses, and operator target applications to one fixed
   point through the same specialization cache/resolver transaction.
5. Rewrite noncapturing calls to DirectCall. For a captured call/function
   value, map each producer binding to the call site's concrete ValueId, build
   `MakeFunctionInst` in capture ordinal order, and use ordinary callable
   application where invocation is required.
6. Invoke `runKeyOperationsPreparation` in that transaction, remap every
   deduplicated plan reference, reserve/fill both fixed-ABI adapters per final
   plan, and verify the complete adapter set.
7. At Task 15 this extension point verifies that no accelerator candidate or
   eligibility note exists. Task 16 extends this exact point by invoking
   `materializeArrayOperationCandidates` transactionally after all targets
   and key adapters are final but before phase publication.
8. Destroy the internal session; prove no generic instruction, open executable
   function/value, or open runtime descriptor remains; validate key adapters,
   Task 16 candidate materialization, the remapped interface-root token, and
   the complete module. Reserve the cache-overlay publication with
   `prepareCommit`, then in one nonthrowing critical section set
   `GenericPrepared`, publish the prepared module/result, bind the caller cache,
   and `commitPrepared` its Ready/Failed overlay. Nothing that can fail runs
   after the first of those publications.

Any error through the end of step 8 destroys the unpublished RuntimeModule and
cache overlay and leaves the caller's cache unbound and byte-identical. A
forced late accelerator-candidate failure after successful specialization
asserts exactly that state; retrying the same preparation with the same cache
must succeed. This is the transaction that makes Task 16's final extension
point atomic rather than leaving destination-local FunctionIds in a cache for
a discarded module.

Unused open types may remain in the TypeTable; representation selection
rejects TypeParameter/open-row references only when reachable from an
executable Function, Value, operation instance, or descriptor. Interface
generation consumes `LocalDefinitions` and the same shared type arena before
artifacts are moved. No session may point at a pre-move Module or seed. Add a
regression that records the arena address/domain before the Module move,
observes the identical arena from the internally created session, and proves
the session is destroyed before the result returns. Add exact tests `Generic
specialization keeps an unused
local export only in v2`, `Generic specialization closes a recursive local
generic before representation selection`, `Generic specialization rewrites a
captured first-class local generic`, and `Generic specialization shares one
path for local and imported definitions`. Also test rejection of exported
captured generics, hidden dependency captures, and capture
type/ownership/order mismatches.
Force the root-remap regression with an open private generic ordered before a
later public closed function: runtime rebuilding removes the first body and
shifts the dense FunctionId, yet the replacement token must still identify the
later function exactly and build byte-identical Complete interface/object
symbols. Reusing the pre-rebuild token against `RuntimeModule` must fail as
`StalePreparedRoots`.
Two lexically distinct local generics with the same source spelling have
different canonical declaration paths, cache entries, and fragment binding
owners; top-level/imported generics use the Symbol variant. Test shadowed
same-name nested generics and mutually recursive locals before fragment
alpha-renaming. Also test `outer x = \y -> (x, y)`, a child with inherited and
own binders, inherited deferred trait evidence, and a child whose only
openness is an ancestor row/effect binder.

AST lowering emits `TraitCallInst`/`MakeTraitMethodInst` for every constrained
method use, operator/derived method routed through a trait, and
non-resource `Closeable.close` finalizer whose instance is not yet closed.
Manifest-backed resources instead emit `ReleaseResource` directly from their
projected model declaration and never synthesize a trait-method ValueId. These operations
carry the fully qualified trait/method, complete ordered instance head, and
method type/effect arguments; display names never drive dispatch. Add both to
the exhaustive operand/remap visitors, canonical text codec, and generic
fragment format. An unresolved first-class non-resource close method is the
function-typed ValueId stored in `InvokeFinalizer`, so cleanup never stores a
premature descriptor or confuses it with `ReleaseResource`.

Trait defaults and instance bindings store the v2
`TargetApplicationTemplate`, never a bare symbol. Template type/effect IDs may
reference the declaring Trait/Instance scope and the method's Function scope.
Resolution applies the matched instance, superclass, and method substitutions
to the template and returns the complete declaration identity plus ordered
type/effect arguments; it validates target binder arity and therefore retains
phantom target binders.

Every outer-v2 target must contain the Symbol alternative. Reader and writer
reject a Local target in a trait default, instance binding, FunctionType
source identity, linkage target, or outer GenericFunction; Local identities
are legal only inside TIRF. Every template reference resolves to its declaring
Trait/Instance binder or the referenced trait-method Function binder. An
unowned or synthetic method-binding scope is invalid.

After substituting each local/imported generic use and before publishing
`GenericPrepared`, call exactly one model/semantics-level
`TraitResolver::resolve(const TraitResolutionRequest &, SourceRange)`. The
destination-bound import session implements that API over its exact
`TypeTable` and read-only local `semantics::SemanticInterfaceSeed` overlay; no
semantics/model header names or depends on a Typed IR type. Typed IR merely
adapts its persisted structural use record. The resolver uses
the same closed structural instance-resolution engine as type checking:
validate all argument arities, find the unique matching local/imported
instance, discharge its substituted constraints, follow declared superclass
evidence, prefer an explicit instance binding over the trait default, and
return the exact concrete symbol or generic key. Missing, overlapping, or
equally specific candidates are stable ranged specialization errors. Rewrite
a concrete call/value to `DirectCallInst`/`MakeFunctionInst`; route a generic
target through the ordinary `GenericCallInst`/`MakeGenericFunctionInst` cache
and then rewrite it. The verifier forbids every trait-use operation at
`GenericPrepared`, so closure conversion sees the resolved finalizer callable
and creates its environment/descriptor once.

Type checking occurs before model projection and therefore never passes a raw
`MonoTypePtr`, inference variable, or source-table ID to this model resolver.
Its `TypecheckerTraitResolver` is bound to the current `TypeArena`/
`EffectSolver` and the complete imported semantic environment below. It uses
the same domain-neutral candidate ordering, unification/constraint discharge,
specificity, default, and superclass policy, then records the selected target
and evidence as raw semantic facts. `freezeSemanticBatch` projects those facts
into the model request/application. Equivalence tests run paired raw and
projected requests and require the same target, ordered arguments, or stable
diagnostic without sharing table-local IDs.

Resolve collection/string operators to ordinary source-defined Yona functions
before the syntax tree disappears. Task 15 extends the semantic batch with:

```cpp
enum class ResolvedBuiltinOperatorKind : std::uint8_t {
  SequenceConcat = 0, StringConcat = 1,
  SequenceContains = 2, SequenceDifference = 3, SetDifference = 4
};
struct ResolvedBuiltinApplicationProjection {
  ResolvedBuiltinOperatorKind Operator;
  model::SymbolIdentity Target;
  std::vector<compiler::typechecker::MonoTypePtr> TypeArguments;
  std::vector<compiler::typechecker::EffectRef> EffectArguments;
  compiler::typechecker::MonoTypePtr FunctionType;
};
struct ProjectedResolvedBuiltinApplication {
  ResolvedBuiltinOperatorKind Operator;
  model::SymbolIdentity Target;
  std::vector<model::TypeId> TypeArguments;
  std::vector<model::EffectRowId> EffectArguments;
  model::TypeId FunctionType;
};
```

`NodeSemanticProjection`, `ProjectedNodeFact`, and `NodeSemantics` gain one
optional `BuiltinApplication`, plus a `builtinApplicationFor` accessor. Batch
projection requires the exact well-scoped instantiated FunctionType, target,
ordered arguments, operator node/range, and inferred operand/result/effect
contract to agree; any open IDs must be owned by the current declaration scope
and serialize/remap with its Canonical fragment. Generic specialization
substitutes the record and requires it closed before rewriting the operation
and before `GenericPrepared`;
missing, duplicate, foreign, or mismatched facts roll the whole batch back.
The closed allowlist and call order are normative:

| Surface operation | Resolved target and argument order |
|---|---|
| `left ++ right` for Seq | `Std\List.append left right` |
| `left ++ right` for String | `Std\String.concat left right` |
| `element in sequence` | `Std\List.contains element sequence` |
| `left -- right` for Seq | `Std\List.difference left right` |
| `left -- right` for Set | `Std\Set.difference left right` |

Every other `--` domain and every ambiguous/unsupported `++` or `in` is a
ranged type error. AST lowering validates the persisted allowlist entry and
emits `DirectCallInst` or `GenericCallInst` with those exact arguments; it
never dispatches on token spelling, `CType`, an LLVM shape, or a direct C
operator symbol. Task 11 separately owns the semantically persisted
`element in set` and `key in dictionary` checked key-query intrinsics plus
`element :: sequence` prepend and `sequence :> element` append Move
intrinsics; none creates a row in this fact.

Each resolved external target adds a canonical implicit module import and
declaration dependency to `SemanticInterfaceSeed`, deduplicated structurally
with explicit imports and serialized in v2. `regenerate_interfaces.py`
first syntax-scans an operator-using module and conservatively adds hidden
ordering edges to the applicable candidate owners among `Std\List`,
`Std\String`, and `Std\Set`; after typechecking the persisted projection must
select exactly one candidate, and only that selected edge is serialized.
Unused conservative edges affect bootstrap order/SCC membership only. No FQN
lookup side channel exists. Tests compile all five operators from modules with
no explicit helper import and prove their owner interface is available before
typechecking. The six-member bootstrap SCC carries explicit cross-edge signatures
for `Std\List.append`, `contains`, and `difference`, `Std\String.concat`, and
`Std\Set.difference`.

Implement `Std\List.append`, `contains`, and `difference` in Yona without
using the corresponding surface operators inside their own definitions.
Implement `Std\String.concat` in Yona by converting `chars` iterators to
sequences, applying `Std\List.append`, and rebuilding with `fromChars`;
embedded-NUL and invalid-internal-storage tests remain exact.
`Std\Set.contains` and `Std\Dict.contains` remain thin Yona wrappers over Task
11's private checked HAMT query leaves. `Std\Set.difference` is a Yona
definition that iterates the left set and retains only entries absent from the
right through `contains`; it does not use surface `--` in its own body. No
public direct C operator API is introduced. Typechecker,
semantic-model, TIRF/remap, interface-import, and O0-O3 source tests cover all
five rows plus the two key-query paths, implicit dependency emission, generic specialization, ownership,
and rejection paths.

Add local/imported constrained dispatch, explicit/default/superclass methods,
first-class methods, Eq operators, derived methods, generic `with`/Closeable,
phantom method binders, and missing/ambiguous/overlapping instance failures.
Require the same target for type checking and preparation and prove no trait
operation or unresolved cleanup callee survives.

- [ ] **Step 6: Implement structural specialization with recursion safety**

Use compilation-table IDs only after remapping imported types into that table:

```cpp
struct SpecializationKey {
  std::vector<model::TypeId> TypeArguments;
  std::vector<model::EffectRowId> EffectArguments;
  std::vector<model::ParameterOwnership> ParameterOwnerships;
  model::ResultOwnership ResultContract;
  model::CallingConvention Convention;
  friend bool operator==(const SpecializationKey &,
                         const SpecializationKey &) = default;
};

struct SpecializationKeyHasher {
  std::size_t operator()(const SpecializationKey &) const noexcept;
};
class SpecializationCache final {
public:
  SpecializationCache();
  ~SpecializationCache();
  SpecializationCache(const SpecializationCache &) = delete;
  SpecializationCache &operator=(const SpecializationCache &) = delete;
private:
  friend class SpecializationContext;
  struct State;
  std::unique_ptr<State> State_;
};

class SpecializationCacheTransaction final {
public:
  SpecializationCacheTransaction(SpecializationCacheTransaction &&) noexcept;
  SpecializationCacheTransaction &operator=(
      SpecializationCacheTransaction &&) = delete;
  SpecializationCacheTransaction(
      const SpecializationCacheTransaction &) = delete;
  SpecializationCacheTransaction &operator=(
      const SpecializationCacheTransaction &) = delete;
  static std::expected<SpecializationCacheTransaction, SpecializationError>
  begin(SpecializationCache &Cache,
        const ModuleMutationDomain &Destination);
  std::expected<void, SpecializationError> prepareCommit();
  void commitPrepared() noexcept;
  ~SpecializationCacheTransaction(); // uncommitted overlay is discarded
private:
  friend class SpecializationContext;
  struct State;
  explicit SpecializationCacheTransaction(std::unique_ptr<State> State);
  std::unique_ptr<State> State_;
};

class SpecializationContext final {
public:
  SpecializationContext(Module &Destination,
                        const semantics::TypedImportSource &Imports,
                        std::span<const GenericDefinition> LocalDefinitions,
                        SpecializationCacheTransaction &CacheTransaction);
  std::expected<FunctionId, SpecializationError>
  specialize(const GenericFunctionKey &Generic,
             const SpecializationKey &Key);
private:
  Module &Destination_;
  const semantics::TypedImportSource &Imports_;
  std::span<const GenericDefinition> LocalDefinitions_;
  SpecializationCacheTransaction &CacheTransaction_;
};
```

The cache key is the pair `(GenericFunctionKey, SpecializationKey)`; generic
identity is never duplicated inside `SpecializationKey`, so callers cannot
present contradictory exported symbols or local declaration paths.

A cache begins unbound. Preparation requires an unbound, empty caller cache
and starts one `SpecializationCacheTransaction` for the destination mutation
domain before any lookup. Its binding and all InProgress/Ready/Failed entries
remain in a private overlay until the complete GenericPrepared transaction is
ready to publish. Reuse of a bound/nonempty cache or a different destination
is `InvalidDestinationCache` and cannot expose a prior `Ready` FunctionId; the
second module and cache remain unchanged. Standalone specialization tests use
the same begin/prepare/commit protocol. The move constructor is defined
out-of-line and leaves the source inert, so its destructor is a no-op; a
compile/use test exercises
`auto Tx = TRY(SpecializationCacheTransaction::begin(...))`. Test one cache against two modules as
well as concurrent first binding.

Lookup finds exactly one local definition or catalog record and converts both
to the same `GenericDefinition` validation path. It compares the decoded root
against the outer record before opening the transaction. Newly appended bodies
are scanned inside that transaction; provisional SCC declarations therefore
cover self- and mutual recursion across local/imported generic roots.

Before reserving a provisional ID, specialization performs this exact
binder/application contract:

1. Let effective types be inherited followed by declared type binders, and
   effective effects be inherited followed by declared effect binders.
   Fragment lifting creates root-owned binders in exactly those orders. Require
   the decoded root `FunctionType` to declare the lifted effective vectors and
   the outer record to preserve the inherited/declared partition with
   identical kind/order. Require the key to contain exactly the effective type
   count and the count of Flexible effective variables, plus exactly one
   parameter-ownership entry per instantiated function parameter.
2. Require every key argument to be a valid destination-table ID and a closed
   graph: no reachable TypeParameter or open effect variable may remain.
   Substitute the declared constraints and discharge them through the
   destination-bound import session's model resolver; the raw TypeChecker
   adapter uses the shared selection policy described above.
3. Build one binder substitution from the decoded root's lifted effective
   type/effect vectors to the ordered key arguments. Apply it, together with the fragment-to-
   destination table remap, through one exhaustive visitor over the entire
   provisional dependency SCC: function signatures, parameters/values,
   captures, operations and operation applications, generic uses, patterns,
   control/cleanup records, nominal applications, and every nested type/effect
   edge. A missing occurrence visitor is `RemapFailed`; raw fragment IDs never
   survive. Validate each nested child's complete effective arguments against
   its own fragment-index environment after enclosing substitution.
4. Require the instantiated root signature to be closed and to match the
   key's parameter ownerships, result contract, and calling convention.
   `GenericCallInst` additionally checks argument count/types and its result
   Value type; `MakeGenericFunctionInst` checks the complete resulting
   FunctionType. Any disagreement is `InvalidSpecializationKey`, not an ABI
   adapter request.

Declared arguments remain part of the cache key even when phantom and absent
from the substituted body/signature. Thus different phantom type or effect
arguments reserve distinct specializations and cannot alias by structural
body coincidence. Recursive generic uses derive their child keys only after
substituting the enclosing key and repeat the same validation.

Every instantiated generic root and open generic dependency is rewritten to a
fresh destination-local Private Definition before commit. It has no exported
symbol or producer linkage; its deterministic internal debug name is derived
from the declaration identity plus complete specialization-key bytes, while
`FunctionType::SourceIdentity` remains only semantic/cache identity. Two
specializations in one object therefore cannot define the same external
symbol, and two consumer objects may instantiate the same imported generic
without colliding at link time. A separately declared ABI wrapper is the only
way a specialization becomes externally visible.

For a captured local definition, specialization maps the decoded root's
canonical capture bindings by ordinal to fresh destination `BindingId`s and
rewrites every capture declaration, `LexicalRefInst`, and
`AvailableBindings` occurrence through that map. The enclosing generic-use
rewrite separately maps the definition's raw `ProducerCaptureBindings` to its
concrete call-site `ValueId`s. Capture values are deliberately absent from the
specialization cache key: one structurally specialized function body is
shared by arbitrarily many closure environments, while capture schema,
types, ownership, and order remain part of fragment validation.

Specialization uses an append-only `ModuleMutationTransaction` while holding
the destination's specialization-mutation lock. It owns one Task 2
`TypeTableAppendTransaction` for type/effect/binder rows, snapshots the other
module arena sizes, builds nominal/source remaps and predeclared recursive
functions in private append buffers, and gives only the owning thread
provisional IDs (`snapshot size + local offset`). Recursive calls in the same
specialization SCC resolve those provisional declarations; concurrent callers
wait and can never observe them. Commit has an explicit prepare barrier: first
`TypeTableAppendTransaction::prepareCommit` and every module arena/index
reserve/validation succeed without publication, then one nonthrowing critical
section calls `commitPrepared` and moves the already-reserved module rows.
There is no allocation, hashing, deduplication, or callback after the first
row publishes. Success therefore appends the whole SCC without reindexing an
existing ID and publishes `Ready` with final `FunctionId`s only into the cache
transaction overlay. Failure before the
barrier discards both private buffers, leaves `Destination_` and its shared
TypeTable byte-for-byte unchanged, and publishes a permanent `Failed`
tombstone containing the diagnostic inside that overlay; it never erases a destination vector
element or remap. Repeated/concurrent callers receive the same diagnostic.

Transitive embedded Private non-generic helpers are cloned as Private and use a separate canonical merge key
of canonical source declaration identity, closed structural signature, and normalized
post-remap function bytes. Before reserving a helper provisional ID, the
transaction checks both the committed destination index and its private
buffer. A byte-identical definition reuses that FunctionId and remaps every
provisional reference to it; the helper is not appended twice. A same-key
signature/body/linkage mismatch is `RecursiveDefinitionConflict` and rolls
back the whole specialization. Lookup and comparison occur under the same
mutation lock, so concurrent specializations of two exported roots sharing
one private helper publish one helper or the same deterministic conflict,
never duplicate bodies.

Module/Public non-generic helpers are declaration-only producer-symbol
references and are never cloned. In the producer destination they coalesce
with the existing local Definition; in a consumer they remain Imported. Tests
link two specializations in one object and two independently compiled
consumers of the same generic, and assert one producer definition per
Module/Public helper plus collision-free Private specializations.

Declaration merge is symbol- and contract-based before that body-key check.
For every fragment declaration, consult the destination's canonical
`SymbolIdentity` index. An incoming Imported declaration maps to an existing
local Definition when structural signature, ownership/effects/convention, and
declared linkage agree; the local body wins and no duplicate import or LLVM
symbol is appended. Equal declarations coalesce. Any signature mismatch,
NativeExtern/Yona kind conflict, or two unequal definitions is
`RecursiveDefinitionConflict` and rolls back the whole transaction. This rule
handles an imported generic fragment that calls back into the compiling
module and is tested in both dependency orders, including the cyclic A/B
generic bootstrap fixture.

Cache entries therefore have `InProgress`, `Ready`, and `Failed` states, but
only `Ready` exposes a `FunctionId`. Test exact-key deduplication, mutually
recursive SCC commit, failure after multiple provisional declarations,
unchanged preexisting IDs/tables after failure, waiter behavior, and distinct
effects/ownership/calling conventions producing distinct functions. Add local
and imported cases for phantom type and phantom effect binders, arity errors,
open arguments, unsatisfied substituted constraints, parameter/result/
convention mismatch, and a nested generic call whose child key is produced by
the parent substitution. Add two independent generic roots sharing one
non-generic helper, a conflicting-helper fixture, and concurrent versions of
both cases.

Task 15 also performs the TypeChecker cutover to a binder-aware v2 import
bridge:

```cpp
struct ImportError {
  std::string Module;
  std::string Declaration;
  std::string Path;
  std::string Message;
};
struct ImportedFunctionScheme {
  model::FunctionDeclarationIdentity Declaration;
  std::string Name;
  typechecker::TypeScheme Scheme;
  std::vector<CallableTypeContract> CallableContracts;
  interface::v2::Linkage Linkage;
};
struct ImportedOperationScheme {
  model::EffectOperationKey Key;
  std::vector<typechecker::TypeId> TypeParameters;
  std::vector<typechecker::QuantifiedEffectVariable> EffectVariables;
  std::vector<typechecker::Constraint> Constraints;
  std::vector<typechecker::MonoTypePtr> Parameters;
  std::vector<model::ParameterOwnership> ParameterOwnerships;
  typechecker::MonoTypePtr Result;
  model::ResultOwnership ResultContract;
  typechecker::EffectRef Effects;
};
struct ImportedConstructorScheme {
  std::string Name;
  std::uint32_t Tag;
  std::vector<typechecker::MonoTypePtr> Fields;
};
struct ImportedNominalScheme {
  model::NominalTypeKey Key;
  std::vector<typechecker::TypeId> TypeParameters;
  std::vector<ImportedConstructorScheme> Constructors;
  bool Opaque;
};
struct ImportedResourceScheme {
  model::NominalTypeKey Key;
  std::vector<typechecker::TypeId> TypeParameters;
  interface::v2::ResourceAbiKind AbiKind;
  interface::v2::ResourceShareability Shareability;
  std::optional<std::string> TryRetainNativeSymbol;
  std::string ReleaseNativeSymbol;
};
struct ImportedTraitTargetScheme {
  model::FunctionDeclarationIdentity Target;
  std::vector<typechecker::MonoTypePtr> TypeArguments;
  std::vector<typechecker::EffectRef> EffectArguments;
};
struct ImportedTraitMethodScheme {
  std::string Name;
  ImportedFunctionScheme Function;
  std::optional<ImportedTraitTargetScheme> DefaultTarget;
};
struct ImportedTraitScheme {
  model::NominalTypeKey Key;
  std::vector<typechecker::TypeId> TypeParameters;
  std::vector<typechecker::Constraint> Superclasses;
  std::vector<ImportedTraitMethodScheme> Methods;
};
struct ImportedTraitMethodBinding {
  std::string Method;
  ImportedTraitTargetScheme Target;
};
struct ImportedTraitInstanceScheme {
  model::NominalTypeKey Trait;
  std::vector<typechecker::TypeId> TypeParameters;
  std::vector<typechecker::MonoTypePtr> Arguments;
  std::vector<typechecker::Constraint> Constraints;
  std::vector<ImportedTraitMethodBinding> Methods;
};
struct ImportedNameBinding {
  std::string VisibleName;
  model::SymbolIdentity Target;
  bool Reexported;
};
struct ImportedSemanticModule {
  std::vector<ImportedFunctionScheme> Functions;
  std::vector<ImportedFunctionScheme> Generics;
  std::vector<ImportedOperationScheme> Operations;
  std::vector<ImportedNominalScheme> Nominals;
  std::vector<ImportedResourceScheme> Resources;
  std::vector<ImportedTraitScheme> Traits;
  std::vector<ImportedTraitInstanceScheme> Instances;
  std::vector<ImportedNameBinding> NamesAndReexports;
};

enum class TraitCandidateOrigin : std::uint8_t {
  ExplicitInstanceBinding = 0, TraitDefault = 1, Superclass = 2
};
enum class TraitSpecificity : std::uint8_t {
  Less = 0, Equivalent = 1, Greater = 2, Incomparable = 3
};
class TraitCandidateDomain {
public:
  virtual ~TraitCandidateDomain() = default;
  virtual std::size_t candidateCount() const noexcept = 0;
  virtual bool viable(std::size_t Candidate) const = 0;
  virtual TraitCandidateOrigin origin(std::size_t Candidate) const = 0;
  virtual std::uint32_t superclassDistance(std::size_t Candidate) const = 0;
  virtual TraitSpecificity compareSpecificity(
      std::size_t Left, std::size_t Right) const = 0;
  virtual std::string stablePath(std::size_t Candidate) const = 0;
};
std::expected<std::size_t, semantics::TraitResolutionError>
selectUniqueTraitCandidate(const TraitCandidateDomain &,
                           SourceRange RequestRange);

class TypecheckerTraitResolver {
public:
  virtual ~TypecheckerTraitResolver() = default;
  virtual std::expected<ResolvedTraitTargetProjection,
                        semantics::TraitResolutionError>
  resolve(const TraitMethodSelectionProjection &, SourceRange) const = 0;
};
std::unique_ptr<TypecheckerTraitResolver> createTypecheckerTraitResolver(
    typechecker::TypeArena &, typechecker::EffectSolver &,
    std::span<const TraitDeclarationProjection> LocalTraits,
    std::span<const TraitInstanceProjection> LocalInstances,
    std::span<const ImportedSemanticModule> Imports);

class StructuralSchemeImporter final {
public:
  StructuralSchemeImporter(const interface::v2::InterfaceModule &,
      typechecker::TypeArena &, typechecker::EffectSolver &,
      typechecker::EffectAtomCatalog &);
  std::expected<ImportedFunctionScheme, ImportError>
  importFunction(const interface::v2::Function &);
  std::expected<ImportedFunctionScheme, ImportError>
  importGeneric(const interface::v2::GenericFunction &);
  std::expected<ImportedOperationScheme, ImportError>
  importOperation(const interface::v2::OperationDeclaration &);
  std::expected<ImportedNominalScheme, ImportError>
  importNominal(const interface::v2::NominalDeclaration &);
  std::expected<ImportedResourceScheme, ImportError>
  importResource(const interface::v2::ResourceDeclaration &);
  std::expected<ImportedTraitScheme, ImportError>
  importTrait(const interface::v2::Trait &);
  std::expected<ImportedTraitInstanceScheme, ImportError>
  importInstance(const interface::v2::TraitInstance &);
  std::expected<ImportedSemanticModule, std::vector<ImportError>>
  importModule();
};
```

One importer instance handles an entire imported module so shared tails,
binders, and rows retain identity inside that TypeChecker. The catalog returns
only owning structural records/views; it never returns a `MonoTypePtr`
allocated in another request's arena. `StructuralSchemeImporter` instantiates a v2 FunctionType, operation,
nominal, resource, trait, or instance declaration into fresh TypeChecker monotypes,
`TypeScheme` binders, `SchemeEffectRoot`s, callable contracts, constructor
registrations, trait/instance environments, import/reexport bindings, and one
EffectSolver graph. It preserves declaration/phantom order, shared versus
independent tails, full-application masks/exclusions, every Arrow root, open
record rows, and canonical function/operation/nominal/resource/trait keys. Function and
generic records retain declaration identity plus display name; operations
retain their complete key; trait methods retain name plus optional default
target; instance bindings retain the overridden method name. Each imported
resource records recreate their exact argument arity, ABI kind, shareability,
and explicit callback-symbol policy without exposing a constructor. In
particular, `ChannelSender` and `ChannelReceiver` survive import as distinct
CHANNEL descriptor roles rather than collapsing to GenericResource. Each imported
instance's ordered quantified `TypeParameters` owns every variable reachable
from its heads, constraints, and targets, and resolution freshly instantiates
that binder environment on every candidate attempt. `importModule` stages all eight
vectors and publishes none of them on any error; TypeChecker installs exactly
that returned environment before checking dependent declarations. Repeated imports alpha-rename
raw inference IDs but project back to identical structural bytes. TypeChecker
uses `TypecheckerTraitResolver` over this raw environment; specialization uses
the destination-bound `TypedImportSource`/model-level resolver. Task 17 only switches remaining production/LSP/
fuzzer callers and deletes v1; it does not invent a second importer.

Add import regressions with two same-signature functions and two
same-signature operations under different keys, reordered trait methods with
one omitted override supplied by a default, and two uses of the same generic
imported instance requiring disjoint fresh substitutions. Lookup must select by
the preserved identity/name rather than vector position or structural
signature alone.

Both resolvers adapt their table-local candidates to the one
`TraitCandidateDomain` policy. `viable` performs domain-local unification and
substituted-constraint discharge without committing solver/table state;
`compareSpecificity` compares the instantiated heads. The shared selector
filters nonviable candidates, prefers an explicit instance binding to a trait
default for the same winning instance, removes every strictly less-specific
candidate, then chooses the minimum superclass distance. Zero survivors is
`MissingInstance`; equivalent or incomparable multiple survivors are reported
as `OverlappingInstances` after `stablePath` sorting. The selected adapter then
commits exactly its staged substitution and materializes the target. Raw and
model adapters therefore share ordering/default/superclass policy without
sharing `MonoTypePtr`, solver, or `TypeId` domains. The factory above is
test-visible; paired tests feed alpha-equivalent raw/projected environments and
require the same target arguments or byte-identical stable diagnostic.

Interface construction is explicit:

```cpp
enum class InterfaceBuildErrorCode : std::uint8_t {
  InvalidKind, MissingPublicRoot, OpenPublicFunction, CapturedPublicGeneric,
  LocalIdentityEscapes, RemapFailed, FragmentMissing, FragmentForbidden,
  MissingGenericDefinition, DuplicateGenericDefinition,
  GenericDeclarationMismatch, StalePreparedRoots, RootSymbolMismatch,
  TypeDomainMismatch
};
struct InterfaceBuildError {
  InterfaceBuildErrorCode Code;
  std::string Path;
  std::string Message;
};
using BuildInterfaceResult = std::expected<interface::v2::InterfaceModule,
    std::vector<InterfaceBuildError>>;
using PrepareInterfaceRootsResult =
    std::expected<PreparedInterfaceRootsRef,
                  std::vector<InterfaceBuildError>>;
struct SkeletonFunctionDeclaration {
  model::FunctionDeclarationIdentity Identity;
  std::string Name;
  model::TypeId Signature;
  interface::v2::Visibility Access;
  interface::v2::Linkage SymbolLinkage;
  std::optional<model::GenericBinderEnvironment> GenericBinders;
};
struct SkeletonDeclarationModule {
  model::ModuleIdentity Identity;
  std::shared_ptr<model::TypeTable> Types;
  semantics::SemanticInterfaceSeed Seed;
  std::vector<SkeletonFunctionDeclaration> Functions;
  std::vector<interface::v2::SourceFile> Sources;
};
using PrepareSkeletonInterfaceRootsResult =
    std::expected<PreparedSkeletonInterfaceRootsRef,
                  std::vector<InterfaceBuildError>>;
PrepareInterfaceRootsResult
prepareInterfaceAbiRoots(
    GenericPreparationInput &Input,
    const semantics::TypedImportSource &Imports);
PrepareSkeletonInterfaceRootsResult sealSkeletonInterfaceRoots(
    SkeletonDeclarationModule Declarations);
BuildInterfaceResult buildCompleteInterfaceModule(
    const Module &InterfaceSource,
    const PreparedInterfaceRoots &Roots,
    std::span<const GenericDefinition> LocalDefinitions);
BuildInterfaceResult buildSkeletonInterfaceModule(
    const PreparedSkeletonInterfaceRoots &Roots);
```

`PreparedInterfaceRoots` has no public constructor. It owns the promoted
`SemanticInterfaceSeed`, the canonical module identity, source `TypeTable`
domain/generation token, a canonical fingerprint of the rooted type prefix,
and one ordered snapshot per rooted FunctionId containing declaration
identity, signature, visibility, linkage/symbol, generic binders, and body/
declaration status. `prepareInterfaceAbiRoots` creates it only after the root
promotion transaction commits and stores the same reference on
`GenericPreparationInput`. `prepareGenericModule` uses its total FunctionId
remap to create a replacement snapshot bound to the rebuilt RuntimeModule and,
after any append-only specialization, atomically reseals its table-generation
token only if every remapped root and rooted type prefix is byte-identical;
`extractGenericDefinitions` preserves it without resealing. A null token is a
preparation error.

`PreparedSkeletonInterfaceRoots` likewise has no public constructor, but it is
not a Typed IR token and contains no FunctionId, block, body, runtime phase, or
descriptor. `sealSkeletonInterfaceRoots` consumes a private
`SkeletonDeclarationModule` produced inside the provisional SCC batch,
validates its destination TypeTable, public/promoted declaration identities,
signatures, binders, linkage, sources, and seed as one closed declaration
snapshot, and forbids bodies, fragments, local identities, or catalog
lookups. This avoids inventing a declaration-only state for `typed_ir::Module`,
whose verifier correctly requires every Definition to have an Entry/body. The
skeleton token cannot escape the internal bootstrap path.

`buildCompleteInterfaceModule` consumes the token with the exact interface
source module, not an independently supplied seed. It accepts exactly either
(a) Canonical plus the original prepared token for bootstrap extraction, or
(b) GenericPrepared plus the remapped/resealed token for normal compilation;
all other phases and either cross-paired token fail. It first checks module identity, TypeTable
domain/generation/prefix, and every FunctionId/linkage snapshot, then uses the
token's seed. A stale token, token from another table/module, changed root
visibility/signature/symbol, rolled-back generation, or post-preparation root
mutation fails before allocating the public slice. Tests forge each mismatch
and require zero output/catalog lookup; one test exercises each accepted phase
and cross-pairs both tokens to prove rejection. This is the evidence that
object and interface roots are the same functions.

`buildSkeletonInterfaceModule` consumes only the sealed skeleton token. It
validates the stored table domain/generation and declaration fingerprint,
emits `InterfaceKind::Skeleton`, requires every generic fragment absent, and
has no code path to a Typed IR Function or body. Tests prove a body-bearing or
post-seal-mutated declaration set is rejected and that a skeleton token cannot
be supplied to the Complete builder (or conversely) at the C++ type level.

It builds a fresh public `TypeTable` rooted
only at emitted imports, nominals, resources, traits, instances, operations,
functions, generic outer signatures, and target templates. Every visible
`Seed.Declarations.Resources` row is a root even when no function mentions
it; its declaration binder, parameter graph, ABI kind, shareability, and
callback identities are emitted together. Binder remapping is seeded by
the complete declaration forest—including phantoms—before every seed and
GenericFunction reference is rewritten. Only then does it call
`encodeInterfaceTables` on that slice. Runtime specializations, descriptors,
wrappers, and unrelated/private table entries never enter v2; private
captured/open definitions live only inside an emitted public root's TIRF. A
local `FunctionDeclarationIdentity` is forbidden in the outer v2 slice. Tests
prove unused private/open types and call-site specializations cannot change
the public bytes or golden hash. A separate test exports an otherwise-
unreferenced generic resource and requires its complete row/binder in both
Skeleton and Complete output.

`SemanticInterfaceSeed::Generics` is the sole source of every outer generic
declaration in either interface kind. It contains the declaration identity,
name, complete outer signature, effective binders (including phantoms),
constraints, visibility, and linkage, but never a body or fragment.
Both builders always emit/remap the public or promoted rows from that vector.
For `Skeleton`, every emitted generic has an absent fragment and the skeleton
API has no `LocalDefinitions` parameter. For `Complete`, the builder joins every emitted
generic seed to exactly one `GenericDefinition` by canonical key, then requires
byte-equal outer signature, binders, constraints, visibility, and linkage
before attaching that definition's canonical fragment. A missing, duplicate,
or mismatched join fails atomically with the three dedicated errors above; a
definition without an emitted seed is likewise rejected. No interface builder
discovers a declaration by inspecting a fragment or Typed IR body. Tests cover
phantom-only generics, seed/definition order permutations, each mismatch field,
missing/duplicate/extra definitions, and a mutually recursive A/B pair whose
Skeleton outer records are usable before either Complete fragment exists.

Every emitted trait default or instance binding is also an ABI root: close
over its `TargetApplicationTemplate.Target` to a fixed point. Imported and
NativeExtern targets must resolve in the catalog. A local non-generic target
is emitted as a Module-visible Function with a linkable Symbol; a local generic
target is emitted as a Module-visible GenericFunction with a Complete fragment.
These support records are reachable only through trait evidence, not direct
source import. A Local identity, captured target, missing body/fragment, or
private nonlinkable target is `MissingPublicRoot`. Tests cover public defaults/
instances backed by otherwise-private non-generic and generic helpers plus
rejection of captured/local-only helpers. Skeleton forbids all fragments;
Complete requires one canonical fragment per emitted generic.

`prepareInterfaceAbiRoots` computes that fixed point while
`GenericPreparationInput::Canonical` is still mutable, after derivation and
before generic extraction/runtime-module rebuilding. In one transaction it
promotes each eligible otherwise-private Definition/GenericFunction to Module
visibility, assigns/verifies its canonical `SymbolIdentity`, updates the
semantic seed and local-generic record, and rolls back all promotions on any
captured/local-only/missing target. `buildCompleteInterfaceModule` remains read-only
and only validates that its ABI roots already match the canonical module.
Consequently the Complete interface and emitted object describe the same
symbols; interface construction can never invent an export after function-set
freeze.
Regression coverage combines an otherwise-private local generic ABI root with
an imported trait target, destroys session B before building, and verifies the
complete interface/object symbols. A forced failure after several promotions
must restore the module, seed, local-generic records, and type-table bytes;
the read-only builder must perform zero catalog lookups.

Derivation is an atomic pass:

```cpp
struct DerivationResult {
  std::vector<FunctionId> Functions;
  std::vector<LocalGenericSeed> GenericSeeds;
};
std::expected<DerivationResult, std::vector<DerivationDiagnostic>>
runDerivations(GenericPreparationInput &Input,
               const semantics::TraitResolver &Traits);
```

It runs while the module is Canonical, after the local semantic seed
and trait overlay exist and before `prepareGenericModule`. It emits
`TraitCallInst`/`MakeTraitMethodInst`, generated generic functions, and derived
`TraitInstance` records into that same seed and appends metadata for every open
generated function to `Input.LocalGenerics`; all three mutations commit or
none do. Generated generic bodies then use
ordinary fragment extraction and specialization. Task 6 retains the shared
pattern/decision compiler, but Task 15 owns trait-aware derivation and its
interface metadata. Normal pipeline order is exact: lower the bundle; create
CompleteOnly session A through its expected result, run `runDerivations`, and
destroy A; create fresh CompleteOnly session B through its expected result,
run `prepareInterfaceAbiRoots`, store the returned token, and destroy B; call
`prepareGenericModule(std::move(Input), Catalog, Cache)` exactly once, letting
it perform the sole extraction/internal-session lifecycle; immediately call
`buildCompleteInterfaceModule(Prepared.RuntimeModule,
*Prepared.InterfaceRoots, Prepared.LocalDefinitions)` when an interface is requested; only
then move `Prepared.RuntimeModule` into runtime passes. Test a
generic derived ADT both locally and through v2.

`SemanticInterfaceSeed::Declarations.Operations` is the sole operation source
of truth. Each interface builder derives each outer v2 operation record from
that projected declaration after public-table remapping; it rejects duplicate
operation identities or any attempted separately supplied record before
publishing the interface. Tests inject duplicate projected declarations and a
deliberately mismatched derived signature/access record and require one stable
transactional error with no partial interface.

- [ ] **Step 7: Add the replacement catalog and cross-module cases**

`TypedInterfaceCatalog` reads only v2, remaps structural tables, installs
imports/reexports/ADTs/records/traits/defaults/superclasses/instances, and
operation declarations, source metadata, and external linkage, and retains
opaque generic bytes until specialization. It rejects any imported generic
with a nonempty capture schema: lexical environments are module-local values,
not package ABI. Test private generic
dependencies, first-class imported functions, recursive generics, trait
dispatch, object exports, and module identity collisions. Do not call
`GenericFunctionSourceService`.

Catalog insertion creates explicit Typed IR Imported/NativeExtern declaration
functions with no Entry and preserves visibility/symbol identity. During
specialization, embedded open generic Definitions and Private helper bodies
become destination-local Private definitions; same-module Module/Public,
imported, and native helpers remain declaration-only with their producer
linkage and never turn into duplicate bodies. Object/export tests assert the
exact LLVM symbol, one producer definition across two consumer objects, and
reject name-derived linkage.

Before any canonical source is eligible for bootstrap, create one checked,
compiler-owned `lib/stdlib-manifest.toml`. It contains exactly 45 `[[module]]`
rows with `{identity, source, source_sha256, interface, publish_api}`: Prelude; the existing
27 Std sources (including the three `Std/Constants/*` modules); and the 17 new
root Std sources in this task. Exactly the 41 root `Std/*.yona` rows have
`publish_api = true`; Prelude and the three non-root rows do not. Source and
interface relative paths are normalized, pairwise unique, and must match the
declared module identity. `source_sha256` is lowercase SHA-256 of LF-normalized
source bytes and is part of the embedded capability identity. During Task 15
only, `generate_stdlib_manifest.py --update-source-digests` rewrites those 45
fields after all canonical sources are final; ordinary configure, CI,
bootstrap, and `--check` modes are read-only and fail on any mismatch. No
directory scan is authoritative.

Each module row contains a closed `[[module.leaf]]` list. A leaf records its
source declaration name and one of
`SemanticIntrinsic`, `CheckedDirectV2`, `CheckedOutcomeV2`, or
`StableExternal`. `SemanticIntrinsic` requires exactly one
`semantic_intrinsic` enum plus `intrinsic_lowering = "RuntimeEntry"` or
`"CompilerPlan"`. RuntimeEntry intrinsics require an exact `native_symbol`,
owner header/source, and ABI archetype; CompilerPlan intrinsics forbid those
native fields. Exactly the six Iterator-producing RuntimeEntry rows (the four
Task 11 From entries and two Task 15 File entries) additionally require one
`iterator_factory` record containing a stable adapter identity plus exact
`advance_borrowed_unique_symbol` and `destroy_state_symbol`; all other rows
forbid that record. The generator validates both callback prototypes, global
symbol uniqueness, the factory/type relationship, and emits those fields into
the same RuntimeEntry registry row used by descriptor planning. Every native route
requires exactly one `native_symbol`; `CheckedOutcomeV2` additionally requires
one closed `outcome_opcode` enum, while all other routes forbid that field. Checked
native rows also record `NativeAsyncKind`, owning public runtime header,
implementing source/component, and ABI archetype. `CheckedDirectV2` means Task 7's one
`YonaAbiCheckedDirectNativeEntryV2` prototype. `CheckedOutcomeV2` must name an
exact dedicated checked Typed IR opcode and Task 13/14 API; the generator emits
the only opcode-to-symbol table from those rows and rejects a missing, extra,
or duplicate opcode. It cannot lower as a generic NativeExtern.
`StableExternal` is allowed only for the nine
`Std\Math` rows below and has the literal `double(double)` ABI.
StableExternal rows additionally require
`external_provider = "SystemMath"` and
`external_declaration = "double(double)"`; they forbid project owner-header/
source fields, require `async_kind = "Synchronous"`, and resolve through the platform C math library/
CRT target selected by CMake. All paths are explicit normalized strings and
all symbols are explicit strings in
the checked manifest; suffix/prefix inference
is forbidden.

The RuntimeEntry intrinsic rows are exactly Task 11's thirteen array, two key-
query, five Iterator, and three String leaves plus Task 15's two File-iterator
leaves. `generate_stdlib_manifest.py` extends Task 11's checked-in
`RuntimeEntryRegistry.def` from 23 to 25 rows and thereafter regenerates and
byte-checks it from the manifest. Their enum/module/storage discriminants map
to exact ABI-distinct C entries solely through that compiled generated table.
`CancellationCheck` and `TaskSpawn` are the only CompilerPlan intrinsics and
have no direct C symbols; Task 12 lowers the former to its explicit context
query and Task 14 planning expands the latter to the task ABI. The generator
rejects any other lowering classification,
symbol-less RuntimeEntry, symbol-bearing CompilerPlan, or duplicate
enum/module identity. LLVM and `ReplacementAbiConformance` consume that one
generated enum-to-entry table; neither contains a parallel spelling/symbol
switch. The Task 15 registry regression proves the original 23 keys retain
their numeric identity and exact rows, the two File keys are appended, and all
25 semantic key combinations resolve in both directions.

The manifest deliberately does not duplicate a leaf FunctionType. The
digest-pinned canonical `.yona` declaration is the single structural
signature authority; after privileged parsing, semantic projection produces
its binders, constraints, parameter/result ownership, convention, and effect
row. Route-specific validators check that projected type against the closed
intrinsic/Outcome/StableExternal archetype and produce one immutable
`CanonicalStdlibLeafContract` per row. Private leaf declarations are not
ordinary importable v2 Functions and never become public-interface roots.
`CanonicalStdlibLeafContractSet` is instead an internal, deterministic
module/declaration-ordered projection containing the exact FunctionType,
binders, ownership/effects, route, opcode/symbol, and owning component. The
projector installs its closed route and async strategy directly on the
private leaf's `FunctionLinkage`; AST lowering reads that authenticated enum
pair to choose `CheckedDirectNativeCallInst`, `NativeAsyncCallInst`, or
ordinary StableExternal `DirectCallInst`. No later pass can recover a route
from a C symbol or module name. The
bootstrap generator writes its canonical build-tree-only
`stdlib-leaf-contracts.json` sidecar beside temporary interfaces; it is never
installed or checked into `lib/`. `ReplacementAbiConformance` consumes the
same in-memory contract set through the test fixture, while symbol/metadata
contracts read the sidecar and require byte-equivalence to a fresh projection.
A type cannot drift without changing the pinned source digest and sidecar
bytes, and TOML can never disagree with source about a second handwritten type
spelling.

The manifest first defines exactly eight top-level `[[resource_callback]]`
profiles with `{id, try_retain, release}`; `try_retain` is omitted for a linear
profile. Symbols are unique between profiles. Seven profiles are referenced by
exactly one resource row; the `ChannelEndpoint` profile is referenced by
exactly the Sender and Receiver rows and is the sole permitted callback
sharing. The generator rejects an unreferenced profile, inline callback
symbols, any other reuse, or a third resource referencing `ChannelEndpoint`.
This models one implementation contract once without weakening global symbol
uniqueness.

A module may also contain closed `[[module.resource]]` rows with
`{name, type_parameters, visibility, abi_kind, shareability, callbacks}`.
Canonical stdlib source refers to such a row with the compiler-only declaration
`resource Name a b`; policy and callback symbols never appear in source. The
lexer/AST/visitor/parser admit `ResourceDecl{Name, TypeParameters}` only under
the same unforgeable `CompilerStdlib` provenance as `intrinsic`; ordinary user
source and an imported/re-exported spelling cannot manufacture one. The
TypeChecker requires exactly one manifest row, seeds an opaque structural
`ResourceType` before checking function signatures, and forbids constructors,
record fields, pattern matching, derivation, or direct allocation for that
identity. `export type Name` controls its manifest-matching visibility.
Projection emits the v2 `ResourceDeclaration` and descriptor generation uses
only its verified callback policy. Parser/visitor tests dispatch a resource
through `AstNode *` and cover generic binders, export, missing/extra manifest
rows, forged provenance, attempted constructor/pattern use, and callback
identity mismatch. `Std\Io.InputStream = Stdin` and
`Std\Io.OutputStream = Stdout | Stderr` are ordinary closed nominal enums,
not resources or integer fds.

`LinearityChecker` treats a Linear `ResourceType` exactly as a linear value,
using its projected declaration rather than the removed `Linear a` wrapper:
Consume ends the source owner, Borrow is scoped and cannot escape, every path
must drop or transfer the owner once, and branch joins must agree on state.
AlwaysShareable resources permit checked retain/copy but still balance each
owner. Tests cover use-after-consume, double consume, escaping borrow,
asymmetric branch transfer, implicit drop, returned ownership, generic
Sender/Receiver arguments, and a callback-policy mismatch. YonaStyle accepts
the compiler-only declaration but enforces PascalCase resource names and
lowercase type parameters; ordinary-source style mode never grants parser
authority.

Update the canonical TextMate grammar and the checked VS Code copy together so
`resource` and compiler-only `intrinsic` declarations tokenize as declaration
keywords. Highlighting grants no parser capability: ordinary source still
fails the provenance checks above. `check-yona-grammar.sh` verifies the two
JSON grammars are synchronized, contains positive fixtures for both keywords,
and rejects a copy missing either token. The VS Code extension's independent
`editors/vscode/src/test/run.ts` required-token inventory names both as well;
Task 15 runs both checks. The tree-sitter gitlink is deliberately outside this
atomic repository change and may be updated in a separately versioned
follow-up.

`scripts/generate_stdlib_manifest.py` parses only this closed TOML schema at
configure time and deterministically emits a build-tree C++ include plus its
SHA-256; no generated file is checked in. `StdlibManifest.cpp` embeds that
data into the compiler. The generator rejects unknown fields/kinds, missing or
duplicate modules/leaves/resources/callback profiles/paths/symbols/opcodes, a
public leaf or stale digest, an intrinsic with invalid/missing lowering fields,
a checked native or RuntimeEntry intrinsic without header/source/ABI fields, a
CheckedOutcome row without exactly one
outcome opcode,
invalid StableExternal provider/declaration fields, and any count other than
45/41. The sole public-leaf exception is the exact nine-row `Std\Math`
`StableExternal` allowlist: those names are public NativeExtern declarations
with literal `double(double)` linkage and no Yona wrapper. Private-only remains
mandatory for SemanticIntrinsic, CheckedDirectV2, and CheckedOutcomeV2. The
generator rejects a tenth public leaf, a private/misnamed Math row, or applying
the exception to any other route/module. A second generation must be
byte-identical.
It validates every source digest before emitting the include; the include
retains each digest so a compiled bootstrap capability can recheck bytes
before privileged parsing.
The semantic validator projects each actual declaration into the current
TypeTable and applies the manifest route's structural validator using
canonical bytes—not display spelling. It rejects a route/type mismatch before
lowering and never reparses a type from TOML.

In `CompilerStdlib` mode the TypeChecker requires a one-to-one match between
the current module's `resource` plus private `intrinsic`/`extern` declarations
and its embedded resource/leaf rows. It rejects an omitted/extra declaration,
wrong resource visibility/policy, a public non-StableExternal leaf, wrong type,
ownership/effect/async kind, intrinsic discriminant, C symbol, or owning
component before lowering. Ordinary user modules never acquire this policy or
the compiler provenance capability. The v2 writer, bootstrap graph,
intrinsic projection, NativeExtern lowering, replacement-boundary metadata,
and `ReplacementAbiConformance` all consume the same embedded rows; none
maintains another symbol allowlist. Complete interfaces cannot contain an
unmatched private leaf in a generic-fragment closure. Task 16's opaque
`CanonicalStdlibManifest` is a validated capability over this embedded table,
not another hard-coded 45-entry list.

The manifest's leaf inventory is exact at the source-name level:

| Canonical module(s) | Exact private leaf names | Required route |
|---|---|---|
| `Prelude` | `primitiveEq{Int,Float,Bool,String,Symbol}`, `primitiveCompare{Int,Float,Bool,String}`, `primitiveHash{Int,Float,Bool,String,Symbol}`, `primitiveShow{Int,Float,Bool,String,Symbol}` | 19 `CheckedDirectV2` symbols, each explicitly renamed `YonaRuntimeAbiPrelude*V2` |
| `Std\Constants\Platform` | `rawPageSize`, `rawCacheLineSize`, `rawPathMax`, `rawNameMax`, `rawCpuCount`, `rawIsLittleEndian`, `rawOsName`, `rawArchitecture` | eight `CheckedDirectV2` platform symbols |
| `Std\Convert` | `rawParseInt`, `rawParseFloat`, `rawIntToFloat`, `rawFloatToInt`, `rawDecodeUtf8` | five `CheckedDirectV2` entries returning explicit `Result` values |
| `Std\Iterator` | `rawFromSequence`, `rawFromByteArray`, `rawFromIntArray`, `rawFromFloatArray`, `rawNext` | Task 11's five semantic intrinsics |
| `Std\Json` | `rawParse`, `rawStringify`, `rawStringifyString`, `rawStringifyBool`, `rawStringifyFloat`, `rawNull`, `rawParseInt`, `rawParseFloat` | eight `CheckedDirectV2` codec entries |
| `Std\Math` | `sqrt`, `sin`, `cos`, `tan`, `log`, `exp`, `floor`, `ceil`, `round` | the complete and only `StableExternal` allowlist |
| `Std\Regex` | `rawCompile`, `rawMatches`, `rawFind`, `rawFindAll`, `rawReplace`, `rawReplaceAll`, `rawSplit` | seven descriptor-backed `CheckedDirectV2` PCRE2 entries over an opaque Shareable `Regex` resource, never an integer handle |
| `Std\Utf16` | `rawOffsetToLine`, `rawOffsetToCharacter`, `rawPositionToOffset` | three strict-UTF-8 `CheckedDirectV2` entries |
| `Std\ByteArray`, `Std\IntArray`, `Std\FloatArray` | each `rawAllocZeroed`, `rawLength`, `rawGet`, `rawPutMove`; ByteArray also `rawFromString` | Task 11's thirteen array intrinsics |
| `Std\Set`, `Std\Dict` | `rawContains` in each module | Task 11's two key-query intrinsics; every other operation is Yona |
| `Std\String` | `rawLength`, `rawCharAt`, `rawFromChars`, `rawToLower`, `rawToUpper` | three Task 11 String intrinsics plus two Unicode `CheckedDirectV2` entries |
| `Std\Crypto` | `rawRandomBytes`, `rawSha256` | two bit/entropy `CheckedDirectV2` entries; hex/UUID plumbing is Yona |
| `Std\Encoding` | `rawBase64Encode`, `rawBase64Decode`, `rawUrlEncode`, `rawUrlDecode` | four codec `CheckedDirectV2` entries; hex and HTML transforms are Yona |
| `Std\File` | `rawOpen`, `rawClose`, `rawExists`, `rawFlush`, `rawListDir`, `rawRemove`, `rawSeek`, `rawSize`, `rawTell`, `rawTruncate`, `rawAppendFile`, `rawWriteFileBytes`, `rawReadLines`, `rawReadChunks`, plus the six named file submissions | twelve `CheckedDirectV2` resource/OS leaves, the two dedicated file-iterator semantic intrinsics below, plus exactly Task 14's six platform Outcome operations |
| `Std\Net` | `rawClose`, `rawPeerAddress`, `rawTcpListen`, `rawUdpBind`, plus the seven named net submissions | four `CheckedDirectV2` OS leaves plus exactly Task 14's seven platform Outcome operations |
| `Std\Process` | `rawCloseStdin`, `rawExec`, `rawExecArgs`, `rawExecStatus`, `rawExecutablePath`, `rawExit`, `rawGetArgs`, `rawGetcwd`, `rawGetenv`, `rawHostname`, `rawKill`, `rawPid`, `rawReadAll`, `rawReadLine`, `rawRun`, `rawRunWithArgv0`, `rawSetenv`, `rawSpawn`, `rawTempDir`, `rawTempFile`, `rawWait`, `rawWriteStdin`, `rawYonaVersion` | `CheckedDirectV2`; the blocking leaves `rawExec`, `rawExecArgs`, `rawExecStatus`, `rawReadAll`, `rawReadLine`, `rawRun`, `rawRunWithArgv0`, `rawWait`, and `rawWriteStdin` carry `ThreadPool`; all others carry `Synchronous` |
| `Std\Log` | `rawWrite`, `rawGetLevel`, `rawSetLevel` | three `CheckedDirectV2` mutable logging leaves |
| `Std\Random` | `rawNextU64` | one entropy `CheckedDirectV2` leaf; bounded int/float/choice/shuffle are unbiased Yona definitions |
| `Std\Time` | `rawEpochSeconds`, `rawMonotonicMicros`, `rawSleepMicros`, `rawFormatUtc` | four `CheckedDirectV2` clock/OS leaves |
| `Std\Channel` | `rawCreate`, `rawSend`, `rawReceive`, `rawTryReceive`; `rawClose`, `rawClosed`, `rawLength`, `rawCapacity` | first four are the exact source Outcome mappings below; last four are `CheckedDirectV2` |
| `Std\Io` | `rawWriteString`, `rawWriteLine`, `rawWriteBytes`, `rawReadLine`, `rawReadChunk`, `rawReadExactBytes`, `rawIsTty`, `rawFlush` | eight `CheckedDirectV2` closed-standard-stream leaves; all except `rawIsTty` carry `ThreadPool`; filesystem work is expressed through `Std\File` |
| `Std\Task` | `rawCheckCancellation`, `rawSpawn` | the two `CancellationCheck` and `TaskSpawn` CompilerPlan semantic intrinsics; neither has a C symbol, and task/group runtime operations remain compiler-only structured-concurrency opcodes |
| `Std\Gpu` | `rawFloatArrayMul2Async`, `rawFloatArrayScaleAsync`; `rawBackendName`, `rawVulkanStatus`, `rawVulkanLastNote`, `rawHasGpu`, `rawHasSimd`, `rawVulkanAvailable`, `rawVulkanTimelineSemaphore`, `rawVulkanLastIssueKind`, `rawAvailable`, `rawPhysicalDeviceCount`, `rawUpload`, `rawMaterialize`, `rawLength`, `rawMapAdd`, `rawMapMul`, `rawMapSquare`, `rawFilterGreaterThan`, `rawFilterLessThan`, `rawReduceSum`, `rawMapReduceGraph`, `rawMapFloat`, `rawReduceFloat`, `rawAllocPinnedFloats`, `rawClosePinnedFloats`, `rawPinnedLength`, `rawPinnedGet`, `rawPinnedSet`, `rawPinnedToFloatArray`, `rawCopyFloatArrayToPinned`, `rawPinnedBackend`, `rawMapFloatPinned` | the first two are the exact source Outcome mappings below; the remaining 31 are the exact `CheckedDirectV2` block below; Task 14's raw-pointer Vulkan helpers and Task 16's eight transparent kernels are compiler-only and never source leaves |
| every other canonical module | none | pure Yona only |

`Std\Gpu.Buffer` and `Std\Gpu.PinnedFloats` are not leaf rows and carry no
native route. They appear only in the separate nine-row ResourceDeclaration
inventory with their retain/release/shareability contracts below.

The two file-iterator intrinsics use a dedicated descriptor because a generic
CheckedDirect function descriptor cannot carry the source-specific iterator
state-vtable contract or retain a borrowed linear file owner:

Task 15 extends the closed intrinsic enum after Task 14 with
`FileReadLinesOpen = 17` and `FileReadChunksOpen = 18` and adds the semantic
facts here, not in Task 11:

```cpp
struct FileIteratorIntrinsicCallProjection {
  SemanticIntrinsicKind Kind; // one of the two values above
  compiler::typechecker::MonoTypePtr DeclaredFunctionType;
};
struct ProjectedFileIteratorIntrinsicCall {
  SemanticIntrinsicKind Kind;
  model::TypeId FunctionType;
};
```

`NodeSemanticProjection`, `ProjectedNodeFact`, and `NodeSemantics` gain the
optional `FileIteratorIntrinsicCall` field and
`fileIteratorIntrinsicCallFor` accessor in this task. The shared semantic
batch projects it transactionally in the owning binder scope; AST lowering
consumes it into exactly one of the two instructions below. Provenance,
direct/full application, exact generic type/ownership, parser/printer/remap,
rollback, and phase tests are delivered here, so neither earlier milestone
depends on these later instructions.

```c
typedef struct YonaAbiFileIteratorDescriptor {
  uint32_t AbiVersion;
  uint32_t Reserved;
  uint64_t StructuralFingerprint;
  const uint8_t *CanonicalBytes;
  uint64_t CanonicalByteCount;
  const YonaAbiIteratorTypeDescriptor *Iterator;
  const YonaAbiIteratorFactoryDescriptor *Factory;
  const YonaAbiTypeDescriptor *InputType;
  const YonaAbiTypeDescriptor *ElementResultType;
  const YonaAbiTypeDescriptor *FileErrorType;
} YonaAbiFileIteratorDescriptor;

bool YonaRuntimeAbiFileReadLinesOpenV2(
    const YonaAbiFileIteratorDescriptor *Descriptor,
    const YonaAbiValue *BorrowedPathString,
    YonaAbiValue *EmptyIterator,
    YonaControlOutcome *EmptyFailure);
bool YonaRuntimeAbiFileReadChunksOpenMoveV2(
    const YonaAbiFileIteratorDescriptor *Descriptor,
    YonaAbiValue *OwnedFileHandle, int64_t Count,
    YonaAbiValue *EmptyIterator,
    YonaControlOutcome *EmptyFailure);
```

Task 15 extends the module-owned checked-runtime vocabulary exactly as
follows:

```cpp
using FileIteratorDescriptorId =
    StrongId<struct FileIteratorDescriptorIdTag>;
struct FileIteratorDescriptorPlan {
  FileIteratorDescriptorId Id;
  IteratorTypeDescriptorId Iterator;
  IteratorFactoryDescriptorId Factory;
  model::TypeId InputType;
  model::TypeId ElementResultType;
  model::TypeId FileErrorType;
};
struct FileReadLinesOpenInst {
  FileIteratorDescriptorId Descriptor;
  ValueId PathString;
};
struct FileReadChunksOpenMoveInst {
  FileIteratorDescriptorId Descriptor;
  ValueId FileHandle;
  ValueId Count;
};
```

`Module::FileIteratorDescriptors` is canonical structural order and
printer/parser/remap/verification cover it. Each plan's factory must point
back to its stored iterator type plan, have the exact File-backed source/state
type and authenticated Advance/Destroy callbacks, and produce the stored
element Result; a
mismatched factory/type pair is rejected before body emission. Both
instructions are ordinary
Canonical operations until `runRuntimeFailureNormalization` wraps them in
`CheckedRuntimeOp`; they remain checked operations through LLVM. Task 15
extends every exhaustive `InstructionPayload`/`CheckedRuntimePayload` visitor
without removing an earlier alternative.

LLVM lowering is part of this task, not deferred to the cutover.
`LlvmModuleLowerer` emits exactly one private constant
`YonaAbiFileIteratorDescriptor` global per plan ID, in canonical ID order,
using the already-emitted input, element-Result, FileError, and Iterator
type/factory descriptor globals; it rejects a missing or structurally unequal referenced
descriptor before emitting a function body. `LlvmBlockLowerer` has two
exhaustive `CheckedRuntimeOp` cases. It allocates an empty `YonaAbiValue`
result slot and empty diagnostic, passes the exact descriptor-global pointer,
and calls only the two prototypes above with the platform C calling
convention. ReadLines passes the borrowed path unchanged. ReadChunks passes
the address of the owned FileHandle slot; the normal edge may observe the
handle cleared only after a true return, while the false edge observes the
original non-null owner and empty result. Both branch on the returned `i1`
through Task 7's common checked-runtime success/ABI-failure terminator and move
the iterator result into the normal successor; neither synthesizes an unwind,
sentinel, generic native call, or descriptor at the call site. LLVM tests
inspect the constant layout, exact prototypes/calling convention, both
poststates, and balanced early-drop/error execution.

`rawReadLines` has source type
`String -> Iterator (Result ByteArray FileError)`; `rawReadChunks` has
`FileHandle -Consume-> Int -> Iterator (Result ByteArray FileError)`.
The descriptor requires the exact matching input, iterator, element-Result,
error, state-vtable, ownership, and empty effect contracts. Its canonical
bytes include all those identities except callback addresses, and lowering
resolves the addresses from the authenticated factory registry. A
false ABI/infrastructure failure leaves every input and output unchanged and
writes only the reserved diagnostic. On true, Lines snapshots the path and
the produced iterator owns any opened handle; Chunks clears the FileHandle and
the iterator owns it. Open/count/read errors appear exactly once as an `Err`
iterator element followed by `None`; successful reads are `Ok ByteArray`, and EOF is
`None`. Exhaustion, early iterator drop, and every error close/release the
handle exactly once. The two semantic facts lower only to
`FileReadLinesOpenInst`/`FileReadChunksOpenMoveInst`, whose exact checked ABI
forms survive to LLVM; no generic native call, retained Borrow, sentinel, or
legacy iterator constructor exists.

`Std\File` declares and publicly exports these exact closed source types in
declaration/tag order:

```yona
type FileMode = Read | Write | ReadWrite | Append
type Whence = SeekSet | SeekCur | SeekEnd
type FileError = FileNotFound | FilePermissionDenied | FileAlreadyExists
  | FileNotDirectory | FileIsDirectory | FileNoSpace | FileClosed
  | FileUnexpectedEof | FileInvalidArgument | FileUnsupported
  | FileSystemError Int
resource FileHandle
```

Thus `FileError` tags are exactly `0..10`; only `FileSystemError` has a payload,
one Trivial positive native error code. POSIX maps `ENOENT`, `EACCES|EPERM`,
`EEXIST`, `ENOTDIR`, `EISDIR`, `ENOSPC`, and `EBADF` to the corresponding
named constructors; Windows maps `ERROR_FILE_NOT_FOUND|ERROR_PATH_NOT_FOUND`,
`ERROR_ACCESS_DENIED`, `ERROR_FILE_EXISTS|ERROR_ALREADY_EXISTS`,
`ERROR_DIRECTORY`, `ERROR_DISK_FULL|ERROR_HANDLE_DISK_FULL`, and
`ERROR_INVALID_HANDLE` likewise. Invalid source counts/offsets/modes and
`EINVAL|ENAMETOOLONG|ERROR_INVALID_NAME|ERROR_INVALID_PARAMETER` map to
`FileInvalidArgument`; a platform-unimplemented operation maps to
`FileUnsupported`; every other nonzero code becomes `FileSystemError code`.
`EINTR` is retried internally. EOF and a short non-exact read are successful
states; `readExactBytes` loops until its requested count and raises
`FileUnexpectedEof` if EOF arrives first, while `readExact` only adds strict
UTF-8 `ConvertError` after that exact byte read. Cancellation remains a
`Cancelled` control outcome, and descriptor,
OOM, overlap, or ABI faults remain false plus reserved `AbiFailure`; none is
encoded as `FileError`.

The public File surface is frozen here. Checked-direct wrappers have these
types: `openFile : String -> FileMode -> Result FileHandle FileError`,
`closeFileHandle : FileHandle -Consume-> Result Unit FileError`,
`exists : String -> Result Bool FileError`,
`flush : FileHandle -Borrow-> Result Unit FileError`,
`rawListDir : String -> Result (Seq ByteArray) FileError` with public
`listDir : String -> Result (Seq String) (FileError | ConvertError)`,
`remove : String -> Result Unit FileError`,
`seek : FileHandle -Borrow-> Int -> Whence -> Result Int FileError`,
`size : String -> Result Int FileError`,
`tell : FileHandle -Borrow-> Result Int FileError`,
`truncate : FileHandle -Borrow-> Int -> Result Unit FileError`,
`appendFile : String -> String -> Result Unit FileError`, and
`writeFileBytes : String -> ByteArray -Borrow-> Result Unit FileError`.
All twelve underlying checked leaves are `ThreadPool`; these public wrappers
transparently await them, preserve their data-valued Result, and add `Cancel`
to the function effect row. Cancellation never becomes a FileError.
The iterator signatures are the two above. Public
`readChunks : FileHandle -Consume-> Int -> Iterator (Result ByteArray FileError)`
is the ordinary identity wrapper over private `rawReadChunks`; it preserves
the moved-handle ownership and the iterator's Err-then-None termination.
The six source Outcome leaves have the
exact source contracts
`rawWrite : String -> String -> Unit ! {Raise FileError, Cancel}`,
`rawReadByte : String -> ByteArray ! {Raise FileError, Cancel}`,
`rawReadDescriptorBytes : FileHandle -Borrow-> Int -> ByteArray ! {Raise FileError, Cancel}`,
`rawWriteDescriptorBytes : FileHandle -Borrow-> ByteArray -Consume-> Int ! {Raise FileError, Cancel}`,
`rawWriteDescriptorString : FileHandle -Borrow-> String -> Int ! {Raise FileError, Cancel}`, and
`rawWriteDescriptorStrings : FileHandle -Borrow-> String -> String -> Int ! {Raise FileError, Cancel}`;
each write result is bytes transferred. Public `readFile`, `writeFile`, `readFileBytes`, `readBytes`,
`writeBytes`, `readExactBytes`, and `readExact` are Yona definitions over those
leaves. Their respective result types are String, Unit, ByteArray, ByteArray,
Int, ByteArray, and String. Every byte-producing native leaf remains ByteArray:
`readFile` wraps `rawReadByte`, and `readFile`, `readExact`, and public
`listDir` add `Raise ConvertError` through strict Yona decoding. Because an
opaque Iterator has a deliberately closed producer set, public
`readLines : String -> Stream (Result String (FileError | ConvertError))`
passes `rawReadLines path` through `Std\Stream.fromIterator` and the ordinary
Yona `Std\Stream.map`, strictly decoding each Ok ByteArray lazily without
buffering or changing the exactly-one Err/then-Nil rule. The Stream closure
owns the single-pass iterator; dropping it releases the file iterator through
normal captured-resource cleanup. No native text decoder or hidden Iterator
constructor is added. The remaining asynchronous wrappers carry
`{Raise FileError, Cancel}`. Raw C never constructs a String from filesystem
bytes.
Descriptor reads/writes serialize on the FileHandle's internal lock, capture
the current logical offset into a private backend request, and advance it by
the actual transferred byte count at terminal completion. `seek` and `tell`
use the same lock, so no pure-Yona `tell`/reservation race exists. No source or
public platform submission exposes an offset; future positioned I/O requires
separate explicit `readAt`/`writeAt` contracts rather than overloading this
cursor.
V2/source tests compare every wrapper and leaf FunctionType exactly.

Here and in the tables below, `! {Raise FileError, Cancel}` is normative
canonical display shorthand, not new general Yona effect-declaration syntax:
the structural EffectRow has `MayRaise=true` and `MayCancel=true`, while the
leaf's checked Outcome result contract fixes `FileError` as its Raised payload.
The compiler-owned source uses the existing privileged Outcome/async leaf
declaration form plus its ordinary value signature, and the manifest route
validates those inferred facts. Promise's `! <effect-row>` suffix is the one
new annotation grammar introduced by this task and names the latent structural
row; it does not make File's display shorthand a source literal.

`Std\Io` contains ordinary closed source enums `InputStream = Stdin` and
`OutputStream = Stdout | Stderr`; neither carries an fd and neither is a
resource. It also publicly declares, in exact tag order,
`IoError = IoClosed | IoBrokenPipe | IoUnexpectedEof | IoInvalidCount |
IoUnsupported | IoSystemError Int`. Only the last constructor has a Trivial
positive native-code payload. `EBADF|ERROR_INVALID_HANDLE` maps to IoClosed;
`EPIPE|ERROR_BROKEN_PIPE|ERROR_NO_DATA` to IoBrokenPipe; a premature exact-read
EOF to IoUnexpectedEof; negative/overflow counts and
`EINVAL|ERROR_INVALID_PARAMETER` to IoInvalidCount; an unavailable operation
to IoUnsupported; and every other nonzero code to IoSystemError. EINTR is
retried. Ordinary EOF from line/chunk reads is a successful `None`.

The eight exact leaf signatures are
`rawWriteString : OutputStream -> String -> Result Int IoError`,
`rawWriteLine : OutputStream -> String -> Result Int IoError`,
`rawWriteBytes : OutputStream -> ByteArray -Borrow-> Result Int IoError`,
`rawReadLine : InputStream -> Result (Option ByteArray) IoError`,
`rawReadChunk : InputStream -> Int -> Result (Option ByteArray) IoError`,
`rawReadExactBytes : InputStream -> Int -> Result ByteArray IoError`,
`rawIsTty : (InputStream | OutputStream) -> Bool`, and
`rawFlush : OutputStream -> Result Unit IoError`. This deliberately replaces
the misleading private `rawReadExact` spelling with `rawReadChunk`; the manifest
contains neither spelling as an alias. Every leaf except rawIsTty is
`ThreadPool`; rawIsTty is Synchronous. The public export inventory is closed at
exactly these 15 functions (the four print conveniences select Stdout/Stderr):

```text
print : String -> Unit ! {Raise IoError, Cancel}
println : String -> Unit ! {Raise IoError, Cancel}
eprint : String -> Unit ! {Raise IoError, Cancel}
eprintln : String -> Unit ! {Raise IoError, Cancel}
writeString : OutputStream -> String -> Int ! {Raise IoError, Cancel}
writeLine : OutputStream -> String -> Int ! {Raise IoError, Cancel}
writeBytes : OutputStream -> ByteArray -Borrow-> Int ! {Raise IoError, Cancel}
readLine : InputStream -> Option String ! {Raise IoError, Raise ConvertError, Cancel}
readChunk : InputStream -> Int -> Option ByteArray ! {Raise IoError, Cancel}
readExactBytes : InputStream -> Int -> ByteArray ! {Raise IoError, Cancel}
readExact : InputStream -> Int -> String ! {Raise IoError, Raise ConvertError, Cancel}
readStdinBytes : Unit -> ByteArray ! {Raise IoError, Cancel}
readStdin : Unit -> String ! {Raise IoError, Raise ConvertError, Cancel}
flush : OutputStream -> Unit ! {Raise IoError, Cancel}
isTty : (InputStream | OutputStream) -> Bool
```

Each high-level wrapper unwraps its private data-valued Result and raises the
typed error. `readLine` and `readExact` strictly decode in Yona;
`readStdinBytes` loops over `rawReadChunk Stdin` and joins chunks once, while
`readStdin` strictly decodes that result. Raw C never constructs a String from
standard-stream bytes. There are no public `stdinFd`/`stdoutFd`/`stderrFd`,
`putStr`, `putStrLn`, `write`, or `readLineFrom` aliases, no optional extra byte
variant, and no integer descriptor at the source/runtime boundary. The
manifest/source/interface/doc tests require this exact 15-name set and exact
FunctionTypes.

`Std\Net` similarly owns and exports the only `Socket` identity and its exact
closed error family:

```yona
resource Socket
type NetError = NetHostNotFound | NetConnectionRefused | NetConnectionReset
  | NetTimedOut | NetAddressInUse | NetAddressNotAvailable
  | NetNetworkUnreachable | NetClosed | NetInvalidArgument | NetUnsupported
  | NetNameResolutionError Int | NetSystemError Int
```

Tags are `0..11`; only the last two constructors carry one Trivial positive
platform code. POSIX/WinSock maps `ECONNREFUSED|WSAECONNREFUSED`,
`ECONNRESET|WSAECONNRESET`, `ETIMEDOUT|WSAETIMEDOUT`,
`EADDRINUSE|WSAEADDRINUSE`, `EADDRNOTAVAIL|WSAEADDRNOTAVAIL`,
`ENETUNREACH|WSAENETUNREACH`, and `EBADF|ENOTSOCK|WSAENOTSOCK` to the
matching constructors; source-invalid hosts/ports/counts and
`EINVAL|WSAEINVAL` map to NetInvalidArgument. `getaddrinfo` no-name/noname
maps to NetHostNotFound and its other nonzero results to
`NetNameResolutionError abs(code)`; an unavailable platform operation maps to
NetUnsupported; all other native failures map to NetSystemError. EINTR is
retried, stream EOF is successful empty data, cancellation is a Cancelled
outcome, and ABI/descriptor/OOM failures never become NetError.

The four checked leaves are exactly
`rawClose : Socket -Consume-> Result Unit NetError`,
`rawPeerAddress : Socket -Borrow-> Result String NetError`,
`rawTcpListen : String -> Int -> Result Socket NetError`,
and `rawUdpBind : String -> Int -> Result Socket NetError`; all four are
Synchronous. The seven Outcome leaves are
`rawRecvBytes : Socket -Borrow-> Int -> ByteArray ! {Raise NetError, Cancel}`,
`rawSend : Socket -Borrow-> String -> Int ! {Raise NetError, Cancel}`,
`rawSendBytes : Socket -Borrow-> ByteArray -Consume-> Int ! {Raise NetError, Cancel}`,
`rawTcpAccept : Socket -Borrow-> Socket ! {Raise NetError, Cancel}`,
`rawTcpConnect : String -> Int -> Socket ! {Raise NetError, Cancel}`,
`rawUdpRecvBytes : Socket -Borrow-> Int -> ByteArray ! {Raise NetError, Cancel}`,
and `rawUdpSendToBytes : Socket -Borrow-> String -> Int -> ByteArray -Consume-> Int ! {Raise NetError, Cancel}`. As with
File, that notation is canonical semantic display; the checked Outcome route
supplies MayRaise/MayCancel and exact Raised payload. Public `close`,
`peerAddress`, `tcpListen`, and `udpBind` preserve their checked Result types.
Public `recvBytes`, `send`, `sendBytes`, `tcpAccept`, `tcpConnect`,
`udpRecvBytes`, and `udpSendToBytes` preserve the effectful result types.
Public `recv` and `udpRecv` are strict Yona UTF-8 decoders over their byte
counterparts and additionally admit `Raise ConvertError`; public `udpSendTo`
encodes its String payload and delegates to the byte leaf. No native receive
path constructs a String from arbitrary network bytes.
Accepted/connected/listening/bound sockets are Owned;
Borrowed operations acquire Task 14's private pin before commit; close consumes
one source owner and arbitrates with pins. `Std\Http` is rewritten against
these contracts using `with` for connected/listener/client resources and
propagating Net/Convert effects—never `Linear fd`, integer sockets, or a manual
close after transfer. Net/Http source, v2, bootstrap-SCC, wrong-resource,
error-map, close-race, and ownership tests freeze this surface.

`Std\Process` owns and exports `resource ProcessHandle` plus
`ProcessError = ProcessNotFound | ProcessPermissionDenied |
ProcessAlreadyExited | ProcessPipeClosed | ProcessInvalidArgument |
ProcessUnsupported | ProcessSystemError Int` in exact tag order `0..6`; only
the last has a Trivial positive native-code payload. Platform spawn/exec/wait/
pipe errors map not-found, access, no-child/already-reaped, broken-pipe/closed,
and invalid-argument codes to the named constructors, retry EINTR, use
ProcessUnsupported for unavailable operations, and otherwise preserve the
native code in ProcessSystemError. A child's nonzero exit status is successful
data, not ProcessError. The private leaf schemes are fixed:

```text
rawCloseStdin : ProcessHandle -Borrow-> Result Unit ProcessError
rawExec : String -> Seq String -Borrow-> Result ByteArray ProcessError
rawExecArgs : String -> Seq String -Borrow-> Result Unit ProcessError
rawExecStatus : String -> Seq String -Borrow-> Result Int ProcessError
rawExecutablePath : () -> Result ByteArray ProcessError
rawExit : Int -> Unit                         # successful call is noreturn
rawGetArgs : () -> Result (Seq ByteArray) ProcessError
rawGetcwd : () -> Result ByteArray ProcessError
rawGetenv : String -> Result (Option ByteArray) ProcessError
rawHostname : () -> Result ByteArray ProcessError
rawKill : ProcessHandle -Borrow-> Int -> Result Unit ProcessError
rawPid : ProcessHandle -Borrow-> Result Int ProcessError
rawReadAll : ProcessHandle -Borrow-> Result ByteArray ProcessError
rawReadLine : ProcessHandle -Borrow-> Result (Option ByteArray) ProcessError
rawRun : String -> Seq String -Borrow-> Result Int ProcessError
rawRunWithArgv0 : String -> String -> Seq String -Borrow-> Result Int ProcessError
rawSetenv : String -> String -> Result Unit ProcessError
rawSpawn : String -> Seq String -Borrow-> Result ProcessHandle ProcessError
rawTempDir : () -> Result ByteArray ProcessError
rawTempFile : String -> String -> Result ByteArray ProcessError
rawWait : ProcessHandle -Borrow-> Result Int ProcessError
rawWriteStdin : ProcessHandle -Borrow-> ByteArray -Consume-> Result Int ProcessError
rawYonaVersion : () -> String
```

The nine blocking rows named in the manifest remain ThreadPool; all others are
Synchronous. The public inventory is closed at exactly these 34 functions;
there are no implicit aliases or optional byte variants:

```text
closeStdin : ProcessHandle -Borrow-> Result Unit ProcessError
execBytes : String -> Seq String -Borrow-> Result ByteArray ProcessError ! {Cancel}
exec : String -> Seq String -Borrow-> Result String (ProcessError | ConvertError) ! {Cancel}
execArgs : String -> Seq String -Borrow-> Result Unit ProcessError ! {Cancel}
execStatus : String -> Seq String -Borrow-> Result Int ProcessError ! {Cancel}
executablePathBytes : Unit -> Result ByteArray ProcessError
executablePath : Unit -> Result String (ProcessError | ConvertError)
exit : Int -> Unit
getArgsBytes : Unit -> Result (Seq ByteArray) ProcessError
getArgs : Unit -> Result (Seq String) (ProcessError | ConvertError)
getcwdBytes : Unit -> Result ByteArray ProcessError
getcwd : Unit -> Result String (ProcessError | ConvertError)
getenvBytes : String -> Result (Option ByteArray) ProcessError
getenv : String -> Result (Option String) (ProcessError | ConvertError)
hostnameBytes : Unit -> Result ByteArray ProcessError
hostname : Unit -> Result String (ProcessError | ConvertError)
kill : ProcessHandle -Borrow-> Int -> Result Unit ProcessError
pid : ProcessHandle -Borrow-> Result Int ProcessError
readAllBytes : ProcessHandle -Borrow-> Result ByteArray ProcessError ! {Cancel}
readAll : ProcessHandle -Borrow-> Result String (ProcessError | ConvertError) ! {Cancel}
readLineBytes : ProcessHandle -Borrow-> Result (Option ByteArray) ProcessError ! {Cancel}
readLine : ProcessHandle -Borrow-> Result (Option String) (ProcessError | ConvertError) ! {Cancel}
run : String -> Seq String -Borrow-> Result Int ProcessError ! {Cancel}
runWithArgv0 : String -> String -> Seq String -Borrow-> Result Int ProcessError ! {Cancel}
setenv : String -> String -> Result Unit ProcessError
spawn : String -> Seq String -Borrow-> Result ProcessHandle ProcessError
tempDirBytes : Unit -> Result ByteArray ProcessError
tempDir : Unit -> Result String (ProcessError | ConvertError)
tempFileBytes : String -> String -> Result ByteArray ProcessError
tempFile : String -> String -> Result String (ProcessError | ConvertError)
wait : ProcessHandle -Borrow-> Result Int ProcessError ! {Cancel}
writeStdinBytes : ProcessHandle -Borrow-> ByteArray -Consume-> Result Int ProcessError ! {Cancel}
writeStdin : ProcessHandle -Borrow-> String -> Result Int ProcessError ! {Cancel}
yonaVersion : Unit -> String
```

Every wrapper over a ThreadPool leaf transparently awaits its Promise and adds
only the `Cancel` effect; `ProcessError` and `ConvertError` remain explicit
Result data. Text wrappers strictly decode the correspondingly named byte
wrapper in Yona, `writeStdin` encodes a known-valid String then delegates to
`writeStdinBytes`, and `rawYonaVersion` is the only String-producing leaf
because its literal compiler version is known-valid UTF-8. The
manifest/source/interface/doc tests require this exact 34-name set and exact
FunctionTypes.
There is deliberately no separate process-handle close:
balanced descriptor release retires its shared state after in-flight pins;
`closeStdin` closes only the pipe, `kill` requests termination, and `wait`
observes/reaps the child. Runtime and interface tests cover retained aliases,
concurrent wait/read, last-release-before-worker-completion, double wait,
nonzero status, closed pipe, and exact error mapping. Encoding regressions
cover invalid POSIX path/environment/argument bytes and unpaired Win32 UTF-16
input: byte APIs preserve the former exactly, public text wrappers return
`ConvertError`, and neither platform constructs an invalid Yona String.

`Std\Task.rawCheckCancellation` is the private
`CancellationCheck=15` CompilerPlan intrinsic with the exact closed contract
`Unit -> Unit ! {Cancel}`. It has no FunctionId or native symbol and is legal
only as the direct full application in public
`checkCancellation unit = rawCheckCancellation unit`; Task 12's authenticated
semantic fact lowers it to `CancellationPointInst`. The manifest, interface,
documentation, and source tests require that public signature and reject any
first-class/private-intrinsic escape.

`Std\Task.rawSpawn` is the other private `TaskSpawn=16` semantic intrinsic. Its
generic source contract returns the explicit internal source type
`(() -> a ! e) -> Promise a ! e`; the Promise record stores
`resultOwnershipFor(a)` and the async lift of `e` (including Cancel). The
projected fact stores the exact callable, result, result ownership, and work
row. AST lowering produces a Promise-typed `TaskSpawnInst`; Task 14's
`runAsyncPlanning` alone turns it into `SubmitOutcomeTask`, and the ordinary
transparent-demand machinery inserts the single consuming await. The public
wrapper has the same Promise result, so returning it does not await inside
`spawn`; coercion to `a` happens only at a caller demand. It never resolves to a C symbol or direct native
entry. Identifier, partial, first-class, export, imported, or wrong-arity use
of `rawSpawn` is rejected; public `spawn action = rawSpawn action` is the only
wrapper. The canonical formatter may display the Promise's latent work row,
but it never erases the structural Promise in v2. Tests cover zero-argument callable submission, Trivial/Owned result,
Raise/Perform/Cancel propagation, unused Promise cleanup, and exactly-once
transparent await.

The final public `Std\Task` inventory is exactly two functions:
`spawn : (() -> a ! e) -> Promise a ! e` and
`checkCancellation : Unit -> Unit ! {Cancel}`. Manifest/interface/source/API
tests freeze both names and signatures; neither private raw intrinsic is
exported.

`Promise` is consequently a real structural source/inference type rather than
an erased checker implementation detail. Add reserved annotation syntax
`Promise <type-atom> ! <effect-row>` at type-application precedence;
non-atomic element types require parentheses, `!` belongs to the Promise
constructor, and `A -> Promise B ! e` therefore has the Promise as its arrow
result. Ordinary user annotations/imports may name and return Promise values,
but only compiler intrinsics construct or complete them. The inference model
adds `MonoType::Promise{Element, LatentEffects}` and carries it through
unification, occurs checks, substitution, generalization/instantiation,
canonical structural projection/remap, formatting, and v2 round-trip without
equating it with `Element`. Task 14's existing
`PromiseDemandProjection`/`ProjectedPromiseDemand` fact records each permitted
transparent Promise-to-element demand (including rawSpawn and async extern
applications); AST lowering consumes that
fact to emit the await, and neither unification nor import silently erases the
Promise. Parser precedence, nested Promise, row-polymorphic, imported/exported,
unused, and illegal direct-construction tests freeze the contract.

Every source leaf scheme is fully binder-owned and well-scoped: it has no free
type, row, or effect binder. StableExternal and monomorphic rows are closed at
projection time; generic Task, Channel, Iterator, Set, and Dict schemes remain
open only through their own declared binders. Every executable leaf
instantiation and emitted runtime descriptor is structurally closed after
specialization. The verifier tests both stages and rejects a free source binder
or an open runtime descriptor. In particular, this task replaces pointer-as-Int
and unconstrained `Linear` signatures with explicit
opaque nominal resources: `FileHandle`, `Socket`, `ProcessHandle`, `Regex`,
`Buffer`, and `PinnedFloats`. Each has one generated Resource descriptor,
exact Shareable/linear policy, and typed close/finalizer. `Std\Net` accepts and
returns `Socket`; `Std\Process` accepts and returns `ProcessHandle`; `Std\Io`
uses a closed `OutputStream` nominal instead of file-descriptor integers; and
Regex operations Borrow the compiled `Regex` value. The manifest records
those exact nominal identities and ownerships. No `Int` carrier may be
reinterpreted as a pointer, fd, task, socket, regex, process, or GPU handle.
Source/API/interface tests exercise the deliberate type changes and prove a
resource from one family cannot satisfy another's descriptor even under a
forced fingerprint collision.

The Prelude cutover is explicit and occurs before source digests are frozen.
Delete `Linear a` and its constructor/export completely; ownership is a
property of structural ResourceType values and parameter/result contracts, not
a source wrapper. Delete Prelude's `FileHandle`, `FileMode`, and `Whence`
declarations/exports; their only identities are the declarations above in
`Std\File`, and consumers import them from that module. Replace Prelude's
nominal `Iterator a = Iterator ...` with compiler-authorized
`resource Iterator a` and keep `export type Iterator`, so the type remains
available without an import but has no constructor/pattern representation.
Only the closed seven-operation set may traffic in this opaque value: Task
11's four Iterator factories and `next`, plus Task 15's two File factories;
exactly the six factories may publish an Owned Iterator. Remove the parser/typechecker's hard-coded Prelude File/Linear
constructor admissions and treat Iterator exactly like any other linear
ResourceType. Migrate all canonical
stdlib uses, notably `Std\Channel`, `Std\Gpu`, `Std\Http`, `Std\Io`, and
`Std\Stream`, to direct resource ownership and explicit File imports. Tests
prove Prelude has no `Linear`, `FileHandle`, `FileMode`, or `Whence` export,
there is exactly one `Std\File.FileHandle` key, channel endpoints need no
unwrap, and Iterator construction/pattern matching is rejected outside its
compiler intrinsic lowering.

The resource policy is closed:

| Nominal resource | Shareability | Descriptor release/drop entry | Legal ABI ownership |
|---|---|---|---|
| `Prelude.Iterator a` | linear | `YonaRuntimeAbiIteratorReleaseV2(YonaAbiWord)` | only verified Iterator/File intrinsics create Owned values; next takes one scoped Borrow; no retain/copy; exhaustion or owner drop releases source/state exactly once |
| `Std\File.FileHandle` | linear | `YonaRuntimeAbiFileHandleReleaseV2(YonaAbiWord)` | created Owned; queries and async submissions Borrow with a runtime-private pin; close Consume |
| `Std\Net.Socket` | linear | `YonaRuntimeAbiSocketReleaseV2(YonaAbiWord)` | created Owned; address and async send/receive/accept submissions Borrow with a runtime-private pin; close Consume |
| `Std\Process.ProcessHandle` | `ALWAYS_SHAREABLE` synchronized | `YonaRuntimeAbiProcessHandleTryRetainV2` / `YonaRuntimeAbiProcessHandleReleaseV2` | spawn returns Owned; read/write/wait/pid/status operations Borrow and ThreadPool staging retains; last descriptor release retires the handle state after in-flight pins, while process termination and stdin close remain explicit operations |
| `Std\Regex.Regex` | `ALWAYS_SHAREABLE` | `YonaRuntimeAbiRegexTryRetainV2` / `YonaRuntimeAbiRegexReleaseV2` | compile returns Owned; all matching operations Borrow |
| `Std\Gpu.Buffer` | `ALWAYS_SHAREABLE` immutable | `YonaRuntimeAbiGpuBufferTryRetainV2` / `YonaRuntimeAbiGpuBufferReleaseV2` | constructors/results Owned; kernels/materialize Borrow |
| `Std\Gpu.PinnedFloats` | linear mutable | `YonaRuntimeAbiPinnedFloatsReleaseV2(YonaAbiWord)` | allocation returns Owned; reads Borrow; set/copy/map/close Consume and either republish one owner or destroy it |
| `Std\Channel.Sender a` | `ALWAYS_SHAREABLE`, ABI `ChannelSender` | `YonaRuntimeAbiChannelEndpointTryRetainV2` / `YonaRuntimeAbiChannelEndpointReleaseV2` | create returns Owned; send/close Borrow so independent retained producers may share one queue |
| `Std\Channel.Receiver a` | `ALWAYS_SHAREABLE`, ABI `ChannelReceiver` | `YonaRuntimeAbiChannelEndpointTryRetainV2` / `YonaRuntimeAbiChannelEndpointReleaseV2` | create returns Owned; receive/tryReceive/close Borrow so independent consumers may share one queue |

`Runtime/Stdlib/Channel.{h,c}` owns the endpoint retain/release callbacks and
the four CheckedDirect V2 leaves `rawClose`, `rawClosed`, `rawLength`, and
`rawCapacity`; Task 14 continues to own create/send/receive/tryReceive. Close
accepts the closed sum `Sender a | Receiver a` by Borrow and atomically closes
the shared queue without consuming that endpoint owner. Closed/length/
capacity likewise Borrow either role and return Trivial values. All four
validate exact resource kind, role, element arguments, and queue identity via
the generated descriptor before touching state. Channel resource ABI tests
cover both roles, retained aliases, close idempotence, wrong-role/element/
family descriptors, and balanced release of both endpoint wrappers plus the
shared queue core.

`Std\Channel` declares `type ChannelError = ChannelInvalidCapacity |
ChannelClosed` in exact tag order `0..1`; neither constructor has a payload.
Its exact generic contracts are:

```text
rawCreate : Int -> (Sender a, Receiver a) ! {Raise ChannelError}
rawSend : Sender a -Borrow-> a -Consume-> Unit ! {Raise ChannelError, Cancel}
rawReceive : Receiver a -Borrow-> Option a ! {Cancel}
rawTryReceive : Receiver a -Borrow-> Option a
rawClose : (Sender a | Receiver a) -Borrow-> Unit
rawClosed : (Sender a | Receiver a) -Borrow-> Bool
rawLength : (Sender a | Receiver a) -Borrow-> Int
rawCapacity : (Sender a | Receiver a) -Borrow-> Int
```

Create raises only `ChannelInvalidCapacity` for a negative capacity. Send
raises only `ChannelClosed` when the queue is closed; on every true-return
path it has consumed its payload. Receive and tryReceive never raise:
closed-and-drained is `None`, and tryReceive also returns `None` for a
currently empty open queue. Close is idempotent and leaves the borrowed
endpoint owner valid but closed. Public wrappers preserve these rows exactly,
and source/v2/runtime tests freeze both constructor tags, the false-unchanged
versus true-consumed send poststates, closed-and-drained behavior, and the
absence of invented Raise edges on either receive operation.

Every release/drop entry is infallible, nonallocating, and has the descriptor
callback ABI (`void(YonaAbiWord)`; TryRetain is `bool(YonaAbiWord)`). A
source-visible close that can fail is a separate checked leaf returning an
explicit `Result`; `ReleaseResource` cleanup invokes only the infallible
descriptor drop, while `InvokeFinalizer` remains reserved for a real resolved
function-typed callable. The
manifest contract checks these exact identities, flags, callback signatures,
and ownership positions rather than accepting an “opaque resource” category.
These nine rows are the complete `[[module.resource]]` inventory; the generator
requires exactly nine and rejects any tenth. Sender/Receiver project to
distinct parameterized ResourceTypes whose `AbiKind` selects the CHANNEL
descriptor representation. `Iterator a` is the Prelude-owned linear
GenericResource around Task 11's exact opaque state ABI, with one binder, absent
TryRetain, and the exact iterator release callback. Its source declaration is
`resource Iterator a` plus `export type Iterator`; therefore it has no
constructor or pattern form at all. `Std\Iterator`'s verified intrinsic calls
return that opaque Prelude identity, but no source in any module constructs it.
It is distinct from the compiler-internal `CursorType` used by generator
lowering. `Std\Stream.fromIterator` calls the public checked
`Std\Iterator.next` definition instead of pattern matching its representation.
Tests reject user construction/pattern matching and prove a runtime-created
iterator remains single-pass and releases its state/source once. The
manifest/conformance tests assert Iterator is exactly one of the nine
ResourceDeclaration rows, assert the seven-operation/six-producer set
exactly, and require every non-resource built-in or nominal family to be
absent.

The retained source-visible GPU CheckedDirect inventory is also closed; every
row has an empty source effect row, uses Task 7's single C prototype, and maps
to the explicitly shown symbol:

`Std\Gpu` declares and exports the closed error family `GpuError =
GpuUnavailable | GpuUnsupported | GpuDeviceLost | GpuOutOfMemory |
GpuSubmissionFailed | GpuSystemError Int` in exact tag order `0..5`; only
`GpuSystemError` has a Trivial positive native-code payload. A missing usable
backend/device maps to `GpuUnavailable`; a device/capability or operation that
cannot implement the request maps to `GpuUnsupported`; Vulkan/driver device
loss maps to `GpuDeviceLost`; post-commit host/device allocation exhaustion
maps to `GpuOutOfMemory`; queue, command-buffer, fence, or submission failure
with a live device maps to `GpuSubmissionFailed`; and every other nonzero
backend code maps to `GpuSystemError abs(code)`. Precommit descriptor,
ownership, overlap, or infrastructure allocation faults remain the reserved
false `AbiFailure` path and are never source `GpuError` values.

The two source Outcome leaves and public wrappers have the same exact
contracts:

```text
rawFloatArrayMul2Async : FloatArray -Consume-> FloatArray ! {Raise GpuError, Cancel}
rawFloatArrayScaleAsync : Float -> FloatArray -Consume-> FloatArray ! {Raise GpuError, Cancel}
floatArrayMul2Async : FloatArray -Consume-> FloatArray ! {Raise GpuError, Cancel}
floatArrayScaleAsync : Float -> FloatArray -Consume-> FloatArray ! {Raise GpuError, Cancel}
```

The manifest builds each `YonaAsyncResultDescriptor` from exactly the closed
`FloatArray` Success type, Owned result, and `{Raise GpuError, Cancel}` row;
stub and Vulkan implementations construct only those six declared Raised
values after commit. Source/v2/conformance tests freeze constructor tags,
payloads, parameter order/ownership, wrapper equality, backend mappings, and
the fact that Success returns the mutated FloatArray rather than an integer
status.

```text
rawBackendName : () -> String -> YonaRuntimeAbiGpuBackendNameV2
rawVulkanStatus : () -> String -> YonaRuntimeAbiGpuVulkanStatusV2
rawVulkanLastNote : () -> String -> YonaRuntimeAbiGpuVulkanLastNoteV2
rawHasGpu : () -> Bool -> YonaRuntimeAbiGpuHasGpuV2
rawHasSimd : () -> Bool -> YonaRuntimeAbiGpuHasSimdV2
rawVulkanAvailable : () -> Bool -> YonaRuntimeAbiGpuVulkanAvailableV2
rawVulkanTimelineSemaphore : () -> Bool -> YonaRuntimeAbiGpuVulkanTimelineSemaphoreV2
rawVulkanLastIssueKind : () -> Int -> YonaRuntimeAbiGpuVulkanLastIssueKindV2
rawAvailable : () -> Bool -> YonaRuntimeAbiGpuAvailableV2
rawPhysicalDeviceCount : () -> Int -> YonaRuntimeAbiGpuPhysicalDeviceCountV2
rawUpload : IntArray -Borrow-> Buffer -> YonaRuntimeAbiGpuUploadV2
rawMaterialize : Buffer -Borrow-> IntArray -> YonaRuntimeAbiGpuMaterializeV2
rawLength : Buffer -Borrow-> Int -> YonaRuntimeAbiGpuLengthV2
rawMapAdd : Int -> Buffer -Borrow-> Buffer -> YonaRuntimeAbiGpuMapAddV2
rawMapMul : Int -> Buffer -Borrow-> Buffer -> YonaRuntimeAbiGpuMapMulV2
rawMapSquare : Buffer -Borrow-> Buffer -> YonaRuntimeAbiGpuMapSquareV2
rawFilterGreaterThan : Int -> Buffer -Borrow-> Buffer -> YonaRuntimeAbiGpuFilterGreaterThanV2
rawFilterLessThan : Int -> Buffer -Borrow-> Buffer -> YonaRuntimeAbiGpuFilterLessThanV2
rawReduceSum : Buffer -Borrow-> Int -> YonaRuntimeAbiGpuReduceSumV2
rawMapReduceGraph : Seq IntMapOp -Borrow-> Buffer -Borrow-> Int -> YonaRuntimeAbiGpuMapReduceGraphV2
rawMapFloat : FloatMapOp -> FloatArray -Borrow-> FloatArray -> YonaRuntimeAbiGpuMapFloatV2
rawReduceFloat : FloatReduceOp -> FloatArray -Borrow-> Float -> YonaRuntimeAbiGpuReduceFloatV2
rawAllocPinnedFloats : Int -> PinnedFloats -> YonaRuntimeAbiGpuAllocPinnedFloatsV2
rawClosePinnedFloats : PinnedFloats -Consume-> Unit -> YonaRuntimeAbiGpuClosePinnedFloatsV2
rawPinnedLength : PinnedFloats -Borrow-> Int -> YonaRuntimeAbiGpuPinnedLengthV2
rawPinnedGet : PinnedFloats -Borrow-> Int -> Float -> YonaRuntimeAbiGpuPinnedGetV2
rawPinnedSet : PinnedFloats -Consume-> Int -> Float -> PinnedFloats -> YonaRuntimeAbiGpuPinnedSetV2
rawPinnedToFloatArray : PinnedFloats -Borrow-> FloatArray -> YonaRuntimeAbiGpuPinnedToFloatArrayV2
rawCopyFloatArrayToPinned : FloatArray -Borrow-> PinnedFloats -Consume-> PinnedFloats -> YonaRuntimeAbiGpuCopyFloatArrayToPinnedV2
rawPinnedBackend : PinnedFloats -Borrow-> String -> YonaRuntimeAbiGpuPinnedBackendV2
rawMapFloatPinned : FloatMapOp -> PinnedFloats -Consume-> PinnedFloats -> YonaRuntimeAbiGpuMapFloatPinnedV2
```

Here `-Borrow->`/`-Consume->` annotate the immediately preceding parameter;
all other scalar/nominal-enum parameters are Trivial. String/array/buffer/
pinned results are Owned; Unit/Bool/Int/Float results are Trivial. Task 14's
two typed FloatArray Outcome rows are additional source leaves; its two
raw-pointer Vulkan Outcome rows are runtime-internal beneath those wrappers.
All four use their exact Promise/result contracts, while Task 16's transparent kernels are absent from this source
list. Digest-pinned source plus generated v2 supply the complete structural
encodings; TOML supplies the exact route/symbol mapping. The test joins both
and compares exact FunctionTypes and C symbols rather than parsing this
display shorthand or maintaining a second type table.

Task 14 keeps its 38-name `REQUIRED_OUTCOME_APIS` runtime inventory, but only
this exact 19-row `SOURCE_OUTCOME_LEAF_MAPPINGS` subset is source-callable:

```text
ChannelCreate: Std\Channel.rawCreate -> YonaRuntimeChannelCreateOutcome
ChannelSend: Std\Channel.rawSend -> YonaRuntimeChannelSendOutcomeMove
ChannelReceive: Std\Channel.rawReceive -> YonaRuntimeChannelReceiveOutcome
ChannelTryReceive: Std\Channel.rawTryReceive -> YonaRuntimeChannelTryReceiveOutcome
FileWrite: Std\File.rawWrite -> YonaRuntimePlatformSubmitFileWriteOutcome
FileReadBytes: Std\File.rawReadByte -> YonaRuntimePlatformSubmitFileByteReadOutcome
FileDescriptorReadBytes: Std\File.rawReadDescriptorBytes -> YonaRuntimePlatformSubmitFileDescriptorByteReadOutcome
FileDescriptorWriteBytes: Std\File.rawWriteDescriptorBytes -> YonaRuntimePlatformSubmitFileDescriptorByteWriteOutcomeMove
FileDescriptorWriteString: Std\File.rawWriteDescriptorString -> YonaRuntimePlatformSubmitFileDescriptorStringWriteOutcome
FileDescriptorWriteStrings: Std\File.rawWriteDescriptorStrings -> YonaRuntimePlatformSubmitFileDescriptorStringsWriteOutcome
NetRecvBytes: Std\Net.rawRecvBytes -> YonaRuntimePlatformSubmitNetRecvBytesOutcome
NetSendString: Std\Net.rawSend -> YonaRuntimePlatformSubmitNetSendOutcome
NetSendBytes: Std\Net.rawSendBytes -> YonaRuntimePlatformSubmitNetSendBytesOutcomeMove
NetTcpAccept: Std\Net.rawTcpAccept -> YonaRuntimePlatformSubmitTcpAcceptOutcome
NetTcpConnect: Std\Net.rawTcpConnect -> YonaRuntimePlatformSubmitTcpConnectOutcome
NetUdpRecvBytes: Std\Net.rawUdpRecvBytes -> YonaRuntimePlatformSubmitUdpRecvBytesOutcome
NetUdpSendToBytes: Std\Net.rawUdpSendToBytes -> YonaRuntimePlatformSubmitUdpSendToBytesOutcomeMove
GpuFloatArrayMul2: Std\Gpu.rawFloatArrayMul2Async -> YonaStdGpuFloatArrayMul2OutcomeAsync
GpuFloatArrayScale: Std\Gpu.rawFloatArrayScaleAsync -> YonaStdGpuFloatArrayScaleOutcomeAsync
```

Every non-intrinsic native manifest row spells `async_kind`; there is no parser
or compiler default. RuntimeEntry SemanticIntrinsic rows deliberately forbid
it. The 19 rows above are exactly `DedicatedOutcome`. The exact
`ThreadPool` set is the twelve CheckedDirect `Std\File` OS/resource leaves,
`Std\Time.rawSleepMicros`, the seven `Std\Io` leaves other than `rawIsTty`, and
these nine `Std\Process` leaves: `rawExec`, `rawExecArgs`, `rawExecStatus`,
`rawReadAll`, `rawReadLine`, `rawRun`, `rawRunWithArgv0`, `rawWait`, and
`rawWriteStdin`. Every other CheckedDirect or StableExternal row is explicitly
`Synchronous`; SemanticIntrinsic rows forbid `async_kind` because their
dedicated Typed IR contracts supply their execution semantics. The manifest
generator and conformance suite compare the exhaustive row set and reject an
omission, defaulted value, or changed classification. `DedicatedOutcome` is
backend-neutral: Linux may use io_uring, macOS kqueue, Windows IOCP, or a
portable fallback without changing interface bytes.

Every CheckedOutcomeV2 leaf must equal one row above and its C symbol must be a
member of `REQUIRED_OUTCOME_APIS`; the runtime inventory may additionally
contain compiler-internal IO-request, task/group, and low-level Vulkan
machinery. Those 19
internal names are explicitly forbidden as source declarations. The contract
compares the exact 19 mapping rows, then separately checks all 38 runtime
entries; it never equates the two sets. The static module table above is not
permission for an open-ended family. Explicit GPU Buffer/PinnedFloats capability/resource
operations remain source-visible only when they are converted to descriptor-
checked V2 entries in the checked-direct table; none may use a raw array pointer, integer
handle, legacy kernel symbol, or implicit error sentinel. This resolves Task
17's previous “port or remove” choice: preserve the documented public GPU
surface through typed resources and the two typed FloatArray Outcome wrappers.
The two `YonaRuntimeGpuVulkanFloat64Buffer*OutcomeAsync` functions are
runtime-internal implementations under those wrappers: no source type can
name their borrowed pointer/count view, and no manifest leaf may target them.
Remove only the redundant raw private kernels replaced by Tasks 14 and 16.

Finalize every live canonical source before Task 16 generation. Prelude drops
all ten legacy array externs: Seq size/index are pattern-recursive Yona, String
and typed-array instances live in their owning modules, and only the 19 rows
above remain. Convert, Json, Regex, and Utf16 expose public Yona wrappers over
their renamed private checked leaves. Constants/Platform uses only its eight
rows. Math is checked against the closed libm allowlist. Iterator declares its
five semantic intrinsics. Rewrite `Channel`, `Io`, `Task`, and `Gpu` here,
after compiler-only resource/manifest syntax exists, and validate all four
against the same manifest. Task 14 changes only their runtime/Typed IR
substrate; it never creates source that the then-current parser cannot admit.

`Std\Stream` is finalized here as well: allow blank header lines before
`module`, make `bracket` invoke its acquisition callback as `acquire ()`,
delete the obsolete Unit/arity workaround, and sequence both `release r` and
recursive `forEach` effects with `do ... end`. Its v2/API signature regression
requires `bracket : (() -> r) -> (r -> Unit) -> (r -> Stream a) -> Stream a`
and `allMatch : (a -> Bool) -> Stream a -> Bool`, with the canonical formatter's
explicit latent-effect notation. The two frozen oracle files are the only
legacy compiler inputs after these edits. Task 17 performs no canonical
`.yona` semantic migration; it only switches generated interfaces and deletes
obsolete C aliases/files.

Implement every final V2 leaf in this task, before Task 16 attempts native
validation. The per-module `Runtime/Stdlib/*.h/.c` files listed above contain
the Task 7 checked-direct wrappers and delegate to narrowly scoped platform or
codec helpers; Json/Regex add their descriptor-backed V2 entries beside the
frozen legacy exports. Task 11/14 entries remain owned by their existing
components. No replacement wrapper calls a legacy `YonaTypeDescriptor`, raw
closure, pointer-as-integer, sentinel-error, or old Yona-facing ABI; shared
code beneath both surfaces is representation-neutral internal substrate.
Every wrapper validates its manifest descriptor and arguments,
stages allocations/resources, and obeys Task 7's precommit/commit rule. The
legacy names and layouts continue to exist only for oracle-linked objects but
no replacement source references them. `StdlibLeafAbiTest` iterates every
manifest row, calls all CheckedDirect zero/one/many-argument shapes with
success, malformed descriptor, overlap, forced OOM, and managed ownership,
and verifies that every named symbol is declared in its owning header and
defined in exactly one runtime component. Thus Task 16's conformance and
replacement archive scans validate already-existing code rather than relying
on Task 17 to implement missing runtime behavior.

The seventeen new `lib/Std/*.yona` files are canonical source modules, not a
blanket declaration-only category. Their module names match the existing
`.yonai` identities exactly. In every one, transformations expressible in Yona
are Yona Definitions; only OS/external-library, mutable/bit-layout, or measured
hot-loop substrate is a private `extern`/intrinsic leaf. Task 16's bootstrap
compiler generates v2 artifacts for these files together with the existing
twenty-eight Yona modules, giving every checked-in interface one permanent
source of truth.

`Std\ByteArray`, `Std\IntArray`, and `Std\FloatArray` are explicitly mixed
Yona implementation modules. Their only private semantic-intrinsic declarations are
Task 11's callback-free `rawAllocZeroed`, `rawLength`, `rawGet`, and
`rawPutMove` families plus ByteArray `rawFromString`, bound one-to-one to the
thirteen ABI-distinct V2 symbols. All public exports are Yona Definitions:

- ByteArray wraps `alloc`, `length`, `get`, and `fromString`; it implements
  `concat`, `join`, `slice`, `head`, `tail`, `map`, `foldl`, `fromSeq`,
  `toSeq`, and persistent `set : ByteArray -> Int -> Int -> ByteArray` in
  Yona. The old mutating Unit-returning set and unsafe `toString` export are
  removed; strict byte decoding remains `Std\Convert.decodeUtf8`.
- IntArray wraps `alloc`, `length`, and `get`; it implements `fill`, `cons`,
  `filter`, `foldl`, `fromSeq`, `head`, `join`, `map`, persistent `set`,
  `slice`, `tail`, and `toSeq` in Yona.
- FloatArray wraps `alloc`, `length`, and `get`; it implements `cons`, `fill`,
  `foldl`, `head`, `join`, `map`, persistent `set`, and `tail` in Yona.

Their local builder loops allocate once and thread the one Owned accumulator
through `rawPutMove`. Map/fold/filter invoke the source callable in Yona, in
index order, with its inferred effect-polymorphic row; filter evaluates the
predicate exactly once per element, threads a write index through a capacity-
length builder, and slices once at the end. No C callback or two-pass predicate
evaluation remains. These three modules import nothing beyond implicit Prelude
and use local recursion plus builtin sequence construction/access, so they do
not enlarge Task 16's bootstrap SCC. Replacement-only source/interface tests
cover the breaking ByteArray set/toString changes and exact callable-effect
propagation without entering the legacy-discovered fixture or generated-doc
suites before cutover.
Cross-module tests in this task exercise catalog + codec + specialization
directly; the end-to-end `CompilerPipeline` test begins in Task 16.
Every private native extern has an explicit ABI-distinct replacement
`Linkage.NativeSymbol`; semantic array intrinsics instead encode their closed
enum/storage-kind identity in Typed IR and never create Function linkage. In
particular `Std\File` and `Std\Net` use exactly the
thirteen platform Outcome symbols from Task 14 and their structural signatures
include the generated Promise/result contracts. The reader rejects a legacy
native symbol in a v2 module, and no catalog or lowerer rewrites a symbol name
implicitly.

Before freezing digests, finalize documentation comments in all 41
`publish_api` sources. Each must have exactly one leading contiguous `##`
module summary before `module`, and every public function, nominal, resource,
or trait export must resolve to exactly one contiguous `##` declaration block;
private leaves/helpers are omitted and there is no public `@nodoc` escape.
`stdlib_manifest_contract.py --check-source-docs` compares parsed public
declarations (including leading blank-header handling) with those blocks and
fails on missing, extra, duplicate, or stale names. Task 17 may re-render this
already-frozen prose but must not edit canonical source comments.

- [ ] **Step 8: Verify component acyclicity and cross-module behavior**

```bash
mkdir -p out/build
cp lib/stdlib-manifest.toml \
  out/build/stdlib-manifest.before-digest-update.toml
python3 scripts/generate_stdlib_manifest.py --update-source-digests
python3 scripts/generate_stdlib_manifest.py --check
python3 test/CMake/stdlib_manifest_contract.py \
  --verify-digest-only-update \
  out/build/stdlib-manifest.before-digest-update.toml \
  lib/stdlib-manifest.toml
python3 test/CMake/stdlib_manifest_contract.py --check-source-docs
./scripts/check-yona-grammar.sh
(cd editors/vscode && npm test)
cmake --preset x64-debug-linux
cmake --build --preset build-debug-linux --target tests -j2
python3 scripts/run-focused-tests.py ./out/build/x64-debug-linux/tests \
  -tc='Interface v2*,Interface documentation*,Typed IR interface codec*,Generic specialization*,*cross-module*,*Trait*,Stdlib leaf ABI*,File iterator ABI*,Channel resource ABI*,*linearity*resource*'
python3 test/CMake/stdlib_manifest_contract.py
python3 scripts/quality.py architecture
if rg -n 'yona/TypedIr' include/yona/Interface src/Interface; then
  exit 1
fi
git diff --check
```

Expected: focused tests pass; the 45/41 source/Docs and closed leaf inventories
match their deterministic generated include; architecture checks pass; the
`rg` command prints nothing; and `v2_complete.yonai` matches the checked-in
SHA-256 on the host without a host-endian rewrite.
The digest-only verifier parses both TOML files, requires the same canonical
non-digest tree and ordering byte-for-byte, requires all 45 post-update
`source_sha256` values to match the LF-normalized source bytes, and requires
every changed field to be one of those digest values. It deliberately does
not require all 45 hashes to differ: an already-current source digest remains
byte-identical. This is the sole mutating digest command in the program;
Tasks 16/17 and CI use `--check` only.

- [ ] **Step 9: Document and commit v2 internals**

Finish the normative field/discriminant/normalization tables and annotated
golden hex dump in `docs/interface-v2-format.md`. Document `YONAI 2`, byte
framing, structural signatures, and the absence of source reparsing in a
clearly marked future/test-only section of `docs/typed-ir.md`.
Current-behavior module/derive/public docs remain unchanged until the cutover.

```bash
git add include/yona/Interface src/Interface include/yona/TypedIr \
  src/TypedIr include/yona/Model/Types.h include/yona/Model/InferType.h \
  include/yona/Model/TypeArena.h include/yona/Model/TypeEnv.h \
  src/Model/TypeArena.cpp src/Model/TypeEnv.cpp \
  include/yona/Semantics/Unification.h src/Semantics/Unification.cpp \
  include/yona/Semantics/TypedInterfaceCatalog.h \
  src/Semantics/TypedInterfaceCatalog.cpp \
  include/yona/Semantics/TraitResolution.h src/Semantics/TraitResolution.cpp \
  include/yona/Semantics/StructuralSchemeImporter.h \
  src/Semantics/StructuralSchemeImporter.cpp \
  include/yona/Semantics/StdlibManifest.h \
  src/Semantics/StdlibManifest.cpp \
  include/yona/Semantics/StructuralTypeProjection.h \
  src/Semantics/StructuralTypeProjection.cpp \
  include/yona/Semantics/TypeChecker.h src/Semantics/TypeChecker.cpp \
  include/yona/Semantics/LinearityChecker.h \
  src/Semantics/LinearityChecker.cpp \
  include/yona/Semantics/SemanticModel.h src/Semantics/SemanticModel.cpp \
  include/yona/Semantics/RuntimeEntryRegistry.h \
  include/yona/Semantics/RuntimeEntryRegistry.def \
  src/Semantics/RuntimeEntryRegistry.cpp \
  include/yona/Support/SourceManager.h \
  src/Support/SourceManager.cpp include/yona/Syntax/Ast.h \
  include/yona/Syntax/AstVisitor.h include/yona/Syntax/AstVisitorImpl.h \
  include/yona/Syntax/Lexer.h include/yona/Syntax/Parser.h \
  src/Syntax/Ast.cpp src/Syntax/Lexer.cpp src/Syntax/Parser.cpp \
  src/Syntax/ParserModule.cpp src/Syntax/ParserImpl.h \
  src/Syntax/ParserType.cpp src/Syntax/YonaStyle.cpp \
  site/grammars/yona.tmLanguage.json \
  editors/vscode/syntaxes/yona.tmLanguage.json \
  editors/vscode/src/test/run.ts scripts/check-yona-grammar.sh \
  test/Syntax/AstTest.cpp test/Syntax/LexerTest.cpp \
  test/Syntax/YonaStyleTest.cpp test/Interface \
  test/Semantics/RuntimeEntryRegistryTest.cpp \
  test/TypedIr \
  lib/stdlib-manifest.toml lib/Prelude.yona \
  lib/Std/Constants/Platform.yona \
  lib/Std/{Channel,Convert,Gpu,Http,Io,Iterator,Json,Math,Parallel,Regex,Stream,Task,Utf16}.yona \
  lib/Std/{ByteArray,Crypto,Dict,Encoding,File,FloatArray,Format,IntArray,List,Log,Net,Path,Process,Random,Set,String,Time,Types}.yona \
  test/Semantics/InterfaceCatalogTest.cpp \
  test/Semantics/StructuralSchemeImporterTest.cpp \
  test/Semantics/TraitResolutionTest.cpp test/Semantics/TraitTest.cpp \
  test/Semantics/TypeCheckerTest.cpp test/Semantics/SemanticModelTest.cpp \
  test/Semantics/LinearityCheckerTest.cpp \
  test/Fixtures/Interface scripts/generate_stdlib_manifest.py \
  test/CMake/stdlib_manifest_contract.py \
  include/yona/Runtime/Stdlib src/Runtime/Stdlib \
  include/yona/Runtime/Codecs/Json.h src/Runtime/Codecs/Json.c \
  include/yona/Runtime/Codecs/Regex.h src/Runtime/Codecs/Regex.c \
  include/yona/Runtime/Gpu/Api.h include/yona/Runtime/Gpu/VulkanDevice.h \
  src/Runtime/Gpu/Cpu.c src/Runtime/Gpu/Stub.c \
  src/Runtime/Gpu/VulkanDevice.c src/Runtime/Gpu/VulkanOperations.c \
  include/yona/Codegen/Llvm/ModuleLowerer.h \
  include/yona/Codegen/Llvm/BlockLowerer.h \
  include/yona/Codegen/Llvm/TypeLowering.h \
  src/Codegen/Llvm/ModuleLowerer.cpp src/Codegen/Llvm/BlockLowerer.cpp \
  src/Codegen/Llvm/TypeLowering.cpp \
  test/Codegen/LlvmLoweringTest.cpp \
  test/Codegen/TypedIrExecutionTest.cpp \
  test/Runtime/StdlibLeafAbiTest.cpp test/Runtime/JsonAbiTest.cpp \
  test/Runtime/FileIteratorAbiTest.cpp \
  test/Runtime/ChannelResourceAbiTest.cpp \
  cmake/YonaComponents.cmake CMakeLists.txt \
  docs/typed-ir.md docs/interface-v2-format.md
git add docs/superpowers/plans/2026-09-01-typed-ir-codegen-rewrite.md
git commit -m "feat: serialize verified generic typed ir"
```

### Task 16: Finish accelerators, debug/toolchain integration, and the parity matrix

**Files:**

- Create: `include/yona/TypedIr/Passes/AcceleratorLowering.h`
- Create: `src/TypedIr/Passes/AcceleratorLowering.cpp`
- Create: `test/TypedIr/AcceleratorLoweringTest.cpp`
- Create: `include/yona/Codegen/Llvm/AcceleratorLowering.h`
- Create: `src/Codegen/Llvm/AcceleratorLowering.cpp`
- Create: `include/yona/Codegen/Llvm/DebugInfo.h`
- Create: `src/Codegen/Llvm/DebugInfo.cpp`
- Create: `include/yona/Toolchain/CompilerPipeline.h`
- Create: `src/Toolchain/CompilerPipeline.cpp`
- Create: `src/Toolchain/CompilerPipelineInternal.h`
- Create: `test/Codegen/DebugInfoTest.cpp`
- Create: `test/Codegen/AbiMatrixTest.cpp`
- Create: `test/Toolchain/CompilerPipelineTest.cpp`
- Create: `test/Toolchain/CrossModulePipelineTest.cpp`
- Create: `test/Codegen/ReplacementGenericAbiTest.cpp`
- Create: `tools/stdlib-interface-generator/main.cpp`
- Create: `test/CMake/typed_ir_runtime_boundary_contract.py`
- Create: `test/Runtime/ReplacementAbiConformance.cpp`
- Create: `test/Runtime/GpuTypedKernelTest.cpp`
- Modify: `test/TypedIr/ClosureConversionTest.cpp`
- Modify: `test/TypedIr/OwnershipLoweringTest.cpp`
- Modify: `test/Codegen/LlvmLoweringTest.cpp`
- Create: `scripts/regenerate_interfaces.py`
- Modify: `include/yona/Semantics/AcceleratorDiag.h`
- Modify: `src/Semantics/AcceleratorDiag.cpp`
- Modify: `include/yona/TypedIr/Instruction.h`
- Modify: `include/yona/TypedIr/TypedIr.h`
- Modify: `include/yona/TypedIr/Builder.h`
- Modify: `src/TypedIr/Builder.cpp`
- Modify: `src/TypedIr/Verifier.cpp`
- Modify: `src/TypedIr/Printer.cpp`
- Modify: `src/TypedIr/Parser.cpp`
- Modify: `test/TypedIr/PrinterParserTest.cpp`
- Modify: `src/TypedIr/Pipeline.cpp`
- Modify: `src/TypedIr/GenericPreparation.cpp`
- Modify: `src/TypedIr/Passes/ClosureConversion.cpp`
- Modify: `src/TypedIr/Analysis/FreeVariables.cpp`
- Modify: `src/TypedIr/Analysis/EscapeAnalysis.cpp`
- Modify: `src/TypedIr/Analysis/OwnershipAnalysis.cpp`
- Modify: `src/TypedIr/Passes/OwnershipLowering.cpp`
- Modify: `src/TypedIr/Passes/CleanupLowering.cpp`
- Modify: `src/TypedIr/Verification/CallableVerifier.cpp`
- Modify: `src/TypedIr/Verification/EscapeVerifier.cpp`
- Modify: `src/TypedIr/Verification/OwnershipVerifier.cpp`
- Modify: `src/TypedIr/Verification/CleanupVerifier.cpp`
- Modify: `test/TypedIr/OwnershipVerifierTest.cpp`
- Modify: `test/TypedIr/CleanupLoweringTest.cpp`
- Modify: `test/TypedIr/CleanupVerifierTest.cpp`
- Modify: `test/TypedIr/EscapeVerifierTest.cpp`
- Modify: `include/yona/TypedIr/Passes/RuntimeFailureNormalization.h`
- Modify: `src/TypedIr/Passes/RuntimeFailureNormalization.cpp`
- Modify: `test/TypedIr/RuntimeFailureNormalizationTest.cpp`
- Modify: `src/TypedIr/Passes/EffectPreparation.cpp`
- Modify: `src/TypedIr/Passes/OperationInstantiation.cpp`
- Modify: `test/TypedIr/OperationInstantiationTest.cpp`
- Modify: `include/yona/Codegen/Llvm/ModuleLowerer.h`
- Modify: `src/Codegen/Llvm/ModuleLowerer.cpp`
- Modify: `src/Codegen/Llvm/FunctionLowerer.cpp`
- Modify: `src/Codegen/Llvm/BlockLowerer.cpp`
- Modify: `src/Codegen/Llvm/TypeLowering.cpp`
- Modify: `src/Codegen/Llvm/Finalization.cpp`
- Modify: `include/yona/Runtime/Gpu/Api.h`
- Modify: `include/yona/Runtime/Gpu/VulkanDevice.h`
- Modify: `src/Runtime/Gpu/Cpu.c`
- Modify: `src/Runtime/Gpu/Stub.c`
- Modify: `src/Runtime/Gpu/VulkanDevice.c`
- Modify: `src/Runtime/Gpu/VulkanOperations.c`
- Modify: `cmake/YonaComponents.cmake`
- Modify: `scripts/check_architecture.py`
- Modify: `scripts/test-arm64-qemu.sh`
- Modify: `.github/workflows/cmake-multi-platform.yml`
- Modify: `CMakeLists.txt`
- Modify: `docs/gpu-transparent-lowering.md`
- Modify: `docs/typed-ir.md`
- Modify: `docs/quality.md`

**Interfaces:**

- Consumes: the full canonical/pass/runtime/interface stack from Tasks 2-15.
- Produces: typed accelerator decisions, debug lowering, one test-only
  `CompilerPipeline`, deterministic interface regeneration tooling, and the
  complete O0-O3 ABI/ownership/cleanup parity gate required for cutover.
- Production rule: `CompilerPipeline` is final code, but only the
  `yona-stdlib-interface-generator` and tests call it; it remains the sole
  non-installed stdlib bootstrap entry after Task 17.

- [ ] **Step 1: Write red accelerator and debug tests**

Accelerator tests operate on resolved typed array operations, not AST or LLVM
shape. They cover fixed kernels, CPU fallback, strict E0700, explicit GPU
calls, effectful rejection, and decision reports:

```cpp
TEST_CASE("Typed accelerator selection emits one resolved disposition") {
  auto Module = makeResolvedArrayOperationCandidate(
      AcceleratorKernel::IntMapSquare);
  std::vector<AcceleratorDecision> Decisions;
  REQUIRE(runAcceleratorLowering(
              Module, {.Enabled = true, .Strict = false}, Decisions)
              .has_value());
  REQUIRE(Decisions.size() == 1);
  CHECK(Decisions[0].Disposition == AcceleratorDisposition::Accelerator);
  CHECK(Decisions[0].Kernel == AcceleratorKernel::IntMapSquare);
}
```

Debug tests assert `DISubprogram`, `DILocation`, lexical scopes, and that
closure/handler functions do not inherit an outer LLVM/debug value map.
Name its sentinel exactly `Typed IR debug info preserves generated lexical
scopes`.

- [ ] **Step 2: Define the complete table-driven ABI matrix before plumbing**

Create rows for Unit, Bool, Byte, Char, Int, Float, Symbol, String, Sum, tuple,
sequence, set, dictionary, record, nonrecursive ADT, recursive/heap ADT,
array, channel, Promise, closure, and legal linear resource values. Array,
channel, Promise, and resource cells are marked applicable only where that
boundary legally admits their ownership/lifetime contract; an illegal cell is
an asserted verifier rejection, not silently omitted. Columns are direct call, noncapturing closure,
capturing closure, under/over application, async extern 0/1/N, perform/resume,
exception propagation, exported call, and cross-module call. Outcomes are
Success, Raised, Performed, and Cancelled where legal.
Byte/Char/Symbol have scalar direct/export/cross-module round trips; Symbol
also uses forced fingerprint collisions with distinct UTF-8 spellings so no
matrix path mistakes the hash for equality.
Sum has both scalar and managed alternatives across direct, closure, export,
and cross-module cells; clone/release/conformance dispatch through the actual
alternative descriptor. A linear alternative is consume-only and every
attempted clone/Keep/duplicating boundary is an asserted verifier/runtime
rejection.

The harness is data, not hand-written subcases:

```cpp
struct AbiMatrixCase {
  std::string Name;
  std::string Source;
  std::string ExpectedStdout;
  std::vector<std::string> ExpectedBalancedTags;
  std::array<int, 4> OptimizationLevels{0, 1, 2, 3};
};

for (const auto &Case : abiMatrixCases()) {
  CAPTURE(Case.Name);
  executeTypedIrForTest(
      Case.Source,
      {.Stdout = Case.ExpectedStdout,
       .ExpectedLiveAllocations = zeroFor(Case.ExpectedBalancedTags)},
      Case.OptimizationLevels);
}
```

Wrap the table loop in top-level case `ABI matrix covers every carrier and
control outcome`, and create `Compiler pipeline: expression and module
lowering share finalization`. Together with the accelerator case above and
the permanent Task 9 callable sentinel, every Task 16 positive filter is
independently reachable.

Maintain this exact traceability register. Applicable cases are also ABI
matrix rows at O0-O3; structural/frontend/platform-only cases stay focused
tests and are not mislabeled as ABI cells.

| # | Exact test/fixture | Owning task |
|---:|---|---:|
| 1 | `test/Runtime/CallableTest.cpp` — `Runtime callable applies a runtime-selected arity incrementally` | 9 |
| 2 | `test/Codegen/CallableLoweringTest.cpp` — `Callable lowering agrees for captured native and universal entries` | 9 |
| 3 | `test/TypedIr/EffectConversionTest.cpp` — `Effect conversion gives handlers explicit lexical environments` | 13 |
| 4 | `test/Codegen/EffectLoweringTest.cpp` — `Effect lowering preserves typed argument and resume carriers` | 13 |
| 5 | `test/Codegen/AsyncAbiTest.cpp` — `Async ABI uses one descriptor for arities zero one and many` | 14 |
| 6 | `test/TypedIr/EscapeAnalysisTest.cpp` — `Typed IR escape: propagates through nested owned aggregates` | 10 |
| 7 | `test/Codegen/PatternOwnershipTest.cpp` — `Typed IR collections: pattern projections retain every escaping payload` | 11 |
| 8 | `test/Codegen/CollectionLoweringTest.cpp` — `Typed IR collections: typeOf stores descriptor-derived owned names` | 11 |
| 9 | `test/TypedIr/ControlOutcomeLoweringTest.cpp` — `Control outcome lowering transfers a complete exception ADT` | 12 |
| 10 | `test/Runtime/HamtRcTest.cpp` — `HamtRc replacement dictionary consumes duplicate key and old value` | 11 |
| 11 | `seq_generator_named_reuse.yona` and `seq_generator_named_reuse_fused.yona` | 11 |
| 12 | `test/Codegen/CollectionLoweringTest.cpp` — `Typed IR collections: heap ADT update uses runtime functional update` | 11 |
| 13 | `test/Syntax/FunctionClauseParserTest.cpp` — `Function clauses preserve every equation and guard` | 4 |
| 14 | `test/TypedIr/ClosureConversionTest.cpp` — `Compiler pipeline: source lowering exposes free references in every expression family` | 16 |
| 15 | `test/TypedIr/CleanupVerifierTest.cpp` — `Typed IR control outcomes: with finalizer runs for every control outcome` | 12-14 |
| 16 | `test/TypedIr/OwnershipLoweringTest.cpp` — `Typed IR ownership: cleanup handles more than sixteen live owners` | 10/12 |
| 17 | `test/TypedIr/ControlFlowLoweringTest.cpp` — `Typed IR control flow lowering: non-exhaustive match reaches only MatchError` | 6 |
| 18 | `test/Codegen/LlvmLoweringTest.cpp` — `Compiler pipeline: expression and module lowering share finalization` | 16 |
| 19 | `test/TypedIr/TailCallLoweringTest.cpp` — `Typed IR tail calls: cleanup is edge-local` | 12 |
| 20 | `test/CMake/native_arm64_ci_packaging_contract.py` — `AArch64 longjmp keeps restore base in x16` | 1 |
| 21 | `test/Semantics/TypeCheckerTest.cpp` — `Guard and pattern typing rejects non-Bool case guards` | 4 |
| 22 | `test/Semantics/TypeCheckerTest.cpp` — `Guard and pattern typing requires identical or-pattern bindings` | 4 |
| 23 | `test/Semantics/TypeCheckerTest.cpp` — `Guard and pattern typing rejects non-Bool generator guards` | 4 |

Task 9 adds exact case `Typed IR callables: verifier rejects a partial wrapper
with a foreign outer ValueId`; currying no longer generates wrappers, but the
verifier permanently guards the latent path and Task 16's full gate reruns it.

- [ ] **Step 3: Run and confirm accelerator/pipeline/matrix gaps**

```bash
cmake --build --preset build-debug-linux --target tests -j2
python3 scripts/run-focused-tests.py ./out/build/x64-debug-linux/tests \
  -tc='Typed accelerator*,Typed IR debug info*,Compiler pipeline*,ABI matrix*,Typed IR callables*'
```

Expected: missing pass/pipeline headers and failing unsupported matrix cells.

- [ ] **Step 4: Move accelerator selection before LLVM**

Expose:

```cpp
struct AcceleratorOptions { bool Enabled; bool Strict; };
enum class AcceleratorDisposition { Accelerator, Cpu };
enum class AcceleratorKernel {
  None, IntMapAdd, IntMapMul, IntMapSquare, IntFilterGreater,
  IntFilterLess, IntReduceSum, FloatScale, FloatReduceSum
};
struct AcceleratorDecision {
  SourceRange Range;
  AcceleratorKernel Kernel;
  AcceleratorDisposition Disposition;
  std::string Reason;
};
enum class ArrayOperationKind : std::uint8_t {
  IntMap, IntFilter, IntReduce, FloatMap, FloatReduce
};
struct ArrayOperationCandidateInst {
  model::FunctionDeclarationIdentity ResolvedOperation;
  ArrayOperationKind Operation;
  FunctionId FrozenFallback;
  std::vector<ValueId> Operands;
  std::vector<model::ParameterOwnership> OperandOwnerships;
  std::optional<std::uint32_t> CallableOperand;
  std::optional<FunctionId> StaticCallableBody;
  std::optional<model::TraitTargetApplication> CallableTraitEvidence;
  std::optional<ValueId> BoundaryContext;
  model::TypeId ResultType;
  OwnershipKind ResultOwnership;
  model::EffectRowId SemanticEffects;
  std::optional<RuntimeEffectRowId> RuntimeEffects;
  SourceRange Range;
};
struct AcceleratorOp {
  AcceleratorKernel Kernel;
  FunctionId FrozenFallback;
  std::vector<ValueId> SourceOperands;
  std::vector<model::ParameterOwnership> SourceOwnerships;
  std::vector<ValueId> KernelOperands;
  std::optional<std::uint32_t> EliminatedCallableOperand;
  ValueId BoundaryContext;
  model::TypeId ResultType;
  OwnershipKind ResultOwnership;
  SourceRange Range;
};
struct CpuArrayOp {
  FunctionId FrozenFallback;
  std::vector<ValueId> Operands;
  ValueId BoundaryContext;
};
struct AcceleratorEligibilityNote {
  SourceRange Range;
  model::FunctionDeclarationIdentity ResolvedOperation;
  std::string Reason;
};
PassResult materializeArrayOperationCandidates(Module &);
PassResult runAcceleratorLowering(Module &, const AcceleratorOptions &,
                                  std::vector<AcceleratorDecision> &);
```

Task 16 adds `materializeArrayOperationCandidates` as the final transactional
subpass of `prepareGenericModule`, after every generic/trait target is closed
but before publishing `GenericPrepared`. It uses one compiler-owned immutable
allowlist keyed by complete `FunctionDeclarationIdentity`; each entry contains
the exact public FunctionType, `ArrayOperationKind`, and optional callable
operand index. A call becomes a candidate only when both identity and complete
structural signature match and its closed semantic row has no operation,
`MayRaise == false`, and `MayCancel == false`. At this phase RuntimeEffects is
necessarily empty: materialization copies that semantic row into
`SemanticEffects`, writes `RuntimeEffects = std::nullopt`, and never fabricates
an operation instance. `BoundaryContext` is likewise null at this pre-effect
phase. Task 16 extends Task 13's effect-preparation transaction to fill it with
the containing function's newly installed exact hidden parameter alongside
ordinary Yona direct calls; from `EffectPrepared` onward it is required and
preserved. It preserves the already-resolved fallback
`FunctionId`, all original operands, the fallback FunctionType's exact
`ParameterOwnership` vector, exact result ownership/type/effects, and
the projected trait target when a callable came through trait dispatch. For a
direct `MakeFunctionInst` operand it also records that existing body ID;
`MakeClosureInst` does not exist at this pre-closure phase. Closure conversion
later rewrites that defining value normally and preserves/cross-checks the
recorded body identity; otherwise `StaticCallableBody` is empty and selection
must choose CPU.
The helper requires the private resolved Canonical runtime module, performs
all call replacements/note additions in one `ModuleMutationTransaction`, and
publishes neither arena on failure. `prepareGenericModule` converts its ranged
`PassDiagnostic`s to
`SpecializationErrorCode::InvalidAcceleratorCandidate`,
discards the still-private runtime module, and leaves the caller's cache and
published inputs unchanged because all cache states are still in Task 15's
uncommitted `SpecializationCacheTransaction` overlay. A late-failure regression
forces this path after at least one Ready specialization, asserts the caller
cache remains unbound/byte-identical, and retries successfully with that same
cache. It is never called after `GenericPrepared`.
There is no source-spelling, AST-shape, LLVM-shape, or debug-name recognition.
An otherwise recognized effectful, raising, or cancelling call is not a
candidate: materialization leaves its original
`DirectCallInst` unchanged and appends one deterministic module-owned
`AcceleratorEligibilityNote`. Every intervening pass through
`TailCallsLowered` preserves that call/note pair. The selector emits the note
as a CPU report row (and E0700 in Strict mode) without rewriting the call;
the later control-outcome pass may then transform the preserved call to
`InvokeOutcome` according to its runtime row. Such a call can never be
squeezed into `CpuArrayOp`.
Task 16 extends Task 13's existing operation-instantiation transaction to fill
and cross-check each candidate's `RuntimeEffects` with the canonical closed
empty runtime row at `OperationInstantiated`; any missing/nonempty/raising/
cancelling mismatch rolls back the whole transaction. The candidate is handled
by every operand/remap/text/phase visitor; closure
conversion updates its callable evidence without replacing or synthesizing the
fallback, and all intervening passes preserve it except ordinary ID remapping.

Task 16 also extends `Module` with the checked, deterministic
`AcceleratorEligibilityNotes` arena. Candidate materialization appends at most
one note per source range/resolved identity/reason tuple in source order in the
same transaction, so failure restores both candidates and notes exactly.
Clone/remap preserves the source range and remaps the complete declaration
identity; the text printer/parser round-trips notes in runtime modules. Phase
verification requires byte-identical ordered notes from `GenericPrepared`
through `TailCallsLowered`. They are created only after generic-definition
extraction and therefore are not part of v2/TIRF generic fragments. A
successful selector consumes all notes into ordered decision rows and erases
the arena when publishing `AcceleratorSelected`; Strict E0700 failure leaves
the input candidate and note arenas unchanged.

The selector consumes `TailCallsLowered` and produces `AcceleratorSelected`.
Every candidate must now have its populated canonical empty `RuntimeEffects`;
missing or nonempty rows are verifier errors rather than CPU fallbacks.
It recognizes only a documented bounded set of canonical pure callable-body
graphs (the listed add/multiply/square/comparison/scale/reduce kernels), using
typed opcodes, def-use links, constants, and exact capture contracts. Managed
or Consume captures are ineligible. For a recognized closure with only
Trivial captures, the selector emits ordinary `CaptureBorrowInst` projections
before the selected operation and builds `KernelOperands` from the mapping
table's array source operand followed by those scalar results in canonical
capture-index order; it never passes the callable object
or an environment pointer to C. `SourceOperands` and `SourceOwnerships` retain
the entire original call contract, including the now-elided callable operand.
Any extra block, call, effect, ownership mismatch, dynamic callable, or
unrecognized graph deterministically selects CPU with a reason. Emit explicit
raw `AcceleratorOp` or `CpuArrayOp`. The verifier rejects unresolved candidates
at `AcceleratorSelected`. Both outcomes preserve the candidate's required
boundary context: `CpuArrayOp` passes it after the declared operands to the
Yona DirectReturn fallback, while `AcceleratorOp` retains it for its dynamic
Unavailable fallback edge. Every candidate
persists the ordinary fallback callee selected
before closure conversion; `CpuArrayOp::FrozenFallback` must be that existing
closed FunctionId from the `ClosureConverted` function-set snapshot, with an
exact operand/result/context contract and a closed empty/nonraising/noncancelling row.
Accelerator lowering may neither import
nor synthesize a helper, and the verifier rejects a new/foreign/mismatched ID.
Add the candidate and both raw outcomes to `InstructionPayload` and its
operand visitor. `None` is valid only for a CPU disposition. In Strict mode every
unsupported candidate still records one CPU decision/reason, then returns
E0700 and leaves the input phase unchanged; explicit GPU calls and effectful
ineligibility notes follow their separately tested diagnostics. `AcceleratorDiag`
consumes decisions and no longer scans AST.

The selected accelerator path has a real replacement ABI in this task; it
does not call any legacy `int64_t *`/`double *` Yona-array entry and does not
duplicate a Yona combinator in C. Declare the three-way status and these eight
ABI-distinct functions in `Runtime/Gpu/Api.h`:

```c
typedef uint32_t YonaAbiAcceleratorStatus;
enum {
  YONA_ABI_ACCELERATOR_ERROR = 0u,
  YONA_ABI_ACCELERATOR_UNAVAILABLE = 1u,
  YONA_ABI_ACCELERATOR_SUCCESS = 2u
};

YonaAbiAcceleratorStatus YonaRuntimeAbiGpuIntArrayMapAddV2(
    const YonaAbiTypeDescriptor *ArrayType,
    YonaIntArrayRef BorrowedInput, int64_t Delta,
    YonaIntArrayRef *EmptyOutput, YonaControlOutcome *EmptyFailure);
YonaAbiAcceleratorStatus YonaRuntimeAbiGpuIntArrayMapMulV2(
    const YonaAbiTypeDescriptor *ArrayType,
    YonaIntArrayRef BorrowedInput, int64_t Factor,
    YonaIntArrayRef *EmptyOutput, YonaControlOutcome *EmptyFailure);
YonaAbiAcceleratorStatus YonaRuntimeAbiGpuIntArrayMapSquareV2(
    const YonaAbiTypeDescriptor *ArrayType,
    YonaIntArrayRef BorrowedInput,
    YonaIntArrayRef *EmptyOutput, YonaControlOutcome *EmptyFailure);
YonaAbiAcceleratorStatus YonaRuntimeAbiGpuIntArrayFilterGreaterV2(
    const YonaAbiTypeDescriptor *ArrayType,
    YonaIntArrayRef BorrowedInput, int64_t Threshold,
    YonaIntArrayRef *EmptyOutput, YonaControlOutcome *EmptyFailure);
YonaAbiAcceleratorStatus YonaRuntimeAbiGpuIntArrayFilterLessV2(
    const YonaAbiTypeDescriptor *ArrayType,
    YonaIntArrayRef BorrowedInput, int64_t Threshold,
    YonaIntArrayRef *EmptyOutput, YonaControlOutcome *EmptyFailure);
YonaAbiAcceleratorStatus YonaRuntimeAbiGpuIntArrayReduceSumV2(
    const YonaAbiTypeDescriptor *ArrayType,
    YonaIntArrayRef BorrowedInput, int64_t *Output,
    YonaControlOutcome *EmptyFailure);
YonaAbiAcceleratorStatus YonaRuntimeAbiGpuFloatArrayScaleV2(
    const YonaAbiTypeDescriptor *ArrayType,
    YonaFloatArrayRef BorrowedInput, double Scale,
    YonaFloatArrayRef *EmptyOutput, YonaControlOutcome *EmptyFailure);
YonaAbiAcceleratorStatus YonaRuntimeAbiGpuFloatArrayReduceSumV2(
    const YonaAbiTypeDescriptor *ArrayType,
    YonaFloatArrayRef BorrowedInput, double *Output,
    YonaControlOutcome *EmptyFailure);
```

Every function validates the full canonical descriptor bytes and kind—not a
fingerprint alone—against the borrowed object before reading it. Array result
slots are non-null, initially null, and storage-distinct from all inputs;
scalar result slots are non-null, storage-distinct, and written only on
Success;
`EmptyFailure` is non-null, initially Empty, and disjoint from every other
slot. The borrowed input is never mutated. The implementation materializes a
private contiguous scratch span through Task 11's checked array leaves and
tries only the matching GPU/span substrate. Internal GPU span primitives
accept only `(pointer, count)` storage views owned by this wrapper—never a
`Yona*ArrayRef`, descriptor, closure, or raw legacy array layout. The C layer
does not implement map, filter, or fold as a CPU fallback: that semantic
fallback remains the `FrozenFallback` Yona Definition.

Success constructs the final immutable array through
`AllocZeroedV2`/`PutMoveV2` after its result length is known, publishes exactly
one Owned array or one Trivial scalar, and leaves failure Empty. Unavailable
frees all private scratch state and leaves the input, result storage, and
failure byte-for-byte unchanged. Error also leaves the input and result
storage unchanged, frees staging, and writes exactly one owned nonallocating
ABI diagnostic to `EmptyFailure`. An unknown status or a status/slot
poststate mismatch is a malformed runtime result and takes the Error invariant
path after releasing any structurally valid published value. Integer kernels
use defined two's-complement wrapping consistent with Task 8. Float reduction
may return Success only when the device preserves the language's specified
ordered fold, including NaN and signed-zero behavior; otherwise it returns
Unavailable so the canonical Yona implementation runs.

The selector and LLVM helper share this closed mapping; no implementation may
infer operand positions from a kernel name:

| Kernel | Source operation | Recognized callable/initializer | Array source operand | Ordered `KernelOperands` | Runtime symbol | Result |
|---|---|---|---:|---|---|---|
| `IntMapAdd` | `IntMap` | one Trivial Int capture `d`; body `x + d` | 1 | array, `d` | `YonaRuntimeAbiGpuIntArrayMapAddV2` | Owned IntArray |
| `IntMapMul` | `IntMap` | one Trivial Int capture `k`; body `x * k` | 1 | array, `k` | `YonaRuntimeAbiGpuIntArrayMapMulV2` | Owned IntArray |
| `IntMapSquare` | `IntMap` | no capture; body `x * x` | 1 | array | `YonaRuntimeAbiGpuIntArrayMapSquareV2` | Owned IntArray |
| `IntFilterGreater` | `IntFilter` | one Trivial Int capture `t`; body `x > t` | 1 | array, `t` | `YonaRuntimeAbiGpuIntArrayFilterGreaterV2` | Owned IntArray |
| `IntFilterLess` | `IntFilter` | one Trivial Int capture `t`; body `x < t` | 1 | array, `t` | `YonaRuntimeAbiGpuIntArrayFilterLessV2` | Owned IntArray |
| `IntReduceSum` | `IntReduce` | no capture; body `a + x`; literal initial accumulator `0` | 2 | array | `YonaRuntimeAbiGpuIntArrayReduceSumV2` | Trivial Int |
| `FloatScale` | `FloatMap` | one Trivial Float capture `s`; body `x * s` | 1 | array, `s` | `YonaRuntimeAbiGpuFloatArrayScaleV2` | Owned FloatArray |
| `FloatReduceSum` | `FloatReduce` | no capture; body `a + x`; literal initial accumulator `+0.0` | 2 | array | `YonaRuntimeAbiGpuFloatArrayReduceSumV2` | Trivial Float |

Source operand zero is the callable in map/filter rows; in reduce rows it is
the reducer, operand one is the literal initializer, and operand two is the
array. `KernelOperands` excludes the callable and reduce initializer after
the verifier has proven the exact graph/literal, but `SourceOperands` keeps
them for ownership and fallback. Constants embedded directly in a callable
body are accepted only by the corresponding zero-capture graph; all scalar
parameters shown above come from the exact Trivial capture projection. The
verifier independently checks the table, the exact closed fallback
FunctionType, and every source/kernel index before phase publication.

Task 16 adds a dedicated terminator:

```cpp
struct CheckedAcceleratorOp {
  AcceleratorOp Operation;
  ProducedBranchTarget Success;
  BranchTarget Unavailable;
  RuntimeFailureDisposition Failure;
};
```

Add `CheckedAcceleratorOp` to `Terminator` and `SuccessorView`; Builder assigns
three pairwise-distinct deterministic EdgeIds and clone/remap preserves them.
It is handled exhaustively by operand, free-variable, escape, ownership,
cleanup, phase, text/parser, and LLVM visitors. The ownership analysis records
Consume only on Success or on the fallback call reached by Unavailable, never
on Error or on the Unavailable edge itself. Cleanup verification proves the
diagnostic and every still-live source owner reach Failure cleanup. Only this
checked form survives `ControlOutcomeLowered` through `LlvmReady`; tests reject
missing/duplicate edges, a produced prefix on Unavailable/Failure, and every
wrong per-edge owner state.

The final `runRuntimeFailureNormalization` subpass of
`AcceleratorSelected -> ControlOutcomeLowered` splits every raw selected
accelerator operation into this three-way form. Success defines the former
result and ownership lowering applies the saved source contract: Trivial and
Borrow operands remain untouched, while each Consume operand is cleared and
released exactly once after result publication. This includes a Consume
callable owner that was removed from `KernelOperands`; a Borrow callable
remains borrowed. Unavailable carries no result or diagnostic and enters a
generated block that invokes the existing `FrozenFallback` once with all
original `SourceOperands` plus `BoundaryContext`, using the same ownership
contracts, then forwards that result to the common continuation. Error owns
and releases the diagnostic and follows `TrapCompilerFailure`; every source
owner remains in its precommit state for cleanup. Thus neither recognition nor
dynamic device absence can silently drop an owner. Raw `AcceleratorOp` is
forbidden at `ControlOutcomeLowered` and later. LLVM maps only
`CheckedAcceleratorOp` to the exact tri-state symbols above. `CpuArrayOp`
remains an ordinary verified direct call of the frozen Yona fallback and never
enters this C ABI.

`makeResolvedArrayOperationCandidate` constructs a verified
`TailCallsLowered` fixture containing this exact record; it is not an implied
recognition hook. Tests cover candidate text round-trip and remapping through
closure/effect/tail passes, allowlist identity/signature mismatch, dynamic
callables, every recognized body graph, a nearly matching graph, and
transactional Strict failure that leaves the candidate and phase unchanged.
Runtime/ownership tests force Success, Unavailable, and Error for every
kernel, including Consume callable and array operands; they prove the success
edge releases each consumed source once, the unavailable edge invokes the
Yona fallback once with the original owners/context, and the error edge sees
all owners unchanged before cleanup.
Separate effectful/raising/cancelling cases prove materialization preserves the
original call and its exact result/semantic-effect contract, records one stable
note, and never emits `CpuArrayOp` in either mode. After selection,
control-outcome lowering must produce its required boundary/execution-context
and four-way `InvokeOutcome` contract.
An operation-instantiation phase-transition test starts with a candidate whose
runtime row is empty, proves the exact canonical row is installed, and proves
a forced mismatch rolls back the module, candidate, note arena, and phase.

`LlvmBlockLowerer` dispatches both surviving operations:
`CheckedAcceleratorOp` delegates to the bounded tri-state LLVM accelerator helper and
`CpuArrayOp` emits the verified pure direct fallback call. An effectful call
remains its already-checked ordinary/outcome form and never reaches this case.
`LlvmFunctionLowerer`
supplies function/value context and `TypeLowering` supplies exact array/
element ABI types. Add LLVM tests for every kernel and CPU fallback proving no
accelerator opcode reaches module finalization unhandled.

- [ ] **Step 5: Add debug info and one module-finalization path**

`LlvmDebugInfo` derives files/scopes/locations solely from Typed IR source
ranges. `LlvmModuleLowerer` owns one finalization method:

```cpp
std::expected<void, LoweringError>
LlvmModuleLowerer::finalize(llvm::Module &Output) {
  materializePendingDeclarations(Output);
  finalizeDebugInfo(Output);
  if (auto Verified = verifyLlvm(Output, "before optimization"); !Verified)
    return std::unexpected(Verified.error());
  optimizeLlvm(Output, Options_.OptimizationLevel);
  if (auto Verified = verifyLlvm(Output, "after optimization"); !Verified)
    return std::unexpected(Verified.error());
  return {};
}
```

Expression and module compilation call this same function, so no pending
ownership/declaration flush can diverge.

- [ ] **Step 6: Implement the final, still test-only compiler pipeline**

Expose:

```cpp
enum class EmitArtifact : std::uint32_t {
  None = 0, TypedCore = 1u << 0, TypedIr = 1u << 1,
  LlvmIr = 1u << 2, Object = 1u << 3, Interface = 1u << 4,
  AcceleratorReport = 1u << 5
};
constexpr EmitArtifact operator|(EmitArtifact Left, EmitArtifact Right);
constexpr bool contains(EmitArtifact Set, EmitArtifact Item);
struct TargetOptions {
  std::string Triple;
  std::string Cpu;
  std::string Features;
  std::filesystem::path Sysroot;
};
struct WarningOptions {
  bool Wall = false;
  bool Wextra = false;
  bool SuppressAll = false;
  bool WarningsAsErrors = false;
  bool IncompletePatterns = false;
  bool OverlappingPatterns = false;
};
struct SemanticOptions {
  WarningOptions Warnings;
  bool EnableRefinement = true;
  bool EnableLinearity = true;
  bool EnableLinearLeak = true;
  bool RequireEffectFree = false;
  bool CheckStyleOnly = false;
  bool Accelerator = true;
  bool StrictAccelerator = false;
};
struct CompileRequest {
  std::string Source;
  std::string Filename;
  std::vector<std::filesystem::path> ModulePaths;
  EmitArtifact Emit;
  TargetOptions Target;
  SemanticOptions Semantics;
  int OptimizationLevel = 2;
  bool DebugInfo = false;
};
enum class DiagnosticSeverity { Note, Warning, Error };
struct CompilationDiagnostic {
  DiagnosticSeverity Severity;
  std::string Code;
  SourceRange Range;
  std::string Message;
  std::vector<std::string> Notes;
};
struct CompilationArtifacts {
  std::shared_ptr<SourceManager> Sources;
  std::optional<std::string> TypedCore;
  std::optional<typed_ir::Module> TypedIr;
  std::unique_ptr<llvm::LLVMContext> LlvmContext;
  std::unique_ptr<llvm::Module> LlvmModule;
  std::optional<interface::v2::InterfaceModule> Interface;
  std::vector<std::byte> Object;
  std::vector<CompilationDiagnostic> Diagnostics;
  std::vector<AcceleratorDecision> AcceleratorReport;
};
struct CompilationFailure {
  std::shared_ptr<SourceManager> Sources;
  std::vector<CompilationDiagnostic> Diagnostics;
};
using CompilationResult = std::expected<CompilationArtifacts,
                                        CompilationFailure>;
CompilationResult compile(const CompileRequest &Request);

enum class LinkKind { Executable, SharedLibrary };
struct LinkRequest {
  std::vector<std::filesystem::path> Objects;
  std::filesystem::path Output;
  LinkKind Kind;
  TargetOptions Target;
  std::filesystem::path Linker;
  std::vector<std::filesystem::path> RuntimeLibraries;
  std::vector<std::string> ExtraArguments;
};
std::expected<void, CompilationFailure> link(const LinkRequest &Request);
```

Bootstrap authority is deliberately absent from that installed/public header.
The non-installed `src/Toolchain/CompilerPipelineInternal.h`, included only by
the pipeline implementation and non-installed
`yona-stdlib-interface-generator`, declares:

```cpp
enum class BootstrapMode : std::uint8_t {
  EmitSkeleton = 0, CompleteAgainstSkeleton = 1
};
class CanonicalStdlibManifest;
std::expected<CanonicalStdlibManifest, CompilationFailure>
makeCanonicalStdlibManifest(const std::filesystem::path &CanonicalRoot);
struct BootstrapCompileRequest {
  CompileRequest Base;
  BootstrapMode Mode;
  const CanonicalStdlibManifest *Manifest;
};
CompilationResult compileCanonicalStdlibForBootstrap(
    const BootstrapCompileRequest &Request);
struct BootstrapSkeletonSource {
  CompileRequest Base; // Emit == Interface; one authenticated SCC member
};
using BootstrapSkeletonBatchResult =
    std::expected<std::vector<interface::v2::InterfaceModule>,
                  CompilationFailure>;
BootstrapSkeletonBatchResult emitCanonicalStdlibSkeletonBatch(
    std::span<const BootstrapSkeletonSource> Sources,
    const CanonicalStdlibManifest &Manifest,
    const semantics::TypedInterfaceCatalog &PredecessorCompleteCatalog);
```

`CanonicalStdlibManifest` is immutable and opaque outside the implementation.
Its factory uses Task 15's embedded, hash-validated 45-entry module/leaf
manifest; callers cannot add or replace an entry and there is no parallel
path list in the toolchain. It canonicalizes the root and every target without
opening source content and rejects escapes, duplicates, symlinks, non-regular
files, or an embedded-manifest hash mismatch. The test driver constructs this
capability before opening any source, then reads only the matched path into
`Base.Source`. The private compile entry rechecks `Base.Filename` and the
source digest against that selected capability entry before constructing its
`SourceManager`; it creates the immutable parse authorization and
`ParserConfig{CompilerStdlib}` before lexing or parsing. Immediately after a
successful parse it requires the declared ModuleIdentity and exact private
leaf/resource inventory to equal the selected entry, then creates the
`VerifiedCompilerStdlibSource` seal passed to TypeChecker. It never mutates
provenance after parsing. A null, foreign, stale, digest-mismatched, wrong-
identity, or wrong-inventory capability fails before semantic admission and
discards the untrusted AST. Public `compile()` always parses
with `Ordinary` provenance, even for a reserved module name or custom sysroot;
neither the CLI nor `CompileRequest` can select a bootstrap mode.

The normal pipeline performs parse → v2 catalog/typecheck → SemanticModel →
`lowerModule` to `GenericPreparationInput` → CompleteOnly session A/
`runDerivations`/destroy A → fresh CompleteOnly session B/
`prepareInterfaceAbiRoots`/store token/destroy B →
`Prepared = prepareGenericModule(std::move(Input), Catalog, Cache)`. If an
interface is requested, it next builds
`buildCompleteInterfaceModule(Prepared.RuntimeModule,
*Prepared.InterfaceRoots, Prepared.LocalDefinitions)` and stores that
artifact before moving the module. Only then does it call
`runTypedIrPipeline(std::move(Prepared.RuntimeModule))`, consume
`PipelineOutput::Ir`, and emit the remaining requested artifacts. Unsupported syntax or a bad interface
is a diagnostic; no legacy fallback occurs. `Object | Interface` is the normal
module request and both derive from the same preparation result and verified
runtime module.

LLVM lowering takes `const typed_ir::Module &` after the pipeline has produced
`LlvmReady`; it does not consume, mutate, or renumber that module. For a
combined `TypedIr | LlvmIr | Object` request, lower all LLVM/object artifacts
from that exact reference first and only then move the same module into
`CompilationArtifacts::TypedIr`. A TypedIr-only request moves it directly;
an LLVM-only request destroys it after lowering. No deep clone or second pass
run is permitted. A regression requests every combination, compares the
TypeTable domain/FunctionIds and printed IR, and proves the LLVM/object ABI
metadata was derived from the identical verified module.

Private bootstrap requests are deliberately separate. `EmitSkeleton` and
`CompleteAgainstSkeleton` require `Emit == Interface`; either combined with
TypedIr/LLVM/Object/Accelerator output is rejected before parsing. A cyclic SCC
must use the batch API—single-module `compileCanonicalStdlibForBootstrap` with
`EmitSkeleton` is rejected—because no member may derive a peer declaration
before every peer name and binder exists.

`emitCanonicalStdlibSkeletonBatch` performs this exact all-or-nothing
algorithm: (1) authenticate every SCC path and LF-normalized digest against the
immutable manifest before opening/parsing, then parse each with its immutable
`CompilerStdlib` authorization; (2) validate module identity and exact
leaf/resource inventory; (3) transactionally predeclare every module, nominal,
resource, trait, value, generic name, and declaration binder for all members in
one combined provisional SCC environment; for each destination TypeTable,
create a read-only `CompleteOnly` catalog session over predecessor Complete
interfaces and that member's immutable provisional local seed, and layer the
separate in-memory SCC peer environment over the predecessor lookup; (4)
resolve only explicitly annotated exported and cross-edge signatures against
that combined lookup—never bodies,
derivation, inferred unrelated exports, runtime descriptors, or
specialization—then destroy every predecessor session; (5) construct each member's destination-local TypeTable,
`SkeletonDeclarationModule`, including its destination-local TypeTable,
`SemanticInterfaceSeed`, and one `GenericDeclarationSeed` per annotated
generic; and (6) call `sealSkeletonInterfaceRoots`, then invoke
`buildSkeletonInterfaceModule` with that sealed token for every member,
validate the whole set, then commit
all outputs together. Any missing cross-edge annotation, duplicate, ambiguity,
open free binder, session/remap failure, or member failure rolls back every
provisional table/seed and writes nothing. It creates no
`BootstrapSkeletonDeclarations` session and never imports a peer Skeleton
through the catalog; only predecessor Complete interfaces use the read-only
sessions above. It creates no derivation, extraction, specialization, or
runtime module. A regression makes one Skeleton signature mention both a type
from a predecessor Complete interface and a type declared by a later same-SCC
peer, then proves both resolve and all sessions die before root sealing.

`CompleteAgainstSkeleton` loads predecessor Complete interfaces plus the
entire staged SCC Skeleton set for name resolution/typechecking. It fully
typechecks and lowers one member to Canonical, populating the same outer
GenericDeclarationSeeds; creates BootstrapSkeletonDeclarations session A
through its expected result for
`runDerivations` and destroys A; creates fresh session B for
`prepareInterfaceAbiRoots` through its expected result, stores its token, and
destroys B; then calls
`Extraction = extractGenericDefinitions(Input)`. It builds Complete with
`buildCompleteInterfaceModule(Input.Canonical, *Extraction.InterfaceRoots,
Extraction.LocalDefinitions)`, whose exact seed/definition join
attaches every fragment. It never calls `prepareGenericModule`, specializes a
local/imported body, creates a runtime module, or runs the runtime Typed IR
pipeline. The generator validates every completed member against all Skeleton
outer declarations before atomically publishing the entire Complete SCC.
Ordinary compilation loads only Complete interfaces. Tests cover a generic
A↔B cross-edge, phantom binders, a peer declared later in manifest order,
missing/mismatched outer signatures, a body error found only during Complete,
and rollback with no partial Skeleton or Complete files.

A separate ordinary public-`compile` runtime-pipeline test uses
`CollectPhaseTrace=true` and requires, in order,
`EffectPrepared`,
`ClosureConverted`, `OperationInstantiated`, and `EffectOutlined`. Snapshot
the FunctionId and `CallableDescriptorId` sets at `ClosureConverted` and prove
they are unchanged through `LlvmReady`; operation instantiation may fill only
their previously empty runtime-row slots (the trace counts change from zero to
the exact reachable totals), and effect finalization may rewrite only prepared
CFG/region records. Also prove trace-off returns an empty vector and byte-
identical final IR.
Each call owns its mutable catalog/pass/LLVM state and is safe to invoke
concurrently with another request; returned sources outlive every diagnostic
and LLVM modules are destroyed before their contexts. CLI-local `--explain`,
argument parsing, executable launch, and REPL interaction remain outside this
API; the CLI uses `compile` plus `link`, while the REPL requests Typed IR/LLVM
for one expression through the same compilation path.
`WarningOptions` preserves `--Wall`, `--Wextra`, `-w`, `--Werror`,
`--Wincomplete-patterns`, and `--Woverlapping-patterns`; the three checker
switches preserve `--Wno-refinement`, `--Wno-linear`, and
`--Wno-linear-leak`. `RequireEffectFree`, `CheckStyleOnly`, and the TypedCore
artifact preserve their existing CLI modes. Add a table-driven test mapping
every current flag into one request and asserting diagnostics/artifacts; the
only intentional removal is the redundant
`--emit-accelerator-report-with-types`, because all replacement accelerator
reports are already post-semantics.

- [ ] **Step 7: Add deterministic temporary interface generation**

Build the repository-only `yona-stdlib-interface-generator` target when
`YONA_BUILD_BOOTSTRAP_TOOLS=ON`. CMake defines that option with
`${BUILD_TESTING}` as its default, so every developer/CI test configuration
builds it without relying on a hidden preset; packaging configurations set it
OFF, and the explicit bootstrap/check commands set it ON. Never
install or link it into `yonac`. Implement:

```bash
cmake --preset x64-debug-linux -DYONA_BUILD_BOOTSTRAP_TOOLS=ON
cmake --build --preset build-debug-linux \
  --target yona-stdlib-interface-generator -j2
python3 scripts/regenerate_interfaces.py \
  --compiler out/build/x64-debug-linux/yona-stdlib-interface-generator \
  --output-root out/build/x64-debug-linux/interfaces-v2
python3 scripts/regenerate_interfaces.py \
  --compiler out/build/x64-debug-linux/yona-stdlib-interface-generator \
  --output-root out/build/x64-debug-linux/interfaces-v2 --check
```

The script requires exactly the 45 canonical sources embedded from
`lib/stdlib-manifest.toml`: `Prelude.yona`, the existing 27 Std Yona
implementations (including `Std/Constants/*`), and Task 15's 17 new canonical
sources, including the three mixed array implementation modules. Every source
and private leaf was already finalized in Task 15; generation is read-only and
must not rewrite a `.yona` file to make it compile. It constructs the compiler's
immutable canonical-stdlib capability from that embedded inventory before
opening any file, rejects duplicate identity, path escape, symlink retargeting,
source/interface/doc-publication drift, or leaf mismatch, and invokes the internal
bootstrap entry that alone attaches `CompilerStdlib` provenance. This entry is
not present on `CompileRequest`, the CLI, or the public compiler API.
It parses imports/declaration headers without loading checked-in v1, builds the
complete import graph (including the language's implicit edge from every Std
module to Prelude), and computes deterministic SCCs. Process an acyclic
singleton directly after all predecessor Complete interfaces exist—its
inferred exports come from ordinary typechecking, never an invented skeleton
signature. For a genuinely cyclic SCC only, require explicit structural
signatures on every value referenced across the cycle, emit Skeleton v2
declarations—including ADT constructors and trait methods—for that SCC into a
fresh temporary root, then run `CompleteAgainstSkeleton` for every member
against the entire staged Skeleton set with v1 fallback disabled. This phase
typechecks and lowers all members to Canonical, builds Complete local fragments
and interface seeds, and performs no runtime specialization. Validate the
whole Complete set—including cross-edge generic fragments and imported-to-local
declaration merges—in a second temporary root, then atomically replace the
SCC. No consumer can observe a mixture of Skeleton and Complete members.
The cyclic A/B fixture makes each exported generic call the other's generic:
each member's TIRF must contain the other as a declaration-only Symbol while
completion runs against Skeletons; after the Complete set is atomically
installed, an ordinary third consumer specializes A, resolves B's body from
the Complete catalog, and closes the mutual SCC exactly once. Missing producer
bodies before that installation are rejected rather than guessed.
Report the exact cycle and every missing signature before writing any
skeleton. Missing modules and any non-bootstrap diagnostic fail immediately.

The current effective
`{Prelude, Std\Dict, Std\Iterator, Std\List, Std\Set, Std\String}` SCC is a
required bootstrap regression: Prelude directly imports those five modules
and each receives the language's implicit Prelude edge. Only values actually
referenced across those cycle edges need explicit signatures, and their
existing/new declaration-source signatures plus Prelude ADT/trait declarations
are sufficient. Assert all six members and every cross-cycle signature; do not
require annotations on unrelated exports merely because they share the SCC.

After all SCCs complete, the script rejects remaining Skeleton artifacts,
parses all 45 outputs as v2, verifies that source/interface identities are a
bijection, writes exactly one canonical `stdlib-leaf-contracts.json` from the
same privileged projections, validates its manifest/source bijection and
schema, and compares all interfaces plus that sidecar against a second clean
generation byte-for-byte. The sidecar stays under the requested build output
root and is never copied into `lib/`. It never
replaces checked-in v1 files before Task 17.

After temporary interfaces exist, add a `CompilerPipelineTest` compiling the
source-level `Std\IntArray.fromSeq/map` square example against that root. The
fallback target must resolve to the Yona `Std\IntArray.map` Definition or its
specialization, never a NativeExtern; assert the same `IntMapSquare` decision.
The pass-level red test above remains
independent of interface bootstrap ordering.

Add a no-input `workflow_dispatch` trigger to the multi-platform workflow in
this still-test-only task. The push trigger remains restricted to master/main;
Task 17 uses the manual trigger to validate its temporary branch without
permanently broadening automatic branch builds.
Update `scripts/test-arm64-qemu.sh` so filtered invocations run the in-container
test binary through `scripts/run-focused-tests.py`; its unfiltered form still
runs the complete executable directly.

- [ ] **Step 8: Enforce replacement runtime boundaries**

`typed_ir_runtime_boundary_contract.py` accepts named `--build-dir`,
`--interface-root`, mandatory `--leaf-contracts`, `--artifact-root`, and optional repeated `--artifact`
arguments, resolves the target's
LLVM symbol-inspection tool, and scans replacement LLVM declarations,
temporary v2 interfaces, and generated objects. It rejects legacy closure,
exception, frame, SJLJ, post-insert heap-mask, raw async callback, and GENFN
source symbols. It also rejects every undefined reference or LLVM declaration
to the old raw array accelerator kernels (`YonaStdGpuRawMapAdd`,
`YonaStdGpuRawMapMul`, `YonaStdGpuRawMapSquare`, both raw Int filters/reduces,
the raw Float map/reduce family, and every
`YonaRuntimeGpuVulkanTry*Int64` array-layout entry). Their definitions may
remain in the frozen oracle runtime archive until Task 17, so this pre-cutover
contract scopes the check to replacement artifacts and does not mistake
coexistence for use. Symbol tools prove only symbol presence/absence. Exact ABI
shape is separately compiled in `ReplacementAbiConformance.cpp`, which
iterates Task 15's embedded leaf rows and exhaustively partitions them:
CheckedDirectV2 symbols assign to `YonaAbiCheckedDirectNativeEntryV2`;
SemanticIntrinsic/RuntimeEntry symbols assign to their generated exact typed
function pointers; CheckedOutcomeV2 symbols assign to their exact Outcome
pointers; StableExternal symbols assign to `double (*)(double)`; and both
SemanticIntrinsic/CompilerPlan rows (`CancellationCheck` and `TaskSpawn`) must
have no native fields, symbol, registry entry, or callable pointer and validate
only their compiler-plan discriminants. It also verifies the declared owning header/source and
checks enum/size/align/offset constants. Missing, extra, multiply defined, or
legacy symbols fail before object execution. LLVM lowering emits deterministic
`!yona.abi.contracts` metadata containing each called symbol, canonical
structural fingerprints, parameter ownerships, result ownership, async kind,
Outcome opcode, and Raised constraint; the contract compares that metadata
with the emitted/called-symbol subset of `stdlib-leaf-contracts.json`, excluding
CompilerPlan rows, not the public v2 slice. Register both as CTest tests
with the `ci-contract` label so the existing native workflow's ordinary
`ctest` step cannot omit them. Register interface regeneration/check and the
golden v2 hash as another `ci-contract`, using the configuration's
`yona-stdlib-interface-generator`, so every native Debug checkpoint verifies
portable bytes rather than merely parsing a checked-in fixture.

- [ ] **Step 9: Run the full local pre-cutover gate**

```bash
python3 scripts/run-focused-tests.py ./out/build/x64-debug-linux/tests \
  -tc='Typed accelerator*,Typed IR debug info*,Compiler pipeline*,ABI matrix*,Typed IR callables*'
python3 scripts/regenerate_interfaces.py \
  --compiler out/build/x64-debug-linux/yona-stdlib-interface-generator \
  --output-root out/build/x64-debug-linux/interfaces-v2 --check
python3 test/CMake/typed_ir_runtime_boundary_contract.py \
  --build-dir out/build/x64-debug-linux \
  --interface-root out/build/x64-debug-linux/interfaces-v2 \
  --leaf-contracts out/build/x64-debug-linux/interfaces-v2/stdlib-leaf-contracts.json \
  --artifact-root out/build/x64-debug-linux/typed-ir-test-artifacts
python3 scripts/quality.py --build-dir out/build/x64-debug-linux \
  --preset x64-debug-linux --jobs 2 sanitize
python3 scripts/quality.py --build-dir out/build/x64-debug-linux \
  --preset x64-debug-linux --jobs 2 vulkan
scripts/test-arm64-qemu.sh \
  -tc='*ABI matrix*,*ownership*,*effect*,*async*,*interface*,*accelerator*'
ctest --preset unit-tests-linux --output-on-failure
git diff --check
```

Expected: every matrix cell passes at O0-O3, temporary interfaces are stable,
sanitizers/Vulkan/QEMU pass, and replacement artifacts have zero legacy
references while the frozen oracle remains present elsewhere.

- [ ] **Step 10: Commit the completed parallel backend**

```bash
git add include/yona/TypedIr src/TypedIr include/yona/Codegen/Llvm \
  src/Codegen/Llvm include/yona/Toolchain/CompilerPipeline.h \
  src/Toolchain/CompilerPipeline.cpp \
  src/Toolchain/CompilerPipelineInternal.h \
  include/yona/Semantics/AcceleratorDiag.h \
  src/Semantics/AcceleratorDiag.cpp test/TypedIr test/Codegen \
  include/yona/Runtime/Gpu/Api.h include/yona/Runtime/Gpu/VulkanDevice.h \
  src/Runtime/Gpu/Cpu.c src/Runtime/Gpu/Stub.c \
  src/Runtime/Gpu/VulkanDevice.c src/Runtime/Gpu/VulkanOperations.c \
  test/Runtime/ReplacementAbiConformance.cpp \
  test/Runtime/GpuTypedKernelTest.cpp \
  test/Toolchain/CompilerPipelineTest.cpp \
  test/Toolchain/CrossModulePipelineTest.cpp \
  tools/stdlib-interface-generator/main.cpp \
  test/CMake/typed_ir_runtime_boundary_contract.py \
  scripts/regenerate_interfaces.py scripts/check_architecture.py \
  scripts/test-arm64-qemu.sh \
  .github/workflows/cmake-multi-platform.yml \
  cmake/YonaComponents.cmake CMakeLists.txt docs/gpu-transparent-lowering.md \
  docs/typed-ir.md docs/quality.md
git add docs/superpowers/plans/2026-09-01-typed-ir-codegen-rewrite.md
git commit -m "feat: complete parallel typed ir backend"
```

- [ ] **Step 11: Push the still-test-only backend and require native Debug**

The thread already authorizes master pushes. Verify the branch and clean
state, push this non-production checkpoint, then wait for the workflow run for
that exact commit:

```bash
test "$(git branch --show-current)" = master
test -z "$(git status --short)"
git push origin master
commit=$(git rev-parse HEAD)
run_id=""
for attempt in $(seq 1 30); do
  run_id=$(gh run list --commit "$commit" \
    --workflow cmake-multi-platform.yml --limit 20 --json databaseId \
    --jq '.[0].databaseId')
  test -n "$run_id" && break
  sleep 2
done
test -n "$run_id"
gh run watch "$run_id" --exit-status
```

Require green Debug jobs for Linux x64, Linux ARM64, macOS ARM64, Windows x64,
and Windows ARM64, including replacement backend tests and the portable
runtime-boundary contract. Do not begin Task 17 on a native failure; append
its exact repro to `docs/todo-list.md`, fix it in the parallel backend, rerun
the local gates, push, and require the new exact commit to pass.

After success, record Step 11's exact SHA/run/jobs in this plan and push a
plan-only `docs: record typed ir pre-cutover validation` closeout. Task 17
starts from that clean master; the validated production files remain exactly
those of the green parent commit.

### Task 17: Atomically switch production and delete the legacy compiler/ABI

**Files:**

- Modify: `cli/Main.cpp`
- Modify: `repl/Main.cpp`
- Modify: `cmake/YonaComponents.cmake`
- Modify: `cmake/YonaTools.cmake`
- Modify: `cmake/YonaPackageTools.cmake`
- Modify: `CMakeLists.txt`
- Modify: `scripts/quality.py`
- Modify: `scripts/gendocs.py`
- Modify: `scripts/regenerate_interfaces.py`
- Modify: `scripts/ci/smoke_yls_yona.py`
- Modify: `.cursor/rules/keep-docs-up-to-date.mdc`
- Modify: `.cursor/rules/project-guidance.mdc`
- Modify: `lib/Std/README.md`
- Modify: `site/README.md`
- Modify: `site/AGENTS.md`
- Modify: `site/scripts/sync-stdlib.mjs`
- Modify: `tools/yona/main.yona`
- Modify: `tools/yls/main.yona`
- Create: `test/CMake/no_legacy_backend_contract.py`
- Create: `test/CMake/documentation_cutover_contract.py`
- Create: `test/CMake/gendocs_contract.py`
- Create: `test/CMake/stdlib_source_cutover_contract.py`
- Modify: `test/stdlib/manifest.md`
- Create: `test/stdlib/pure/Stream_test.yona`
- Create: `test/stdlib/pure/Stream_test.expected`
- Create: `test/Fixtures/TypedIr/Cutover/all_boundaries.yona`
- Create: `test/Fixtures/TypedIr/Cutover/all_boundaries.expected`
- Delete: `test/Fixtures/LegacyOracle/Prelude.yona`
- Delete: `test/Fixtures/LegacyOracle/Std/Regex.yona`
- Create: `docs/superpowers/plans/2026-09-01-typed-ir-cutover-paths.txt`
- Modify: `test/CMake/native_arm64_ci_packaging_contract.py`
- Modify: `test/CMake/InstalledConsumer/RunInstalledConsumer.cmake`
- Modify: `test/CMake/InstalledConsumer/hello.yona`
- Modify: `.github/workflows/cmake-multi-platform.yml`
- Modify: `.github/workflows/docs-site.yml`
- Delete: `include/yona/Codegen/Codegen.h`
- Delete: `include/yona/Codegen/CodegenSession.h`
- Delete: `include/yona/Codegen/AcceleratorLowering.h`
- Delete: `include/yona/Codegen/EscapeAnalysis.h`
- Delete: `include/yona/Codegen/LastUseAnalysis.h`
- Delete: `include/yona/Codegen/TypedIrLowering.h`
- Delete: `include/yona/Codegen/DeriveEngine.h`
- Delete: `src/Codegen/Codegen.cpp`
- Delete: `src/Codegen/CodegenApply.cpp`
- Delete: `src/Codegen/CodegenCase.cpp`
- Delete: `src/Codegen/CodegenCollections.cpp`
- Delete: `src/Codegen/CodegenEffects.cpp`
- Delete: `src/Codegen/CodegenExpr.cpp`
- Delete: `src/Codegen/CodegenFunction.cpp`
- Delete: `src/Codegen/CodegenModule.cpp`
- Delete: `src/Codegen/CodegenSession.cpp`
- Delete: `src/Codegen/CodegenUtils.cpp`
- Delete: `src/Codegen/AcceleratorLowering.cpp`
- Delete: `src/Codegen/EscapeAnalysis.cpp`
- Delete: `src/Codegen/LastUseAnalysis.cpp`
- Delete: `src/Codegen/TypedIrLowering.cpp`
- Delete: `src/Codegen/DeriveEngine.cpp`
- Delete: `include/yona/Semantics/GenericFunctionSource.h`
- Delete: `src/Semantics/GenericFunctionSource.cpp`
- Delete: `include/yona/Semantics/InterfaceCatalog.h`
- Delete: `src/Semantics/InterfaceCatalog.cpp`
- Modify: `src/Lsp/Analysis.cpp`
- Modify: `include/yona/Syntax/Ast.h`
- Modify: `src/Syntax/ParserModule.cpp`
- Modify: `src/Semantics/TypeChecker.cpp`
- Modify: `include/yona/Semantics/SemanticModel.h`
- Modify: `src/Semantics/SemanticModel.cpp`
- Modify: `src/TypedCore/Analyze.cpp`
- Modify: `fuzz/Interface/InterfaceFuzzer.cpp`
- Modify: `fuzz/Corpus/Interface/adt.yonai`
- Modify: `fuzz/Corpus/Interface/function.yonai`
- Modify: `fuzz/Corpus/Interface/generic_function.yonai`
- Delete: `include/yona/Interface/Module.h`
- Delete: `include/yona/Interface/Reader.h`
- Delete: `include/yona/Interface/Writer.h`
- Delete: `src/Interface/Module.cpp`
- Delete: `src/Interface/Reader.cpp`
- Delete: `src/Interface/Writer.cpp`
- Delete: `include/yona/Runtime/Platform/SjLj.h`
- Delete: `include/yona/Runtime/Core/Value.h`
- Delete: `src/Runtime/Core/Value.c`
- Delete: `src/Runtime/Core/Closures.c`
- Delete: `src/Runtime/Core/Exceptions.c`
- Modify: `include/yona/Runtime/Core/Api.h`
- Modify: `src/Runtime/Core/Internal.h`
- Modify: `src/Runtime/Core/Runtime.c`
- Delete: `include/yona/Runtime/Concurrency/Async.h`
- Delete: `src/Runtime/Concurrency/AsyncPosix.c`
- Delete: `src/Runtime/Concurrency/AsyncWin32.c`
- Modify: `include/yona/Runtime/Concurrency/Channel.h`
- Modify: `include/yona/Runtime/Gpu/Api.h`
- Modify: `include/yona/Runtime/Gpu/VulkanDevice.h`
- Modify: `include/yona/Runtime/Platform/Api.h`
- Modify: `include/yona/Runtime/Platform/IoContext.h`
- Modify: `include/yona/Runtime/Platform/IoUring.h`
- Modify: `include/yona/Runtime/Platform/Kqueue.h`
- Modify: `include/yona/Runtime/Platform/Windows.h`
- Modify: `include/yona/Runtime/Collections/Sequence.h`
- Modify: `include/yona/Runtime/Collections/Set.h`
- Modify: `include/yona/Runtime/Collections/Dictionary.h`
- Modify: `include/yona/Runtime/Collections/Arrays.h`
- Modify: `include/yona/Runtime/Codecs/Json.h`
- Modify: `include/yona/Runtime/Codecs/Regex.h`
- Modify: `include/yona/Runtime/Codecs/Utf16.h`
- Modify: `src/Runtime/Collections/Sequence.c`
- Modify: `src/Runtime/Collections/DictionarySet.c`
- Modify: `src/Runtime/Collections/Hamt.c`
- Modify: `src/Runtime/Collections/HamtInternal.h`
- Modify: `src/Runtime/Collections/Arrays.c`
- Modify: `src/Runtime/Codecs/Json.c`
- Modify: `src/Runtime/Codecs/Regex.c`
- Modify: `src/Runtime/Codecs/Utf16.c`
- Modify: `src/Runtime/Stdlib/Iterator.c`
- Modify: `src/Runtime/Stdlib/Native.c`
- Modify: `src/Runtime/Platform/FileLinux.c`
- Modify: `src/Runtime/Platform/FileMacOs.c`
- Modify: `src/Runtime/Platform/FileWindows.c`
- Modify: `src/Runtime/Platform/IoContext.c`
- Modify: `src/Runtime/Platform/IoUringLinux.c`
- Modify: `src/Runtime/Platform/KqueueMacOs.c`
- Modify: `src/Runtime/Platform/NetLinux.c`
- Modify: `src/Runtime/Platform/NetMacOs.c`
- Modify: `src/Runtime/Platform/NetWindows.c`
- Modify: `src/Runtime/Platform/OsLinux.c`
- Modify: `src/Runtime/Platform/OsMacOs.c`
- Modify: `src/Runtime/Platform/OsWindows.c`
- Modify: `src/Runtime/Concurrency/ChannelPosix.c`
- Modify: `src/Runtime/Concurrency/ChannelWin32.c`
- Modify: `src/Runtime/Gpu/Stub.c`
- Modify: `src/Runtime/Gpu/Cpu.c`
- Modify: `src/Runtime/Gpu/VulkanDevice.c`
- Modify: `src/Runtime/Gpu/VulkanOperations.c`
- Modify: `test/Runtime/JsonAbiTest.cpp`
- Modify: `test/Runtime/IoReadExactTest.cpp`
- Modify: `test/Runtime/HamtRcTest.cpp`
- Modify: `test/Runtime/RuntimeGuardsTest.cpp`
- Modify: `test/Runtime/GpuStubTest.cpp`
- Modify: `test/Runtime/GpuVulkanDeviceTest.cpp`
- Modify: `test/Runtime/GpuVulkanMapAddTest.cpp`
- Modify: `test/Runtime/TaskOwnershipTest.cpp`
- Modify: `test/Runtime/IoFallbackLeakProbe.c`
- Modify: `test/Runtime/NetRuntimeTest.cpp`
- Modify: `test/Runtime/Utf16AbiTest.cpp`
- Modify: `fuzz/Runtime/RegexFuzzer.cpp`
- Modify: `fuzz/Runtime/UtfCodecFuzzer.cpp`
- Modify: `test/stdlib/codecs/Json_test.yona`
- Modify: `test/stdlib/codecs/Regex_test.yona`
- Modify: `test/stdlib/codecs/ByteArray_test.yona`
- Modify: `test/Fixtures/Codegen/stdlib_bytes.yona`
- Modify: `test/Fixtures/Codegen/stdlib_bytes_concat.yona`
- Modify: `test/Fixtures/Codegen/stdlib_bytes_slice.yona`
- Modify: `test/Fixtures/Codegen/foldl_iterator.yona`
- Modify: `test/Fixtures/Codegen/stream_bracket.yona`
- Modify: `test/Fixtures/Codegen/iterator_gen_lines.yona`
- Modify: `test/Fixtures/Codegen/binary_chunks.yona`
- Modify: `test/Fixtures/Codegen/binary_seek.yona`
- Modify: `test/Fixtures/Codegen/binary_write_read.yona`
- Modify: `test/Fixtures/Codegen/iterator_file_lines.yona`
- Modify: `test/Fixtures/Codegen/linear_file_case.yona`
- Modify: `test/Fixtures/Codegen/stdlib_io_readexact.yona`
- Modify: `test/Fixtures/Codegen/stdlib_json_get_import_length.yona`
- Modify: `test/Fixtures/Codegen/stdlib_file.yona`
- Modify: `test/Fixtures/Codegen/stdlib_file_read.yona`
- Modify: `test/Fixtures/Codegen/stdlib_file_write.yona`
- Modify: `test/Fixtures/Codegen/stdlib_io.yona`
- Modify: `test/Fixtures/Codegen/stdlib_process.yona`
- Modify: `test/Fixtures/Codegen/stdlib_process_afn_in_let.yona`
- Modify: `test/Fixtures/Codegen/stdlib_process_exec.yona`
- Modify: `test/Fixtures/Codegen/stdlib_process_executable_path.yona`
- Modify: `test/Fixtures/Codegen/stdlib_process_getargs.yona`
- Modify: `test/Fixtures/Codegen/stdlib_process_pid.yona`
- Modify: `test/Fixtures/Codegen/stdlib_process_readline.yona`
- Modify: `test/Fixtures/Codegen/stdlib_process_run.yona`
- Modify: `test/Fixtures/Codegen/stdlib_process_spawn.yona`
- Modify: `test/Fixtures/Codegen/stdlib_process_stdin.yona`
- Modify: `test/Fixtures/Codegen/stdlib_process_tempfile.yona`
- Modify: `test/Fixtures/Codegen/stdlib_process_version.yona`
- Modify: `test/Fixtures/Codegen/stdlib_process_wait.yona`
- Modify: `test/Fixtures/Codegen/channel_basic.yona`
- Modify: `test/Fixtures/Codegen/channel_capacity.yona`
- Modify: `test/Fixtures/Codegen/channel_deadlock_recv.yona`
- Modify: `test/Fixtures/Codegen/channel_deadlock_send.yona`
- Modify: `test/Fixtures/Codegen/channel_heap_payload.yona`
- Modify: `test/Fixtures/Codegen/channel_pipeline.yona`
- Modify: `test/Fixtures/Codegen/channel_spawn.yona`
- Modify: `test/Fixtures/Codegen/channel_starvation_compensation.yona`
- Modify: `test/Fixtures/Codegen/channel_try_recv.yona`
- Modify: `test/Fixtures/Codegen/channel_try_recv_empty.yona`
- Modify: `test/Fixtures/Codegen/gpu_float_channel.yona`
- Modify: `test/Fixtures/Codegen/gpu_pinned_backend.yona`
- Modify: `test/Fixtures/Codegen/gpu_map_float_pinned.yona`
- Modify: `test/Fixtures/Codegen/closure_captures_linear.yona`
- Create: `test/Fixtures/Codegen/iterator_file_open_error.yona`
- Create: `test/Fixtures/Codegen/iterator_file_open_error.expected`
- Create: `test/Fixtures/Codegen/iterator_file_read_error.yona`
- Create: `test/Fixtures/Codegen/iterator_file_read_error.expected`
- Create: `test/Fixtures/Codegen/iterator_file_early_drop.yona`
- Create: `test/Fixtures/Codegen/iterator_file_early_drop.expected`
- Modify: `test/stdlib/runtime/Channel_test.yona`
- Modify: `test/stdlib/runtime/io_test.yona`
- Modify: `test/stdlib/network/Net_test.yona`
- Modify: `test/stdlib/runtime/File_test.yona`
- Modify: `test/stdlib/runtime/Process_test.yona`
- Modify: `test/stdlib/foundation/Traits_test.yona`
- Modify: `test/Toolchain/YonaScriptTest.cpp`
- Modify: `test/TypedCore/AbiTest.cpp`
- Modify: `test/Syntax/ImportedConstructorParserTest.cpp`
- Modify: `test/Semantics/SemanticModelTest.cpp`
- Modify: `bench/concurrency/channel_fanin.yona`
- Modify: `bench/concurrency/channel_pipeline.yona`
- Modify: `bench/concurrency/channel_throughput.yona`
- Modify: `bench/io/binary_read_chunks.yona`
- Modify: `bench/io/binary_write_read.yona`
- Modify: `bench/io/file_parallel_read.yona`
- Modify: `bench/io/file_parallel_read_large.yona`
- Modify: `bench/io/file_read.yona`
- Modify: `bench/io/file_read_large.yona`
- Modify: `bench/io/file_readlines.yona`
- Modify: `bench/io/file_readlines_large.yona`
- Modify: `bench/io/file_write_read.yona`
- Modify: `bench/io/file_write_read_large.yona`
- Modify: `bench/io/process_exec.yona`
- Modify: `bench/io/process_spawn.yona`
- Modify: `bench/accelerators/gpu_pinned_scale_hot.yona`
- Modify: `bench/README.md`
- Delete: `test/Codegen/TypedIrLoweringTest.cpp`
- Delete: `test/Codegen/AcceleratorLoweringTest.cpp`
- Delete: `test/Interface/InterfaceFileTest.cpp`
- Modify: `test/Codegen/CodegenTest.cpp`
- Modify: `test/Codegen/AdtTest.cpp`
- Modify: `test/Codegen/GenericAbiTest.cpp`
- Modify: `test/Codegen/PatternOwnershipTest.cpp`
- Modify: `test/Codegen/RepeatedOwnershipTest.cpp`
- Modify: `test/Interface/PreludeInterfaceTest.cpp`
- Modify: `test/Semantics/InterfaceCatalogTest.cpp`
- Modify: `test/Semantics/TypeCheckerTest.cpp`
- Modify: `test/Semantics/TraitTest.cpp`
- Modify: `test/Support/SemanticSetup.h`
- Modify: `test/Support/SemanticSetupTest.cpp`
- Modify: all `lib/**/*.yonai` files
- Modify: `INSTALL.md`
- Modify: `README.md`
- Modify: `CONTRIBUTING.md`
- Modify: `AGENTS.md`
- Modify: `CHANGELOG.md`
- Modify: `docs/todo-list.md`
- Modify: `docs/async.md`
- Modify: `docs/arm64-qemu.md`
- Modify: `docs/auto-derive.md`
- Modify: `docs/design-gpu-async.md`
- Modify: `docs/design-borrow-types.md`
- Modify: `docs/design-opaque-exported-types.md`
- Modify: `docs/effects.md`
- Modify: `docs/error-codes.md`
- Modify: `docs/extern-decl.md`
- Modify: `docs/interface-v2-format.md`
- Modify: `docs/gpu-architecture.md`
- Modify: `docs/gpu-transparent-lowering.md`
- Modify: `docs/gpu-vulkan-implementation-plan.md`
- Modify: `docs/memory-management.md`
- Modify: `docs/module-system.md`
- Modify: `docs/pattern-matching.md`
- Modify: `docs/platform-architecture.md`
- Modify: `docs/platform-windows.md`
- Modify: `docs/prelude.md`
- Modify: `docs/quality.md`
- Modify: `docs/refinement-types.md`
- Modify: `docs/row-polymorphism.md`
- Modify: `docs/structured-concurrency.md`
- Modify: `docs/stdlib-plan.md`
- Modify: `docs/array-trait.md`
- Modify: `docs/channels.md`
- Modify: `docs/iterators.md`
- Modify: `docs/language-syntax.md`
- Modify: `docs/linear-types.md`
- Modify: `docs/persistent-data-structures.md`
- Modify: `docs/style-guide.md`
- Modify: `docs/sum-types.md`
- Modify: `docs/traits.md`
- Modify: `docs/type-checker-design.md`
- Modify: `docs/type-introspection.md`
- Modify: `docs/type-system-plan.md`
- Modify: `docs/type-system-status.md`
- Modify: `docs/typed-core.md`
- Modify: `docs/typed-ir.md`
- Modify: `docs/api/Bool.md`
- Modify: `docs/api/ByteArray.md`
- Modify: `docs/api/Channel.md`
- Modify: `docs/api/Collection.md`
- Modify: `docs/api/Convert.md`
- Modify: `docs/api/Crypto.md`
- Modify: `docs/api/Dict.md`
- Modify: `docs/api/Encoding.md`
- Modify: `docs/api/File.md`
- Modify: `docs/api/FloatArray.md`
- Modify: `docs/api/Format.md`
- Modify: `docs/api/Function.md`
- Modify: `docs/api/Gpu.md`
- Modify: `docs/api/Http.md`
- Modify: `docs/api/IntArray.md`
- Modify: `docs/api/Io.md`
- Modify: `docs/api/Iterator.md`
- Modify: `docs/api/Json.md`
- Modify: `docs/api/List.md`
- Modify: `docs/api/Log.md`
- Modify: `docs/api/Math.md`
- Modify: `docs/api/Net.md`
- Modify: `docs/api/Option.md`
- Modify: `docs/api/Pair.md`
- Modify: `docs/api/Parallel.md`
- Modify: `docs/api/Path.md`
- Modify: `docs/api/Process.md`
- Modify: `docs/api/Random.md`
- Modify: `docs/api/Range.md`
- Modify: `docs/api/Regex.md`
- Modify: `docs/api/Result.md`
- Modify: `docs/api/Set.md`
- Create: `docs/api/Stream.md`
- Modify: `docs/api/String.md`
- Modify: `docs/api/Task.md`
- Modify: `docs/api/Test.md`
- Modify: `docs/api/Time.md`
- Modify: `docs/api/TraitLaws.md`
- Modify: `docs/api/Tuple.md`
- Modify: `docs/api/Types.md`
- Modify: `docs/api/Utf16.md`
- Modify: `docs/api/README.md`
- Create: `docs/api/generated-files.txt`
- Modify: `site/src/content/docs/agents.md`
- Modify: `site/src/content/docs/index.mdx`
- Modify: `site/src/content/docs/why-yona.md`
- Modify: `site/src/content/docs/learn/quick-start.md`
- Modify: `site/src/content/docs/learn/collections.md`
- Modify: `site/src/content/docs/learn/functions.md`
- Modify: `site/src/content/docs/learn/pattern-matching.md`
- Modify: `site/src/content/docs/learn/syntax.md`
- Modify: `site/src/content/docs/learn/effects.md`
- Modify: `site/src/content/docs/learn/concurrency.md`
- Modify: `site/src/content/docs/learn/modules.md`
- Modify: `site/src/content/docs/learn/style.md`
- Modify: `site/src/content/docs/learn/types.md`
- Modify: `site/src/content/docs/guides/concurrency.md`
- Modify: `site/src/content/docs/guides/editor.md`
- Modify: `site/src/content/docs/guides/iterators.md`
- Modify: `site/src/content/docs/guides/memory.md`
- Modify: `site/src/content/docs/guides/modules-interfaces.md`
- Modify: `site/src/content/docs/guides/performance.md`
- Modify: `site/src/content/docs/guides/persistent-data-structures.md`
- Modify: `site/src/content/docs/guides/traits.md`
- Modify: `site/src/content/docs/guides/accelerators.md`
- Modify: `site/src/content/docs/guides/type-system.md`
- Modify: `site/src/content/docs/reference/specification.md`
- Modify: `site/src/content/docs/reference/cli.md`
- Modify: `site/src/content/docs/reference/cmake.md`
- Modify: `site/src/content/docs/reference/error-codes.md`
- Modify: `site/src/content/docs/reference/prelude.md`
- Modify: this plan

Generated but intentionally not staged: `pnpm sync` deletes and recreates the
entire ignored `site/src/content/docs/stdlib/` tree from tracked `docs/api`
inputs and rewrites ignored
`site/generated/llms/{llms,llms-small,llms-full}.txt`; the site build invokes
that sync. `scripts/gendocs.py` itself regenerates the tracked `docs/api`
inventory as the exact 41 module pages plus `docs/api/README.md` recorded in
`docs/api/generated-files.txt`. The documentation contract below
scans every generated stdlib page and all three generated LLM text artifacts
after generation, while only their tracked source inputs enter the cutover
manifest/commit.

Every generated module page and the generated API README begins with the same
deterministic HTML provenance comment naming schema `YONAI 2` and renderer
`yonac --emit-doc-signatures`; it contains no absolute path, timestamp, or
compiler-build ID. This intentionally changes all 40 existing module pages
and README during cutover while Stream is added, making the cutover TSV's exact
`M`/`A` status map reproducible even when an individual public signature did
not otherwise change. `gendocs_contract.py` requires the header exactly once
in every one of the 42 generated Markdown files and proves the second run is
byte-identical.

**Interfaces:**

- Consumes: the completely green replacement pipeline and v2 artifacts.
- Produces: one production `CompilerPipeline`, one runtime ABI, one v2
  interface format, no legacy source/object symbols, and current docs/site.
- Atomicity: production wiring, interface regeneration, runtime cleanup,
  legacy deletion, and no-legacy contract land in this one commit.

- [ ] **Step 1: Add the red no-legacy contract before deleting anything**

Task 16 must be committed, pushed, native-green, and the worktree clean.
Create a temporary validation branch so the atomic cutover commit can receive
native checks before the exact commit is fast-forwarded to master:

```bash
test "$(git branch --show-current)" = master
test -z "$(git status --short)"
git switch -c typed-ir-cutover-validation
```

The Python contract must assert deleted paths are absent; source contains none
of these tokens; every checked-in interface begins `YONAI 2`; and a generated
object/executable/runtime archive has no defined or undefined legacy symbol:

```python
FORBIDDEN_TOKENS = (
    'yona/Codegen/Codegen.h', 'GenericFunctionSource',
    'GENFN_BEGIN', 'GENFN_END', 'GENFN_DEP', 'GENFN_CTOR',
    'YONA_SJLJ', 'llvm.eh.sjlj', 'YonaRuntimeTry', 'YonaRuntimeRaise',
    'YonaRuntimeFrame', 'YonaRuntimeClosure', 'YonaAsyncFunction',
    'YonaRuntimeAsyncCall', 'YonaRuntimeSequenceSetHeap',
    'YonaRuntimeSetSetHeap', 'YonaRuntimeDictionarySetHeap',
    'YonaRuntimeSequenceAllocate', 'YonaRuntimeSequenceSet(',
    'YonaRuntimeAdtSetHeapMask', 'YonaRuntimeTupleSetHeapMask',
    'YonaRuntimeClosureSetHeapMask', 'YONA_RC_TYPE_ADT',
    'YONA_ADT_HDR_SIZE', 'ADT_HDR_SIZE',
    'YonaRuntimeCurrentException', 'YonaRuntimeTaskGroupArena',
    'YonaRuntimeTaskAwait(', 'YonaRuntimeTaskAwaitKeep(',
    'YonaRuntimeTaskComplete(', 'YonaTaskRef', 'YonaTaskGroupRef',
    'YonaTypeDescriptor',
    'YonaRuntimeTypeDescriptor', 'yona/Runtime/Core/Value.h',
    'yona/Semantics/InterfaceCatalog.h', 'setjmp', 'longjmp', 'jmp_buf',
    'YonaRuntimeArray', 'YonaRuntimeByteArray', 'YonaRuntimeIntArray',
    'YonaRuntimeFloatArray', 'YonaRuntimeSequence', 'YonaRuntimeSet',
    'YonaRuntimeDictionary', 'YonaRuntimeFoldl', 'YonaRuntimeFoldr',
    'YonaRuntimeStringConcatenate',
    'YonaRuntimeUtf8OffsetToUtf16Line',
    'YonaRuntimeUtf8OffsetToUtf16Character',
    'YonaRuntimeUtf16PositionToUtf8Offset',
    'YonaStdUtf16OffsetToLine', 'YonaStdUtf16OffsetToCharacter',
    'YonaStdUtf16PositionToOffset',
    'YonaRuntimeGpuVulkanTry', 'YonaStdGpuRaw',
    'YONA_HAMT_FLAG_KEY_HEAP', 'YONA_HAMT_FLAG_VALUE_HEAP',
    'YONA_HAMT_FLAG_IS_SET', 'yonaRuntimeHamtFlags',
    'yonaRuntimeHamtAddFlags', 'YonaRuntimeHamtStampAuxiliaryFlags',
)
RUNTIME_SOURCE_ONLY_FORBIDDEN_TOKENS = (
    'YonaStdIteratorFromSeq', 'YonaStdIteratorFromByteArray',
    'YonaStdIteratorFromIntArray', 'YonaStdIteratorFromFloatArray',
    'YonaStdIteratorNextNative',
)
FORBIDDEN_LEGACY_OPERATOR_SYMBOLS = (
    'YonaRuntimeSequencePrepend', 'YonaRuntimeSequenceAppend',
    'YonaRuntimeSequenceJoin', 'YonaRuntimeSequenceContains',
    'YonaRuntimeSequenceDifference', 'YonaRuntimeSetContains',
    'YonaRuntimeSetDifference', 'YonaRuntimeDictionaryContains',
)
```

Register it as a `ci-contract` CTest and rewrite the ARM64 contract from
positive SJLJ assertions to the same absence contract. The contract compiles
`all_boundaries.yona`, which exercises closures, collections, complete
exceptions, effects, async, and an imported generic, then inspects its LLVM
IR, object, executable, and all runtime archives using the target-appropriate
symbol tool and suffix. `Prelude.o` alone is not sufficient.

Use the platform symbol tool in both defined-only and undefined-only modes;
apply all three inventories to C runtime/header source and to both symbol sets
of every runtime archive. Apply the globally safe `YonaRuntime*`, raw GPU, and
HAMT inventories to generated IR/objects/executables as well. Do not globally
reject a public `YonaStdIterator*` definition in a generated Yona module: those
spellings may be legitimate module-mangled replacement Definitions, whereas
they are forbidden in the C runtime and its archives. Replacement
`YonaRuntimeAbi*` symbols do not match any unversioned prefix above.
the ABI conformance TU and LLVM ownership metadata remain the signature proof.
The cutover path manifest is UTF-8 TSV with one literal
`A|M|D<TAB>repository-relative-path` row per authorized change, sorted by path;
directories, globs, pathspec magic, duplicate paths, rename shorthand, and
untracked omissions are invalid. It includes every deletion, both
LegacyOracle snapshots, all 45 literal interface paths, all 42 generated API
Markdown paths plus `generated-files.txt`, both workflow files, the manifest
itself, and this rewrite plan. The contract compares the worktree's exact
name/status map with that table and rejects a missing, extra, renamed, or
wrong-status path before staging. `--emit-pathspec0` emits only those validated
literal paths as NUL-delimited stdout; `--check-cached` requires the cached
name/status map to equal the same table exactly. Thus the authorization table
is also executable without pretending its status column is a Git pathspec.

- [ ] **Step 2: Run the contract and capture the expected legacy inventory**

```bash
python3 test/CMake/no_legacy_backend_contract.py \
  --build-dir out/build/x64-debug-linux
```

Expected: failure listing legacy Codegen, v1 interfaces, SJLJ, closures,
exceptions, async callbacks, and heap-mask symbols.

- [ ] **Step 3: Switch every production consumer together**

Make `cli/Main.cpp` compile through `toolchain::compile(CompileRequest)` and
link through `toolchain::link(LinkRequest)`; make the REPL request expression
artifacts from the same compilation path. CLI-local argument parsing,
`--explain`, and executable launch remain CLI responsibilities. Add
`--emit-typed-ir`; keep
`--emit-ir`, `--emit-obj`, interface/object linking, diagnostics, debug,
optimization, accelerator, module paths, and sysroot behavior on the new
pipeline. Make accelerator reports always semantic/Typed IR-based and delete
the redundant `--emit-accelerator-report-with-types` option.
Update `bench/README.md` in the same cutover so every example uses the one
surviving `--emit-accelerator-report` mode and no benchmark instruction can
reintroduce the deleted flag.

Make CMake's Prelude artifact, `yona`, installed consumers, and package tools
use the same new `yonac`. Make LSP semantic-only but replace its import reader
with `TypedInterfaceCatalog`. Switch the remaining production, typed-core/LSP,
and fuzzer callers to Task 15's `TypedImportSource`/v2 catalog, then remove the
v1 reader; do not add another structural-to-inference bridge.

Compile `tools/yona/main.yona` and `tools/yls/main.yona` against the final
stdlib API in the same gate. The runner uses `Stdin`/typed File resources and
handles every File/Io Result explicitly while preserving `-`, `-e`, script,
argv0, and exit-status behavior. YLS uses `Stdin` and `Stdout`, reads exact
Content-Length bytes as ByteArray, strictly decodes JSON text, writes ByteArray
frames, and turns Io/Convert failures into one protocol diagnostic/clean exit;
it never unwraps a numeric fd or silently substitutes empty input. Runner and
JSON-RPC integration tests cover short frames, EOF, malformed UTF-8, broken
pipe, and successful multi-message framing.

Expand the registered `scripts/ci/smoke_yls_yona.py` harness itself to cover
all five cases, including chunked short writes/reads and the child's clean
diagnostic/exit behavior when its output pipe is closed. The harness must
assert every process terminates within its timeout and no malformed frame or
partial success is accepted; `smoke_yls_yona` remains a registered CTest.

Update `docs/arm64-qemu.md` at the same time: replace its deleted raw-channel
test filter with the exact surviving `*Channel resource ABI*` case.
`documentation_cutover_contract.py` invokes test discovery with that literal
filter and requires it to select at least one case, so the guide cannot retain
a green command that silently runs zero tests. Both this guide and the YLS
harness are literal rows in `typed-ir-cutover-paths.txt`.

- [ ] **Step 4: Migrate behavioral tests without weakening coverage**

Replace direct `Codegen` construction with `CompilerPipeline`, pass- or
lowerer-level APIs. Preserve every executable fixture, trait/generic case,
IR-shape assertion that still expresses a public invariant, ownership counter,
debug test, and module/interface test. Delete only bootstrap/v1-format tests
whose exact replacement already exists.

Migrate the literal File/Iterator/Channel/Io/Net sources and benchmarks listed
in this task together: File acquisition pattern-matches `Ok handle` (not
`Linear`), and every Result has explicit Ok/Err behavior. Public `readLines`
consumers use `Stream (Result String (FileError | ConvertError))` and Stream
combinators; public `readChunks` consumers use
`Iterator (Result ByteArray FileError)` and Iterator consumers. An iterator
that owns a moved FileHandle is never followed by a manual close. Channel create returns the
direct `(Sender a, Receiver a)` resource pair; retained endpoint aliases are
legal and every owner is released. Replace the synthetic
`closure_captures_linear` fixture with an Iterator-backed linear capture so it
still tests a real noncopyable owner. Numeric-invalid Net tests become
wrong-resource/closed-Socket tests through the typed ABI harness. The three new
file-stream/iterator fixtures inject open failure, mid-stream read failure,
and early drop: the public text Stream asserts one Err then Nil, the raw
iterator-level ABI test asserts Err then None, and both paths assert exactly
one close. Source fixtures never call a private raw leaf. Preserve unchanged
expected stdout files rather than editing them gratuitously; any genuinely
changed expected file must be added as its own literal TSV row before staging.
The benchmark sources are compiled in the cutover gate (not exempted) and use
`do` for effects rather than `let _ = effect`.

Port `GpuVulkanMapAddTest.cpp` to Task 16's typed V2 tri-state API and
descriptor-owned buffers; it may not retain a call to a deleted legacy Vulkan
symbol. Remove `NodeSemantics::InferredType`/display-effect truth from
SemanticModel and update its C++/TypedCore/parser tests to assert structural
TypeId/EffectRowId and ResourceType ownership. Old tests whose sole purpose was
constructing the deleted user-visible `Linear` wrapper are replaced by the
Task 15 resource-linearity tests, not weakened into unrestricted values.

Register `stdlib_source_cutover_contract.py` as a `ci-contract`. It enumerates
every tracked noncanonical `.yona` source under tools, tests/fixtures, and
benchmarks and compiles each with the replacement compiler and temporary v2
root; intentionally invalid diagnostic fixtures are not skipped, but carry an
exact expected-diagnostic classification. Its sorted discovered inventory is
asserted, and a companion table classifies every path as compile-only,
executable-with-expected-output, or expected-diagnostic. A new unclassified
source fails the contract.

In addition to compilation, module-aware checks cover every deliberate source
break. File/Io/Channel/Net/Process reject `Linear`, integer handles, stale
stdin/stdout fd names, unchecked changed Results, old channel tuples, and old
Iterator element assumptions. ByteArray rejects removed `toString` and old set
shapes; Iterator rejects constructor/pattern use; Stream freezes the zero-
argument acquire and Unit-returning release shape for `bracket`; GPU freezes
the public `Buffer` identity plus PinnedFloats Consume/republish ownership.
Prelude checks reject removed File/Linear identities. The literal
`stream_bracket.yona` migration changes acquire to the zero-argument/Unit
convention and release to `()`, retaining stdout where semantics do not
change. The two pinned GPU fixtures and pinned-scale benchmark are mandatory
inventory rows. Executable fixtures still run against their expected output,
while benches/tools compile plus their dedicated behavior tests. Every changed
consumer is an exact row in the cutover TSV.

Update `test/stdlib/manifest.md` as part of that executable inventory: replace
the obsolete `Std.Io` numeric-descriptor contract with typed Stdin/Stdout,
byte reads, and tty/flush behavior, and add exactly one `Std.Stream` row backed
by `test/stdlib/pure/Stream_test.yona` plus its expected output. That suite
exercises construction, map/filter/take/fold, and bracket release, so all 41
public API modules again have an executable public-surface suite. The
manifest, Stream source, and expected output are literal cutover-path rows.

- [ ] **Step 5: Delete the legacy compiler, v1 interface, and runtime ABI**

Delete the files listed above and remove them from CMake. Remove from surviving
runtime files every old closure layout/header index, TLS exception state,
SJLJ call, fixed-size frame, raw async function/context variant, int64 task
await/completion, and post-insert heap-mask API. Keep unhandled formatting only
through `YonaRuntimeReportUnhandled` and descriptor formatters.

Do not edit canonical `.yona` semantics during this cutover: Task 15 already
removed Prelude's ten array externs, moved instances and pure traversal to the
owning modules, changed Iterator to semantic intrinsics, finalized Stream, and
made every private leaf match the embedded manifest. Assert those source and
manifest hashes before deleting the two frozen LegacyOracle snapshots. Then
delete only the obsolete legacy C exports/aliases and v1 artifacts that the
already-native-green replacement source no longer references; a source change
here invalidates Task 16's bootstrap/conformance checkpoint and must be moved
back to Task 15 and revalidated before proceeding.

Migrate every surviving aggregate consumer in the same cutover.
`Collections/Hamt.c` builds key sequences through the descriptor-first bulk
sequence API; `Collections/Arrays.c` deletes every Sequence bridge and raw
map/fold/filter callback surface and retains only Task 11's thirteen
callback-free V2 storage leaves. Array combinators and conversions live in the
three canonical Yona modules, and no C array entry invokes a Yona callable.
`Codecs/Json.c` constructs Json/result ADTs, object tuples, and
sequences through immutable generated descriptors and complete
`YonaAbiValue`/outcome contracts; `Codecs/Regex.c` returns descriptor-backed
String sequences (including nested match sequences); and `Gpu/Cpu.c` builds
buffer/result ADTs through the uniform aggregate API. Delete the raw sync GPU
array kernel surface (`YonaStdGpuRawMapAdd/MapMul/MapSquare`, both raw Int
filters and reductions, the raw Float map/reduce family, and all
`YonaRuntimeGpuVulkanTry*Int64` entries). Transparent selection keeps only
Task 16's eight tri-state V2 kernels; any still-public explicit GPU feature is
ported to an ABI-distinct descriptor/outcome entry or removed with its Yona
API. `VulkanOperations.c` accepts only internal pointer/count scratch spans and
never a legacy Yona array layout. Their headers and
`lib/Std/{Json,Regex}.yona` externs expose only the ABI-distinct typed
signatures. No consumer writes a tag/count/mask/header word or child slot
directly, and no child is stored before its descriptor-derived ownership
metadata exists. Focused HAMT/JSON/Regex/GPU tests force allocation failure,
nested owned children, empty results, and teardown; the Regex fuzzer calls the
new ABI and releases every outcome.

Retire the second Utf16 surface in the same cutover. Remove the three scalar
legacy codec externs
`YonaRuntimeUtf8OffsetToUtf16Line`,
`YonaRuntimeUtf8OffsetToUtf16Character`, and
`YonaRuntimeUtf16PositionToUtf8Offset`, plus all three `YonaStdUtf16*`
aliases, from `Runtime/Codecs/Utf16.{h,c}`. Keep only the two length-aware codec
routines as private/internal substrate (renamed or given internal linkage so
they are not Yona-facing ABI); Task 15's typed
`Runtime/Stdlib/Utf16.{h,c}` leaves are the sole public runtime boundary.
Port `Utf16AbiTest.cpp` and `UtfCodecFuzzer.cpp` to those typed V2 leaves or the
internal length-aware helper while preserving malformed/truncated/embedded-NUL
coverage. Add all six removed names to the forbidden runtime-source and symbol
inventories, and list the codec header/source, test, and fuzzer literally in
`typed-ir-cutover-paths.txt`.
`ReplacementAbiConformance` and the no-legacy symbol inventory include every
one of those thirteen surviving Arrays entries, assign each symbol to its
exact distinct-ref prototype, cross-check the matching intrinsic
FunctionType/ownership/effect/linkage record, and reject every old
`YonaStd*Array{Map,Foldl,Filter,...}` or
`YonaRuntime*Array{Map,FoldLeft,Filter,FromSequence,ToSequence,...}` symbol,
raw callback signature, and `YonaRuntimeSequenceAllocate/Set`; both Arrays paths are explicit in
`typed-ir-cutover-paths.txt`.
`IoReadExactTest.cpp` constructs FileHandle values through the same immutable
descriptor-first aggregate builder and closes them through the typed
replacement FileHandle Outcome entry; it contains no
`YonaRuntimeAdtAllocate`/`AdtSet*` or legacy int64 close call. Include that
exact test path in `typed-ir-cutover-paths.txt` and retain its ownership/error
coverage.
Port `IoFallbackLeakProbe.c` to the descriptor-backed ByteArray leaves and
Task 14 Outcome submit/await path while retaining its forced io_uring fallback
and leak assertions. Port `NetRuntimeTest.cpp` to the dedicated platform
Outcome functions and release every task/outcome; port
`GpuVulkanDeviceTest.cpp` from `YonaStdGpuRawHasGpu` to the surviving typed
capability probe. Delete only a legacy subcase whose replacement test is named
and covers the same failure/ownership invariant.

In addition to the token scan, `no_legacy_backend_contract.py` applies a
path-specific raw-layout scan to every runtime consumer outside the canonical
Core/Aggregate/collection implementation. It rejects legacy allocate/set
pairs, direct metadata-index writes, legacy `int64_t *` aggregate-returning
externs, or a raw RC type-tag allocation. This catches Regex-style `Seq[1]`
metadata writes even when no `*SetHeap` spelling is present.

Remove the temporary first-clause projection and display-string semantic truth
from `FunctionExpr`/`NodeSemantics`. Rename no `v2` API back to a compatibility
alias; the versioned namespace may remain explicit.

- [ ] **Step 6: Regenerate every checked-in interface atomically**

Reconfigure after production wiring/deletion and build both production
`yonac` and the repository-only bootstrap generator without requesting the
still-v1 Prelude artifact. Then generate to a temporary tree with the explicit
generator, validate every file, and atomically replace all 45 checked-in
`lib/**/*.yonai` files. Run twice:

```bash
cmake --preset x64-debug-linux -DYONA_BUILD_BOOTSTRAP_TOOLS=ON
cmake --build --preset build-debug-linux \
  --target yonac yona-stdlib-interface-generator --parallel 2
python3 scripts/regenerate_interfaces.py \
  --compiler out/build/x64-debug-linux/yona-stdlib-interface-generator
python3 scripts/regenerate_interfaces.py \
  --compiler out/build/x64-debug-linux/yona-stdlib-interface-generator --check
```

Expected: second run is byte-identical and every first line is `YONAI 2`.
Replace every valid interface fuzz corpus (`adt`, `function`, and
`generic_function`) with minimized v2 artifacts produced by the same writer;
keep separate malformed/version-rejection corpus inputs only when their
filenames and fuzzer assertions explicitly identify them as invalid.
CLI separation tests invoke both binaries: the bootstrap generator rejects
`--emit-doc-signatures` and every ordinary compile/emission mode, while
production `yonac` rejects any bootstrap/canonical-stdlib-authority option and
cannot construct `CompilerStdlib` provenance. Their `--help` inventories are
disjoint accordingly. Only `yonac` owns documentation rendering; only the
repository tool owns authenticated SCC interface generation.

- [ ] **Step 7: Update docs, site, changelog, and outstanding-work state**

Remove all descriptions of shallow/identity continuations, GENFN source,
SJLJ/TLS frames, integer-only async callbacks, normal-only `with` cleanup,
post-insert collection flags, or direct AST-to-LLVM lowering. Document the
structural type/IR pipeline, descriptors, outcomes, v2 rebuild diagnostic,
all-outcome finalization, explicit cancellation contexts, deep one-shot
resumption semantics (including handler/return-clause bypass), and
`--emit-typed-ir`. In particular, correct the
public specification's current statement that a `with` finalizer is skipped
on exceptions. Replace `docs/typed-core.md`'s deleted
`Semantics/InterfaceCatalog.h` link with `Semantics/TypedInterfaceCatalog.h`;
the exact path is included in `typed-ir-cutover-paths.txt`. Update `README.md`,
`CONTRIBUTING.md`, and `AGENTS.md` together
with the internal/public site inventory.
Also update every tracked command surface that invokes documentation
generation: `lib/Std/README.md`, `site/README.md`, `site/AGENTS.md`, both
`.cursor/rules/*.mdc` files listed above, and `site/scripts/sync-stdlib.mjs`.
Each user-facing command passes the built production `yonac` explicitly to
`scripts/gendocs.py --compiler`; the sync script's missing-input diagnostic
prints that exact command instead of the obsolete argument-free form. These
files, `bench/README.md`, and `test/stdlib/manifest.md` are literal entries in
the cutover path manifest rather than implicit documentation exceptions.
`lib/Std/README.md` must also describe all 45 manifest-authenticated canonical
`.yona` sources and their private typed leaves, never a class of “`.yonai`
only” C modules. `.cursor/rules/project-guidance.mdc` is rewritten around the
semantic -> Typed IR -> LLVM pipeline, v2 structural fragments/resources, and
the new Prelude—no direct AST-to-LLVM, GENFN source metadata, or public Linear
wrapper. `site/AGENTS.md` states that source comments provide prose while
public signatures and types come from Complete v2 through `yonac`; comments
alone are not documentation authority.
The documentation contract also rejects current claims built around legacy
`CType::` execution tags, `transferred_maps_`/`transferred_seqs_`, numeric
tagged-tuple Sum encodings, flat ADT layouts, the old five-second channel wait
heuristic, affirmative use of `let _ = effect` for sequencing, “Codegen
unchanged,” deleted paths,
name-derived trait mangling, raw `io_await` IDs, or GENFN/source-body travel
and reparsing. Rewrite E0402 in both error-code references as a failure at the
semantic-to-Typed-IR lowering boundary, not an AST node the deleted Codegen
cannot compile.
Rewrite `docs/stdlib-plan.md`'s live implementation notes to use
descriptor-first aggregate construction, typed outcome failures, and the
Typed IR pipeline; remove its raw ADT setters, sentinel/null error contracts,
flat Dict/Set layout, and legacy Codegen references. Include that exact path
in the cutover manifest rather than excluding it as history.

Rewrite the three user-facing language pages explicitly. The functions guide
describes descriptor-backed callables and one shared recursive-group
environment, never weak-self closure cycles. The pattern-matching guide states
the complete supported matrix—constructor, tuple, sequence/head-tail, dict,
record, as, and or-patterns—and their ownership/exhaustiveness behavior rather
than calling as/dict handling partial. The syntax guide documents structural
Sum, opaque `resource`, and Promise annotations, while marking `intrinsic` as
compiler-stdlib-only syntax whose highlighting grants no user authority.
Current Symbol documentation describes canonical Symbol descriptors/bytes and
structural equality, never dense compile-time intern IDs or integer switches.
Rewrite the README's stdlib table explicitly: Net exposes `tcpConnect` and
`tcpAccept`; Channel Sender/Receiver are typed AlwaysShareable resource
endpoints; and Task exposes only `spawn` plus `checkCancellation`, never a
public `await` function. Io is described as portable typed standard-stream
operations whose blocking leaves use the worker pool, not as universally
non-blocking io_uring calls.

Remove all now-completed rewrite blocker entries (including the Linux release
Prelude abort if the release gate below proves it fixed) from
`docs/todo-list.md`; retain only genuinely unfinished work. Add one detailed
`Unreleased` changelog entry and mark only this cutover commit's Steps 1-10
current. Steps 11-12 remain pending until native validation and fast-forward;
their exact SHA/run/result bookkeeping lands afterward in the explicitly
authorized plan-only closeout commit.
This includes the compiler-aware API-extraction TODO only after the following
contract is green.

Expose Task 15's renderer as
`yonac --emit-doc-signatures <complete-v2.yonai>`. Parse this mutually
exclusive mode before loading source or accepting any compilation emission
flag. It emits exactly the canonical one-line JSON object on stdout; all
diagnostics go to stderr and failure is nonzero. `scripts/gendocs.py` now
requires `--compiler`, reads the exact `publish_api = true` inventory from
`lib/stdlib-manifest.toml`, and invokes
`[compiler, "--emit-doc-signatures", interface]` without a shell for every
one of the 41 modules. Delete its v1 `FN|IO|AFN|NAT` parser, `pretty_arrow`,
name/type heuristics, and invented generic fallback. Source comments remain
the documentation prose, but the complete signature always comes from the
compiler JSON. The generator consumes both sorted top-level arrays:
`types` renders every public nominal/resource/trait declaration and determines
the README type count, while `functions` renders every public closed/generic
function and determines the function count; neither array may be ignored or
reconstructed from comments. Missing/extra/duplicate exports,
schema/version/module mismatch,
Skeleton input, or a source module missing after leading blank header lines is
fatal.

Generate all output in a temporary sibling tree, validate it, write
`docs/api/generated-files.txt` with exactly the 41 sorted module Markdown
paths plus `README.md`, and atomically replace the tracked tree. `--check`
performs the same clean generation and rejects a missing, extra, stale, or
byte-different tracked output without mutation. The newly visible
`docs/api/Stream.md` contains exactly 32 public functions and one public type;
its README row is
`| [Std.Stream](Stream.md) | 32 | 1 | Std\Stream — lazy pull-based sequences. |`.
With the public GPU surface preserved by Task 15 and every deliberate API
delta above, the generator emits and the manifest contract freezes this exact
41-element function-count vector in manifest order:

```text
Bool=7 ByteArray=14 Channel=8 Collection=9 Convert=11 Crypto=4 Dict=9
Encoding=7 File=21 FloatArray=11 Format=1 Function=8 Gpu=42 Http=11
IntArray=15 Io=15 Iterator=7 Json=11 List=31 Log=6 Math=21 Net=14
Option=10 Pair=9 Parallel=2 Path=6 Process=34 Random=4 Range=11 Regex=7
Result=11 Set=9 Stream=32 String=28 Task=2 Test=7 Time=6 TraitLaws=6
Tuple=9 Types=5 Utf16=3
```

Its deterministic sum is 484 public functions. This is the auditable delta
from the current 439/40 inventory: Stream +32, Process +11, Io -3, Net +2,
ByteArray -1, List +2, String +1, and Task +1. No count is inferred from a prose
fallback or optional export.
`gendocs_contract.py` verifies the exact 42-file
inventory, Stream signatures/counts/description, total, missing/extra JSON
signature rejection, blank-header handling, and a byte-identical second check.

Update `docs-site.yml` to use Ubuntu 26.04 and the repository's
`setup-llvm` action, configure/build `yonac`, run this same compiler-backed
generation in `--check` mode, and only then build the site. Run API and site
generation:

```bash
python3 scripts/gendocs.py \
  --compiler out/build/x64-debug-linux/yonac
python3 scripts/gendocs.py \
  --compiler out/build/x64-debug-linux/yonac --check
python3 test/CMake/gendocs_contract.py \
  --compiler out/build/x64-debug-linux/yonac
(cd site && pnpm install --frozen-lockfile && pnpm build)
python3 test/CMake/documentation_cutover_contract.py \
  --root . --exclude docs/superpowers/plans --exclude docs/superpowers/specs
```

The site build's sync must recreate the complete ignored stdlib tree and all
three ignored LLM text artifacts before the contract scan. The contract scans
those derived outputs as well as tracked docs/site pages; tracked Yona sources,
`docs/api` outputs, and hand-written site inputs—not ignored derivatives—are
the committed source of truth.

The documentation contract uses path/section-aware affirmative-claim patterns
and explicit negative-example allowances, not global literal bans. It must
accept “do not use `let _ = effect`” and marked historical/contrast wording
while rejecting “use `let _ = effect` to sequence”; the contract test contains
both cases. It likewise distinguishes a prohibited current flat-ADT claim from
a sentence explaining that flat ADTs were removed. It rejects current-doc/site references to direct
AST-to-LLVM, GENFN source reparsing, SJLJ/TLS frames, legacy async callbacks,
old post-insert heap metadata, dense/interned Symbol IDs, weak-self recursive
closures, and partial as/dict-pattern claims while deliberately excluding
historical plans/specs. Its current-truth inventory is discovered from
`git ls-files`, not maintained as another hand-written subset: `README.md`,
`CONTRIBUTING.md`, `AGENTS.md`, `INSTALL.md`, every tracked nonhistorical
`docs/**/*.md` (excluding `docs/superpowers/{plans,specs}` and the explicitly
historical `docs/benchmark-results*.md` snapshots), tracked `docs/api`, and
every hand-written tracked `site/src/content/docs/**/*.{md,mdx}`, plus the
regenerated ignored stdlib/LLM outputs. The test freezes the sorted exclusion
set and fails if a new tracked current-doc path is skipped. For `CHANGELOG.md`
it scans only the new `Unreleased`
section; released history remains immutable and may mention SJLJ, GENFN,
setjmp, or heap masks. Contract fixtures prove such historical text and the
negative `let _ = effect` guidance pass while equivalent affirmative current
claims fail.

Command-surface validation additionally scans `lib/Std/README.md`,
`site/{README,AGENTS}.md`, `.cursor/rules/keep-docs-up-to-date.mdc`,
`.cursor/rules/project-guidance.mdc`, `site/scripts/sync-stdlib.mjs`, and
`bench/README.md`; stdlib-suite validation reads `test/stdlib/manifest.md`.
It rejects an argument-free gendocs command, the removed accelerator report
flag, a missing public module suite, or the old numeric-Io contract. Its
path-specific assertions also reject `.yonai`-only/C-runtime module
classification in `lib/Std/README.md`, direct AST-to-LLVM/GENFN/Prelude
Linear guidance in `project-guidance.mdc`, and comments-only signature
authority in `site/AGENTS.md`. README-specific fixtures reject the old
`Net.connect`/`Net.accept`, linear-channel-endpoint, and `Std\Task.await`
claims, plus the nonportable “non-blocking via io_uring” Io row, while
requiring the replacement rows above.

- [ ] **Step 8: Run the exact final local verification sequence**

```bash
./scripts/format.sh
cmake --preset x64-debug-linux
cmake --build --preset build-debug-linux --parallel 2
ctest --preset unit-tests-linux --output-on-failure
ctest --preset unit-tests-linux -R '^smoke_yls_yona$' \
  --output-on-failure --no-tests=error
python3 scripts/run-focused-tests.py ./out/build/x64-debug-linux/tests \
  -tc='*ABI matrix*,*ownership*,*effect*,*async*,*interface*,*accelerator*,*debug info*'
for contract in no_legacy_backend_contract \
                native_arm64_ci_packaging_contract \
                installed_consumer_contract; do
  ctest --preset unit-tests-linux -R "^${contract}$" \
    --output-on-failure --no-tests=error
done
python3 scripts/regenerate_interfaces.py \
  --compiler out/build/x64-debug-linux/yona-stdlib-interface-generator --check
python3 scripts/gendocs.py \
  --compiler out/build/x64-debug-linux/yonac --check
python3 test/CMake/gendocs_contract.py \
  --compiler out/build/x64-debug-linux/yonac
python3 test/CMake/documentation_cutover_contract.py \
  --root . --exclude docs/superpowers/plans --exclude docs/superpowers/specs
python3 scripts/quality.py architecture
python3 scripts/quality.py --build-dir out/build/x64-debug-linux \
  --preset x64-debug-linux --jobs 2 sanitize
python3 scripts/quality.py --build-dir out/build/x64-debug-linux \
  --preset x64-debug-linux --jobs 2 vulkan
scripts/test-arm64-qemu.sh \
  -tc='*ABI matrix*,*ownership*,*effect*,*async*,*interface*,*accelerator*'
scripts/test-arm64-qemu.sh
cmake --preset x64-release-linux
cmake --build --preset build-release-linux --parallel 2
ctest --test-dir out/build/x64-release-linux --output-on-failure
python3 scripts/quality.py --build-dir out/build/x64-debug-linux \
  --preset x64-debug-linux --jobs 2 quality
python3 scripts/quality.py --build-dir out/build/x64-debug-linux format-check
git diff --check
```

Expected: all commands pass; release Prelude generation no longer aborts;
Fedora 44 ARM64 QEMU passes; generated programs and interfaces contain no
legacy dependency.

- [ ] **Step 9: Re-run the no-legacy source and object inspection**

```bash
python3 test/CMake/no_legacy_backend_contract.py \
  --build-dir out/build/x64-debug-linux \
  --fixture test/Fixtures/TypedIr/Cutover/all_boundaries.yona
if rg -n 'SjLj|YONA_SJLJ|llvm\.eh\.sjlj|GenericFunctionSource|GENFN_(BEGIN|END|DEP|CTOR)|YonaRuntime(Try|Raise|Frame|Closure)|YonaAsyncFunction|YonaTask(Group)?Ref|YonaTypeDescriptor|YonaRuntimeTypeDescriptor|YonaRuntimeTask(Await|Complete)|YonaRuntime(SequenceAllocate|SequenceSet|AdtSetHeapMask|TupleSetHeapMask|ClosureSetHeapMask)|YONA_(RC_TYPE_ADT|ADT_HDR_SIZE)|Runtime/Core/Value\.h|Semantics/InterfaceCatalog\.h|setjmp|longjmp|jmp_buf' \
  include src cli repl lib cmake CMakeLists.txt; then
  exit 1
fi
if rg -n 'YonaRuntime(Array|ByteArray|IntArray|FloatArray|Sequence|Set|Dictionary|Foldl|Foldr|StringConcatenate)|YonaRuntimeGpuVulkanTry|YonaStdGpuRaw|YONA_HAMT_FLAG_(KEY_HEAP|VALUE_HEAP|IS_SET)|yonaRuntimeHamt(Flags|AddFlags)|YonaRuntimeHamtStampAuxiliaryFlags' \
  include src cli repl lib cmake CMakeLists.txt; then
  exit 1
fi
if rg -n 'YonaStdIterator(FromSeq|FromByteArray|FromIntArray|FromFloatArray|NextNative)' \
  include src; then
  exit 1
fi
```

Expected: the portable contract passes for the representative executable and
all linked archives; the source search prints no forbidden production
reference.

- [ ] **Step 10: Commit the atomic production cutover**

```bash
python3 test/CMake/no_legacy_backend_contract.py \
  --build-dir out/build/x64-debug-linux \
  --fixture test/Fixtures/TypedIr/Cutover/all_boundaries.yona \
  --allowed-paths docs/superpowers/plans/2026-09-01-typed-ir-cutover-paths.txt
python3 test/CMake/no_legacy_backend_contract.py \
  --build-dir out/build/x64-debug-linux \
  --fixture test/Fixtures/TypedIr/Cutover/all_boundaries.yona \
  --allowed-paths docs/superpowers/plans/2026-09-01-typed-ir-cutover-paths.txt \
  --emit-pathspec0 | git add --pathspec-from-file=- --pathspec-file-nul
git diff --cached --check
python3 test/CMake/no_legacy_backend_contract.py \
  --build-dir out/build/x64-debug-linux \
  --fixture test/Fixtures/TypedIr/Cutover/all_boundaries.yona \
  --allowed-paths docs/superpowers/plans/2026-09-01-typed-ir-cutover-paths.txt \
  --check-cached
git diff --cached --name-status
git commit -m "refactor: replace codegen with verified typed ir"
```

- [ ] **Step 11: Validate the exact atomic commit on every native platform**

```bash
test "$(git branch --show-current)" = typed-ir-cutover-validation
test -z "$(git status --short)"
git fetch origin master
test "$(git rev-parse master)" = "$(git rev-parse origin/master)"
test "$(git rev-list --count master..HEAD)" -eq 1
test "$(git rev-parse HEAD^)" = "$(git rev-parse master)"
git push -u origin typed-ir-cutover-validation
commit=$(git rev-parse HEAD)
gh workflow run cmake-multi-platform.yml \
  --ref typed-ir-cutover-validation
run_id=""
for attempt in $(seq 1 30); do
  run_id=$(gh run list --commit "$commit" \
    --workflow cmake-multi-platform.yml --limit 20 --json databaseId \
    --jq '.[0].databaseId')
  test -n "$run_id" && break
  sleep 2
done
test -n "$run_id"
gh run watch "$run_id" --exit-status
```

Require Debug and Release jobs for Linux x64, Linux ARM64, macOS ARM64,
Windows x64, and Windows ARM64 in `.github/workflows/cmake-multi-platform.yml`.
Do not use local QEMU as a substitute for native macOS/Windows object-format,
debug-info, calling-convention, or runtime validation. The portable no-legacy
contract and `regenerate_interfaces.py --check` run in every one of the ten
jobs. Each job configures `YONA_BUILD_BOOTSTRAP_TOOLS=ON`, builds both
configuration executables, and passes its
`yona-stdlib-interface-generator`—never production `yonac`—to
`regenerate_interfaces.py --check`; production fixtures, no-legacy checks, and
documentation use that configuration's `yonac`. Platform object
suffix/symbol-tool inspection and the same golden v2 hash remain mandatory.
If a native-only defect appears, append its exact
repro to `docs/todo-list.md`, fix it through the same cutover path manifest,
and fold it into the existing cutover commit with
`git commit --amend --no-edit`; never add a follow-up production commit.
Force-update only the temporary validation branch with
`git push --force-with-lease`, rerun the complete local gate, trigger CI for
the amended SHA, and require that exact SHA green. Before any retry, repeat
the one-commit count/parent assertions above. If master advanced, rebase that
one commit onto the new master, amend/squash if necessary, rerun every local
gate, and validate the new exact SHA on all native jobs.

- [ ] **Step 12: Fast-forward the validated commit to master and push**

The thread authorizes the final master push, but only the native-green
production commit may move the code/runtime state on master:

```bash
validated_commit=$(git rev-parse HEAD)
git fetch origin master
test "$(git rev-parse master)" = "$(git rev-parse origin/master)"
test "$(git rev-list --count master..$validated_commit)" -eq 1
test "$(git rev-parse "$validated_commit^")" = "$(git rev-parse master)"
git switch master
git merge --ff-only "$validated_commit"
git push origin master
test "$(git rev-parse origin/master)" = "$validated_commit"
git push origin --delete typed-ir-cutover-validation
git branch -d typed-ir-cutover-validation
```

If `origin/master` advanced, do not force-push: merge/rebase that work into the
validation branch, rerun all local and native gates for the new commit, and
only then retry the fast-forward.

After the fast-forward is proven, update this plan's Steps 11-12, acceptance
checkboxes, exact validated SHA, workflow run ID, and final command results.
Commit and push that plan-only closeout as
`docs: record typed ir cutover validation`; reject any staged path other than
this plan. This administrative commit does not alter the already-validated
production tree, and its existence is the completion record (so it does not
add a self-referential unchecked step).

## Program Acceptance Checklist

- [ ] Every supported source construct lowers through canonical Typed IR.
- [ ] Every phase verifier is enabled in Debug and test builds.
- [ ] Complete callable/ABI/ownership/cleanup matrices pass at O0-O3.
- [ ] All twenty-three confirmed findings and the guarded latent wrapper path
  have named regression coverage.
- [ ] Generated programs contain no SJLJ, TLS exception frame, raw closure
  layout, raw async callback, or GENFN source-reparse dependency.
- [ ] All checked-in interfaces are deterministic `.yonai` v2 artifacts.
- [ ] Legacy Codegen and obsolete runtime/interface files are deleted.
- [ ] Debug, Release, sanitizer, Vulkan, x64, ARM64 QEMU, installed-consumer,
  and native CI gates pass.
- [ ] Internal docs, public site, generated API docs, changelog, and
  outstanding-work list describe only the shipped pipeline.

## Execution Notes

Tasks 2-16 are independently reviewable commits but remain test-only. They do
not create partial production cutovers. Task 17 is intentionally one atomic
commit validated on a temporary branch and then fast-forwarded unchanged to
master: switching only the compiler, runtime, interface, artifacts, or tools
would create an ABI combination this design forbids.

When a task discovers a new defect, follow the repository bug rule: append a
one-line repro to `docs/todo-list.md` immediately. Because this program is
already authorized to fix all Codegen-rewrite blockers, continue only when the
defect is within this plan's approved scope; otherwise stop for direction.

## Execution Handoff

Plan complete and saved to
`docs/superpowers/plans/2026-09-01-typed-ir-codegen-rewrite.md`. Two execution
options:

1. **Subagent-Driven (recommended)** — dispatch a fresh implementation agent
   per task, then run specification and quality reviews between tasks.
2. **Inline Execution** — execute the tasks in this session with
   `executing-plans`, in reviewed batches with checkpoints.
