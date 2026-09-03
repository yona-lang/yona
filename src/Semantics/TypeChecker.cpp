/// TypeChecker â€” core HM inference for Yona.
///
/// Walks the AST and infers types for every node. Supports:
/// - Literals (Int, Float, String, Bool, Symbol, Unit)
/// - Identifiers (env lookup with instantiation)
/// - Let bindings (with let-polymorphism / generalization)
/// - Functions (parameter inference from usage)
/// - Application (unify callee with Arrow(arg, result))
/// - If expressions (condition must be Bool, branches must unify)
/// - Binary operators (dispatched via env lookup)
/// - Tuples, sequences, do-blocks

#include "yona/Semantics/TypeChecker.h"

#include "yona/Interface/Reader.h"
#include "yona/Model/InferenceEffectClosure.h"
#include "yona/Model/ModuleIdentity.h"
#include "yona/Semantics/BorrowEscapeAnalysis.h"
#include "yona/Semantics/ModuleFunctionDependencies.h"

#include <algorithm>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <variant>

namespace yona::compiler::typechecker {
namespace {

SourceRange sourceExtent(const DiagnosticEngine &Diagnostics,
                         const ast::AstNode *Root) {
  if (!Root)
    return SourceRange::unknown();
  const auto &Sources = Diagnostics.sources();
  if (!Sources || !Root->Range.isValid() ||
      Root->Range.Source.value() >= Sources->size())
    return Root->Range;
  return SourceRange{Root->Range.Source, 1, 1, 0,
                     Sources->text(Root->Range.Source).size()};
}

/// Read-only alpha-equivalence for complete imported inference contracts.
///
/// Catalog producers borrow one TypeArena, but independently decoded rows may
/// use different raw binder ids and allocations.  Duplicate compatibility is
/// therefore structural rather than pointer- or id-based.  This comparator is
/// intentionally local to the v1/v2 admission boundary; the structural model
/// remains the authority after projection.
class ImportedContractComparator final {
public:
  using ResolveType = std::function<MonoTypePtr(MonoTypePtr)>;

  ImportedContractComparator(const EffectSolver &Effects,
                             const EffectAtomCatalog &Atoms,
                             ResolveType Resolve)
      : Effects_(Effects), Atoms_(Atoms), Resolve_(std::move(Resolve)) {}

  [[nodiscard]] bool
  function(const ImportedFunctionDeclarationProjection &Left,
           const ImportedFunctionDeclarationProjection &Right) {
    if (!schemePreamble(Left.Scheme, Right.Scheme) ||
        Left.ParameterOwnerships != Right.ParameterOwnerships ||
        Left.ResultContract != Right.ResultContract ||
        Left.Convention != Right.Convention ||
        !constraints(Left.Scheme.constraints, Right.Scheme.constraints) ||
        !type(Left.Scheme.body, Right.Scheme.body) ||
        !effectRoots(Left.Scheme, Right.Scheme))
      return false;
    return callables(Left.CallableContracts, Right.CallableContracts);
  }

  [[nodiscard]] bool
  operation(const ImportedOperationDeclarationProjection &Left,
            const ImportedOperationDeclarationProjection &Right) {
    resetBindings(Left.TypeParameters, Right.TypeParameters,
                  Left.EffectVariables, Right.EffectVariables);
    if (Left.TypeParameters.size() != Right.TypeParameters.size() ||
        !sameEffectVariableKinds(Left.EffectVariables, Right.EffectVariables) ||
        Left.ParameterOwnerships != Right.ParameterOwnerships ||
        Left.ResultContract != Right.ResultContract ||
        Left.ParameterTypes.size() != Right.ParameterTypes.size() ||
        !constraints(Left.Constraints, Right.Constraints))
      return false;
    for (std::size_t Index = 0; Index < Left.ParameterTypes.size(); ++Index)
      if (!type(Left.ParameterTypes[Index], Right.ParameterTypes[Index]))
        return false;
    return type(Left.ResultType, Right.ResultType) &&
           effect(Left.Effects, Right.Effects) &&
           callables(Left.CallableContracts, Right.CallableContracts);
  }

  [[nodiscard]] static bool
  resource(const ImportedResourceDeclarationProjection &Left,
           const ImportedResourceDeclarationProjection &Right) {
    return Left.TypeParameters.size() == Right.TypeParameters.size() &&
           Left.AbiKind == Right.AbiKind &&
           Left.Shareability == Right.Shareability &&
           Left.TryRetainNativeSymbol == Right.TryRetainNativeSymbol &&
           Left.ReleaseNativeSymbol == Right.ReleaseNativeSymbol;
  }

private:
  [[nodiscard]] static bool
  sameEffectVariableKinds(std::span<const QuantifiedEffectVariable> Left,
                          std::span<const QuantifiedEffectVariable> Right) {
    if (Left.size() != Right.size())
      return false;
    for (std::size_t Index = 0; Index < Left.size(); ++Index)
      if (Left[Index].Kind != Right[Index].Kind)
        return false;
    return true;
  }

  void resetBindings(std::span<const TypeId> LeftTypes,
                     std::span<const TypeId> RightTypes,
                     std::span<const QuantifiedEffectVariable> LeftEffects,
                     std::span<const QuantifiedEffectVariable> RightEffects) {
    LeftTypes_.clear();
    RightTypes_.clear();
    LeftEffects_.clear();
    RightEffects_.clear();
    for (std::size_t Index = 0; Index < LeftTypes.size(); ++Index)
      LeftTypes_.emplace(LeftTypes[Index], Index);
    for (std::size_t Index = 0; Index < RightTypes.size(); ++Index)
      RightTypes_.emplace(RightTypes[Index], Index);
    for (std::size_t Index = 0; Index < LeftEffects.size(); ++Index)
      LeftEffects_.emplace(LeftEffects[Index].Variable, Index);
    for (std::size_t Index = 0; Index < RightEffects.size(); ++Index)
      RightEffects_.emplace(RightEffects[Index].Variable, Index);
  }

  [[nodiscard]] bool schemePreamble(const TypeScheme &Left,
                                    const TypeScheme &Right) {
    resetBindings(Left.quantified_vars, Right.quantified_vars,
                  Left.quantified_effect_variables,
                  Right.quantified_effect_variables);
    return Left.quantified_vars.size() == Right.quantified_vars.size() &&
           sameEffectVariableKinds(Left.quantified_effect_variables,
                                   Right.quantified_effect_variables) &&
           Left.effect_graph.has_value() == Right.effect_graph.has_value();
  }

  [[nodiscard]] bool effectRoots(const TypeScheme &Left,
                                 const TypeScheme &Right) {
    if (Left.effect_roots.size() != Right.effect_roots.size())
      return false;
    for (std::size_t Index = 0; Index < Left.effect_roots.size(); ++Index)
      if (!type(Left.effect_roots[Index].Arrow,
                Right.effect_roots[Index].Arrow) ||
          !effect(Left.effect_roots[Index].Effect,
                  Right.effect_roots[Index].Effect))
        return false;
    return true;
  }

  [[nodiscard]] bool constraintIdentity(const Constraint &Left,
                                        const Constraint &Right) const {
    if (Left.resolved_trait || Right.resolved_trait)
      return Left.resolved_trait && Right.resolved_trait &&
             *Left.resolved_trait == *Right.resolved_trait;
    return Left.trait_name == Right.trait_name;
  }

  [[nodiscard]] bool constraint(const Constraint &Left,
                                const Constraint &Right) {
    if (!constraintIdentity(Left, Right) ||
        Left.types.size() != Right.types.size())
      return false;
    for (std::size_t Index = 0; Index < Left.types.size(); ++Index)
      if (!type(Left.types[Index], Right.types[Index]))
        return false;
    return true;
  }

  [[nodiscard]] bool constraints(std::span<const Constraint> Left,
                                 std::span<const Constraint> Right) {
    if (Left.size() != Right.size())
      return false;
    std::vector<bool> Matched(Right.size());
    for (const auto &LeftConstraint : Left) {
      bool Found = false;
      for (std::size_t Index = 0; Index < Right.size(); ++Index)
        if (!Matched[Index] && constraint(LeftConstraint, Right[Index])) {
          Matched[Index] = true;
          Found = true;
          break;
        }
      if (!Found)
        return false;
    }
    return true;
  }

  [[nodiscard]] bool callable(const CallableTypeContract &Left,
                              const CallableTypeContract &Right) {
    return Left.ParameterOwnerships == Right.ParameterOwnerships &&
           Left.ResultContract == Right.ResultContract &&
           Left.Convention == Right.Convention && type(Left.Arrow, Right.Arrow);
  }

  [[nodiscard]] bool applicationMetadata(const MonoType &Left,
                                         const MonoType &Right) {
    if (Left.promise_result_contract != Right.promise_result_contract ||
        Left.promise_effects.valid() != Right.promise_effects.valid())
      return false;
    return !Left.promise_effects.valid() ||
           effect(Left.promise_effects, Right.promise_effects);
  }

  [[nodiscard]] bool callables(std::span<const CallableTypeContract> Left,
                               std::span<const CallableTypeContract> Right) {
    if (Left.size() != Right.size())
      return false;
    std::vector<bool> Matched(Right.size());
    for (const auto &LeftCallable : Left) {
      bool Found = false;
      for (std::size_t Index = 0; Index < Right.size(); ++Index)
        if (!Matched[Index] && callable(LeftCallable, Right[Index])) {
          Matched[Index] = true;
          Found = true;
          break;
        }
      if (!Found)
        return false;
    }
    return true;
  }

  [[nodiscard]] bool type(MonoTypePtr Left, MonoTypePtr Right) {
    Left = Resolve_(Left);
    Right = Resolve_(Right);
    if (Left == Right)
      return Left != nullptr;
    if (!Left || !Right || Left->tag != Right->tag)
      return false;
    const auto Active = std::pair{Left, Right};
    if (!ActiveTypes_.insert(Active).second)
      return true;
    const auto Finish = [&](bool Equal) {
      ActiveTypes_.erase(Active);
      return Equal;
    };
    switch (Left->tag) {
    case MonoType::Var: {
      const auto LeftBinder = LeftTypes_.find(Left->var_id);
      const auto RightBinder = RightTypes_.find(Right->var_id);
      return Finish(LeftBinder != LeftTypes_.end() ||
                            RightBinder != RightTypes_.end()
                        ? LeftBinder != LeftTypes_.end() &&
                              RightBinder != RightTypes_.end() &&
                              LeftBinder->second == RightBinder->second
                        : Left->var_id == Right->var_id);
    }
    case MonoType::Con:
      return Finish(Left->con == Right->con);
    case MonoType::App: {
      const bool SameIdentity =
          (Left->resolved_key || Right->resolved_key)
              ? Left->resolved_key && Right->resolved_key &&
                    *Left->resolved_key == *Right->resolved_key
              : Left->type_name == Right->type_name;
      if (!SameIdentity || Left->args.size() != Right->args.size() ||
          !applicationMetadata(*Left, *Right))
        return Finish(false);
      for (std::size_t Index = 0; Index < Left->args.size(); ++Index)
        if (!type(Left->args[Index], Right->args[Index]))
          return Finish(false);
      return Finish(true);
    }
    case MonoType::Arrow:
      return Finish(type(Left->param_type, Right->param_type) &&
                    type(Left->return_type, Right->return_type) &&
                    effect(Left->arrow_effect, Right->arrow_effect));
    case MonoType::MTuple:
      if (Left->elements.size() != Right->elements.size())
        return Finish(false);
      for (std::size_t Index = 0; Index < Left->elements.size(); ++Index)
        if (!type(Left->elements[Index], Right->elements[Index]))
          return Finish(false);
      return Finish(true);
    case MonoType::MRecord:
      if (Left->record_fields.size() != Right->record_fields.size() ||
          static_cast<bool>(Left->row_rest) !=
              static_cast<bool>(Right->row_rest))
        return Finish(false);
      for (std::size_t Index = 0; Index < Left->record_fields.size(); ++Index)
        if (Left->record_fields[Index].first !=
                Right->record_fields[Index].first ||
            !type(Left->record_fields[Index].second,
                  Right->record_fields[Index].second))
          return Finish(false);
      return Finish(!Left->row_rest || type(Left->row_rest, Right->row_rest));
    case MonoType::Sum: {
      if (Left->sum_alternatives.size() != Right->sum_alternatives.size())
        return Finish(false);
      std::vector<bool> Matched(Right->sum_alternatives.size());
      for (const auto *LeftAlternative : Left->sum_alternatives) {
        bool Found = false;
        for (std::size_t Index = 0; Index < Right->sum_alternatives.size();
             ++Index)
          if (!Matched[Index] &&
              type(LeftAlternative, Right->sum_alternatives[Index])) {
            Matched[Index] = true;
            Found = true;
            break;
          }
        if (!Found)
          return Finish(false);
      }
      return Finish(true);
    }
    }
    return Finish(false);
  }

  [[nodiscard]] bool atom(EffectAtomId Left, EffectAtomId Right) {
    try {
      Left = Effects_.canonicalAtom(Left);
      Right = Effects_.canonicalAtom(Right);
      if (!Atoms_.valid(Left) || !Atoms_.valid(Right))
        return false;
      const auto &LeftAtom = Atoms_.at(Left);
      const auto &RightAtom = Atoms_.at(Right);
      if (LeftAtom.index() != RightAtom.index())
        return false;
      const auto *LeftOperation = std::get_if<OperationEffectAtom>(&LeftAtom);
      if (!LeftOperation)
        return true;
      const auto &RightOperation = std::get<OperationEffectAtom>(RightAtom);
      if (LeftOperation->Key != RightOperation.Key ||
          LeftOperation->TypeArguments.size() !=
              RightOperation.TypeArguments.size() ||
          LeftOperation->EffectArguments.size() !=
              RightOperation.EffectArguments.size())
        return false;
      for (std::size_t Index = 0; Index < LeftOperation->TypeArguments.size();
           ++Index)
        if (!type(LeftOperation->TypeArguments[Index],
                  RightOperation.TypeArguments[Index]))
          return false;
      for (std::size_t Index = 0; Index < LeftOperation->EffectArguments.size();
           ++Index)
        if (!effect(LeftOperation->EffectArguments[Index],
                    RightOperation.EffectArguments[Index]))
          return false;
      return true;
    } catch (const std::invalid_argument &) {
      return false;
    }
  }

  [[nodiscard]] bool atomSet(std::span<const EffectAtomId> Left,
                             std::span<const EffectAtomId> Right) {
    if (Left.size() != Right.size())
      return false;
    std::vector<bool> Matched(Right.size());
    for (const auto LeftAtom : Left) {
      bool Found = false;
      for (std::size_t Index = 0; Index < Right.size(); ++Index)
        if (!Matched[Index] && atom(LeftAtom, Right[Index])) {
          Matched[Index] = true;
          Found = true;
          break;
        }
      if (!Found)
        return false;
    }
    return true;
  }

  [[nodiscard]] bool tail(const EffectProjection &Left,
                          const EffectProjection &Right) {
    if (Left.opaque != Right.opaque ||
        !atomSet(Left.excluded_atoms, Right.excluded_atoms))
      return false;
    const auto LeftBinder = LeftEffects_.find(Left.variable);
    const auto RightBinder = RightEffects_.find(Right.variable);
    return LeftBinder != LeftEffects_.end() ||
                   RightBinder != RightEffects_.end()
               ? LeftBinder != LeftEffects_.end() &&
                     RightBinder != RightEffects_.end() &&
                     LeftBinder->second == RightBinder->second
               : Left.variable == Right.variable;
  }

  [[nodiscard]] bool effect(EffectRef Left, EffectRef Right) {
    const auto Active = std::pair{Left, Right};
    if (!ActiveEffects_.insert(Active).second)
      return true;
    const auto Finish = [&](bool Equal) {
      ActiveEffects_.erase(Active);
      return Equal;
    };
    try {
      const auto LeftNormal = Effects_.summarize(Left);
      const auto RightNormal = Effects_.summarize(Right);
      if (!atomSet(LeftNormal.known_atoms, RightNormal.known_atoms) ||
          LeftNormal.tails.size() != RightNormal.tails.size())
        return Finish(false);
      std::vector<bool> Matched(RightNormal.tails.size());
      for (const auto &LeftTail : LeftNormal.tails) {
        bool Found = false;
        for (std::size_t Index = 0; Index < RightNormal.tails.size(); ++Index)
          if (!Matched[Index] && tail(LeftTail, RightNormal.tails[Index])) {
            Matched[Index] = true;
            Found = true;
            break;
          }
        if (!Found)
          return Finish(false);
      }
      return Finish(true);
    } catch (const std::invalid_argument &) {
      return Finish(false);
    }
  }

  const EffectSolver &Effects_;
  const EffectAtomCatalog &Atoms_;
  ResolveType Resolve_;
  std::unordered_map<TypeId, std::size_t> LeftTypes_;
  std::unordered_map<TypeId, std::size_t> RightTypes_;
  std::unordered_map<EffectVarId, std::size_t> LeftEffects_;
  std::unordered_map<EffectVarId, std::size_t> RightEffects_;
  std::set<std::pair<MonoTypePtr, MonoTypePtr>> ActiveTypes_;
  std::set<std::pair<EffectRef, EffectRef>> ActiveEffects_;
};

} // namespace

using ast::ApplyExpr;
using ast::AsDataStructurePattern;
using ast::AST_ADD_EXPR;
using ast::AST_APPLY_EXPR;
using ast::AST_AS_DATA_STRUCTURE_PATTERN;
using ast::AST_BYTE_EXPR;
using ast::AST_CASE_EXPR;
using ast::AST_CHARACTER_EXPR;
using ast::AST_CONS_LEFT_EXPR;
using ast::AST_CONS_RIGHT_EXPR;
using ast::AST_CONSTRUCTOR_PATTERN;
using ast::AST_DICT_EXPR;
using ast::AST_DICT_GENERATOR_EXPR;
using ast::AST_DICT_PATTERN;
using ast::AST_DIVIDE_EXPR;
using ast::AST_DO_EXPR;
using ast::AST_EQ_EXPR;
using ast::AST_EXTERN_DECL;
using ast::AST_FALSE_LITERAL_EXPR;
using ast::AST_FIELD_ACCESS_EXPR;
using ast::AST_FIELD_UPDATE_EXPR;
using ast::AST_FLOAT_EXPR;
using ast::AST_FUNCTION_EXPR;
using ast::AST_GT_EXPR;
using ast::AST_GTE_EXPR;
using ast::AST_HANDLE_EXPR;
using ast::AST_HEAD_TAILS_PATTERN;
using ast::AST_IDENTIFIER_EXPR;
using ast::AST_IF_EXPR;
using ast::AST_IMPORT_EXPR;
using ast::AST_IN_EXPR;
using ast::AST_INTEGER_EXPR;
using ast::AST_JOIN_EXPR;
using ast::AST_LET_EXPR;
using ast::AST_LITERAL_EXPR;
using ast::AST_LOGICAL_AND_EXPR;
using ast::AST_LOGICAL_NOT_OP_EXPR;
using ast::AST_LOGICAL_OR_EXPR;
using ast::AST_LT_EXPR;
using ast::AST_LTE_EXPR;
using ast::AST_MAIN;
using ast::AST_MODULO_EXPR;
using ast::AST_MULTIPLY_EXPR;
using ast::AST_NEQ_EXPR;
using ast::AST_OR_PATTERN;
using ast::AST_PATTERN_VALUE;
using ast::AST_PERFORM_EXPR;
using ast::AST_PIPE_LEFT_EXPR;
using ast::AST_PIPE_RIGHT_EXPR;
using ast::AST_POWER_EXPR;
using ast::AST_RAISE_EXPR;
using ast::AST_RANGE_SEQUENCE_EXPR;
using ast::AST_RECORD_LITERAL_EXPR;
using ast::AST_RECORD_PATTERN;
using ast::AST_REMOVE_EXPR;
using ast::AST_SEQ_GENERATOR_EXPR;
using ast::AST_SEQ_PATTERN;
using ast::AST_SET_EXPR;
using ast::AST_SET_GENERATOR_EXPR;
using ast::AST_STRING_EXPR;
using ast::AST_SUBTRACT_EXPR;
using ast::AST_SYMBOL_EXPR;
using ast::AST_TRUE_LITERAL_EXPR;
using ast::AST_TRY_CATCH_EXPR;
using ast::AST_TUPLE_EXPR;
using ast::AST_TUPLE_PATTERN;
using ast::AST_TYPED_PATTERN;
using ast::AST_UNDERSCORE_PATTERN;
using ast::AST_UNIT_EXPR;
using ast::AST_VALUES_SEQUENCE_EXPR;
using ast::AST_WITH_EXPR;

struct TypeChecker::ProjectionStorage final {
  std::optional<TypeScheme> RootScheme;
  std::vector<model::ParameterOwnership> RootOwnerships;
  std::vector<CallableTypeContract> CallableContracts;
  std::vector<NodeSemanticProjection> NodeFacts;
  std::vector<DeclarationScopeProjection> Scopes;
  std::vector<FunctionSchemeProjection> Functions;
  std::deque<std::vector<model::ParameterOwnership>> OperationOwnerships;
  std::deque<std::vector<CallableTypeContract>> OperationCallableContracts;
  std::vector<OperationSchemeProjection> Operations;
  std::vector<NominalDeclarationProjection> Nominals;
  std::vector<ResourceDeclarationProjection> Resources;
  std::vector<TraitDeclarationProjection> Traits;
  std::vector<TraitInstanceProjection> Instances;
  std::vector<DeriveRequestProjection> Derivations;
  std::vector<ImportedDeclarationCatalogEntry> ImportedDeclarations;
  std::deque<TypeScheme> ModuleSchemes;
  std::deque<std::vector<model::ParameterOwnership>> ModuleOwnerships;
  std::deque<std::vector<CallableTypeContract>> ModuleCallableContracts;
  std::deque<std::vector<NodeSemanticProjection>> ModuleNodeFacts;
  std::deque<std::vector<TypeId>> ModuleTypeParameters;
  std::deque<std::vector<MonoTypePtr>> ConstructorFields;
  std::deque<std::vector<ConstructorProjection>> Constructors;
  std::deque<std::vector<Constraint>> DeclarationConstraints;
  std::deque<std::vector<TraitMethodProjection>> TraitMethods;
  std::deque<std::vector<MonoTypePtr>> InstanceArguments;
  std::deque<std::vector<TraitMethodBindingProjection>> InstanceMethods;
  std::deque<std::vector<model::NominalTypeKey>> DerivedTraits;
  const ast::AstNode *SourceRoot = nullptr;
  SourceRange SourceExtent = SourceRange::unknown();
  SemanticProjectionBatch Batch;

  void clear() {
    Batch = {};
    Functions.clear();
    Scopes.clear();
    NodeFacts.clear();
    CallableContracts.clear();
    RootOwnerships.clear();
    RootScheme.reset();
    Operations.clear();
    OperationCallableContracts.clear();
    OperationOwnerships.clear();
    Nominals.clear();
    Resources.clear();
    Traits.clear();
    Instances.clear();
    Derivations.clear();
    ImportedDeclarations.clear();
    Constructors.clear();
    ConstructorFields.clear();
    DerivedTraits.clear();
    InstanceMethods.clear();
    InstanceArguments.clear();
    TraitMethods.clear();
    DeclarationConstraints.clear();
    ModuleTypeParameters.clear();
    ModuleNodeFacts.clear();
    ModuleCallableContracts.clear();
    ModuleOwnerships.clear();
    ModuleSchemes.clear();
    SourceRoot = nullptr;
    SourceExtent = SourceRange::unknown();
  }

  void publish() noexcept {
    Batch =
        SemanticProjectionBatch{Scopes,     Functions,   Operations,
                                Nominals,   Resources,   Traits,
                                Instances,  Derivations, ImportedDeclarations,
                                SourceRoot, SourceExtent};
  }
};
using ast::AstNode;
using ast::AstNodeType;
using ast::BinaryOpExpr;
using ast::BodyWithGuards;
using ast::BodyWithoutGuards;
using ast::CaseExpr;
using ast::CollectionExtractorExpr;
using ast::ConsLeftExpr;
using ast::ConsRightExpr;
using ast::ConstructorPattern;
using ast::DictExpr;
using ast::DictGeneratorExpr;
using ast::DictPattern;
using ast::DoExpr;
using ast::ExprCall;
using ast::ExprNode;
using ast::ExternDeclExpr;
using ast::FieldAccessExpr;
using ast::FieldType;
using ast::FieldUpdateExpr;
using ast::FqnExpr;
using ast::FunctionExpr;
using ast::FunctionsImport;
using ast::HandleExpr;
using ast::HeadTailsPattern;
using ast::IdentifierExpr;
using ast::IfExpr;
using ast::ImportExpr;
using ast::InExpr;
using ast::JoinExpr;
using ast::KeyValueCollectionExtractorExpr;
using ast::LambdaAlias;
using ast::LetExpr;
using ast::LiteralExpr;
using ast::LogicalNotOpExpr;
using ast::MainNode;
using ast::ModuleDecl;
using ast::ModuleImport;
using ast::NameCall;
using ast::OrPattern;
using ast::PatternAlias;
using ast::PatternNode;
using ast::PatternValue;
using ast::PatternWithoutGuards;
using ast::PerformExpr;
using ast::RaiseExpr;
using ast::RecordLiteralExpr;
using ast::RecordPattern;
using ast::RemoveExpr;
using ast::SeqGeneratorExpr;
using ast::SeqPattern;
using ast::SetExpr;
using ast::SetGeneratorExpr;
using ast::SymbolExpr;
using ast::TryCatchExpr;
using ast::TupleExpr;
using ast::TuplePattern;
using ast::TypedPattern;
using ast::ValueAlias;
using ast::ValueCollectionExtractorExpr;
using ast::ValueExpr;
using ast::ValuesSequenceExpr;
using ast::WithExpr;

/// Simple edit distance for "did you mean?" suggestions.
static size_t edit_distance(const std::string &a, const std::string &b) {
  if (a.empty())
    return b.size();
  if (b.empty())
    return a.size();
  std::vector<std::vector<size_t>> dp(a.size() + 1,
                                      std::vector<size_t>(b.size() + 1));
  for (size_t i = 0; i <= a.size(); i++)
    dp[i][0] = i;
  for (size_t j = 0; j <= b.size(); j++)
    dp[0][j] = j;
  for (size_t i = 1; i <= a.size(); i++)
    for (size_t j = 1; j <= b.size(); j++)
      dp[i][j] =
          std::min({dp[i - 1][j] + 1, dp[i][j - 1] + 1,
                    dp[i - 1][j - 1] + (a[i - 1] != b[j - 1] ? 1u : 0u)});
  return dp[a.size()][b.size()];
}

static std::string fqn_to_string(FqnExpr *fqn) {
  if (!fqn || !fqn->moduleName)
    return {};
  std::string s;
  if (fqn->packageName.has_value()) {
    auto *pkg = fqn->packageName.value();
    for (size_t i = 0; i < pkg->parts.size(); i++) {
      if (i)
        s += "\\";
      s += pkg->parts[i]->value;
    }
    s += "\\";
  }
  s += fqn->moduleName->value;
  return s;
}

static std::string
instance_method_symbol(std::string_view Trait,
                       std::span<const std::string> TypeNames,
                       std::string_view Method) {
  std::string Result(Trait);
  for (const auto &Type : TypeNames)
    Result += "_" + Type;
  Result += "_";
  Result += Method;
  return Result;
}

static std::string instance_method_symbol(const ast::InstanceDeclNode &Instance,
                                          std::string_view Method) {
  return instance_method_symbol(Instance.trait_name, Instance.type_names,
                                Method);
}

static bool
same_callable_contracts(std::span<const CallableTypeContract> Left,
                        std::span<const CallableTypeContract> Right) {
  if (Left.size() != Right.size())
    return false;
  for (std::size_t Index = 0; Index < Left.size(); ++Index)
    if (Left[Index].ParameterOwnerships != Right[Index].ParameterOwnerships ||
        Left[Index].ResultContract != Right[Index].ResultContract ||
        Left[Index].Convention != Right[Index].Convention)
      return false;
  return true;
}

/// A parameterless module binding serializes as an implementation-only root
/// arrow at `$`; a source-visible thunk starts at `$/r`.  Rebase that visible
/// subtree without decoding and re-encoding individual effect entries: labels,
/// tail ids, and masks are part of the interface contract.
static std::optional<std::string>
normalize_zero_arity_thunk_scheme(std::string_view encoded) {
  if (encoded.empty())
    return std::nullopt;

  bool found_hidden_root = false;
  bool found_visible_root = false;
  std::string normalized;
  size_t start = 0;
  while (start <= encoded.size()) {
    const size_t end = encoded.find(';', start);
    const std::string_view field = encoded.substr(
        start,
        end == std::string_view::npos ? std::string_view::npos : end - start);
    if (field.empty())
      return std::nullopt;
    const size_t delimiter = field.find('#');
    if (delimiter == std::string_view::npos)
      return std::nullopt;
    const std::string_view path = field.substr(0, delimiter);
    if (path == "$") {
      if (found_hidden_root)
        return std::nullopt;
      found_hidden_root = true;
    } else if (path == "$/r" || path.starts_with("$/r/")) {
      found_visible_root = found_visible_root || path == "$/r";
      if (!normalized.empty())
        normalized += ';';
      normalized += '$';
      normalized.append(field.substr(3));
    } else {
      // A valid hidden module-binding graph contains only its root and the
      // source-visible return subtree. Preserve unknown graphs conservatively.
      return std::nullopt;
    }
    if (end == std::string_view::npos)
      break;
    start = end + 1;
  }
  if (!found_hidden_root || !found_visible_root)
    return std::nullopt;
  return normalized;
}

TypeChecker::TypeChecker(DiagnosticEngine &diag)
    : unifier_(arena_, uf_, diag), diag_(diag),
      projection_storage_(std::make_unique<ProjectionStorage>()) {
  root_env_ = std::make_shared<TypeEnv>();
  register_builtins(*root_env_, arena_);
  for (const auto &[Name, Scheme] : root_env_->locals())
    scheme_identities_.insert_or_assign(
        Scheme.body, model::FunctionDeclarationIdentity{model::SymbolIdentity{
                         model::ModuleIdentity{"Prelude"}, Name}});
}

TypeChecker::~TypeChecker() = default;

const SemanticProjectionBatch &
TypeChecker::structuralProjectionInput() const noexcept {
  return projection_storage_->Batch;
}

void TypeChecker::begin_check_session() {
  type_map_.clear();
  deferred_constraints_.clear();
  selected_trait_instances_.clear();
  trait_method_uses_.clear();
  collection_key_types_.clear();
  collection_key_operations_.clear();
  sum_injections_.clear();
  node_effects_.clear();
  operation_occurrences_.clear();
  operation_atoms_.clear();
  legacy_untyped_effect_atoms_.clear();
  imported_function_declarations_.clear();
  instantiations_.clear();
  function_callable_contracts_.clear();
  expression_callable_contracts_.clear();
  function_schemes_.clear();
  projection_function_types_.clear();
  projection_function_identities_.clear();
  trait_default_owners_.clear();
  instance_method_owners_.clear();
  projection_trait_type_parameters_.clear();
  projection_nominal_declarations_.clear();
  projection_trait_methods_.clear();
  projection_extern_declarations_.clear();
  projection_instance_type_parameters_.clear();
  function_inference_stack_.clear();
  concurrency_boundaries_.clear();
  capture_frames_.clear();
  function_capture_types_.clear();
  named_function_capture_types_.clear();
  latent_effect_stack_.clear();
  evaluation_effect_stack_.clear();
  isolated_evaluation_roots_.clear();
  effect_origins_.clear();
  unhandled_effect_locations_.clear();
  recursive_self_contexts_.clear();
  predeclared_function_body_effects_.clear();
  projection_root_ = nullptr;
  projection_root_type_ = nullptr;
  projection_visible_parameter_count_ = 0;
  projection_module_ = nullptr;
  projection_module_functions_.clear();
  projection_storage_->clear();
  projection_storage_->publish();
}

void TypeChecker::populateImportedProjectionCatalog(SourceRange Range) {
  auto &Output = projection_storage_->ImportedDeclarations;
  Output.clear();

  const auto canonicalIdentity =
      [](const ImportedDeclarationCatalogEntry &Entry) {
        auto Result = std::to_string(static_cast<unsigned>(Entry.Kind)) + ":";
        Result += std::visit(
            [](const auto &Value) -> std::string {
              using T = std::decay_t<decltype(Value)>;
              if constexpr (std::same_as<T, model::NominalTypeKey>)
                return Value.Module.fqn() + "\\" + Value.Name;
              else if constexpr (std::same_as<T, model::EffectOperationKey>)
                return Value.Effect.Module.fqn() + "\\" + Value.Effect.Name +
                       "." + Value.Operation;
              else if constexpr (std::same_as<
                                     T, model::FunctionDeclarationIdentity>)
                return std::visit(
                    [](const auto &Function) {
                      using F = std::decay_t<decltype(Function)>;
                      if constexpr (std::same_as<F, model::SymbolIdentity>)
                        return Function.Module.fqn() + "\\" + Function.Name;
                      else
                        return Function.Module.fqn() + "\\<local>";
                    },
                    Value);
              else
                return std::string("<instance>");
            },
            Entry.Identity);
        return Result;
      };
  const auto sameIdentity = [](const ImportedDeclarationCatalogEntry &Left,
                               const ImportedDeclarationCatalogEntry &Right) {
    if (Left.Kind != Right.Kind ||
        Left.Identity.index() != Right.Identity.index())
      return false;
    if (const auto *Key = std::get_if<model::NominalTypeKey>(&Left.Identity))
      return *Key == std::get<model::NominalTypeKey>(Right.Identity);
    if (const auto *Identity =
            std::get_if<model::FunctionDeclarationIdentity>(&Left.Identity))
      return *Identity ==
             std::get<model::FunctionDeclarationIdentity>(Right.Identity);
    if (const auto *Key =
            std::get_if<model::EffectOperationKey>(&Left.Identity))
      return *Key == std::get<model::EffectOperationKey>(Right.Identity);
    return false;
  };
  const auto Add = [&](ImportedDeclarationCatalogEntry Entry) {
    Entry.Range = Range;
    std::ranges::sort(Entry.TraitMethods);
    Entry.TraitMethods.erase(std::ranges::unique(Entry.TraitMethods).begin(),
                             Entry.TraitMethods.end());
    const auto Existing = std::ranges::find_if(
        Output, [&](const auto &Value) { return sameIdentity(Value, Entry); });
    if (Existing == Output.end())
      Output.push_back(std::move(Entry));
    else {
      const auto HasRichContract = [](const auto &Value) {
        return Value.Function.has_value() || Value.Operation.has_value() ||
               Value.Resource.has_value();
      };
      const bool ExistingRich = HasRichContract(*Existing);
      const bool EntryRich = HasRichContract(Entry);
      ImportedContractComparator Compare(
          arena_.effect_solver(), arena_.effect_atoms(),
          [&](MonoTypePtr Type) { return unifier_.resolve(Type); });
      const bool SameRichContract = [&] {
        if (!ExistingRich || !EntryRich)
          return true;
        if (Existing->Function || Entry.Function)
          return Existing->Function && Entry.Function &&
                 Compare.function(*Existing->Function, *Entry.Function);
        if (Existing->Operation || Entry.Operation)
          return Existing->Operation && Entry.Operation &&
                 Compare.operation(*Existing->Operation, *Entry.Operation);
        return Existing->Resource && Entry.Resource &&
               ImportedContractComparator::resource(*Existing->Resource,
                                                    *Entry.Resource);
      }();
      const bool ConflictingShape =
          Existing->TypeArity != Entry.TypeArity ||
          Existing->EffectArity != Entry.EffectArity ||
          (!Existing->TraitMethods.empty() && !Entry.TraitMethods.empty() &&
           Existing->TraitMethods != Entry.TraitMethods) ||
          !SameRichContract;
      if (ConflictingShape) {
        const auto Identity = canonicalIdentity(Entry).substr(2);
        diag_.error(Range, ErrorCode::E0100,
                    "conflicting imported declaration catalog rows for '" +
                        Identity + "' (type " +
                        std::to_string(Existing->TypeArity) + " vs " +
                        std::to_string(Entry.TypeArity) + ", effect " +
                        std::to_string(Existing->EffectArity) + " vs " +
                        std::to_string(Entry.EffectArity) + ")");
        ++error_count_;
        return;
      }
      if (!ExistingRich && EntryRich) {
        Existing->TypeArity = Entry.TypeArity;
        Existing->EffectArity = Entry.EffectArity;
      }
      Existing->Derivable = Existing->Derivable || Entry.Derivable;
      if (Entry.Function)
        Existing->Function = std::move(Entry.Function);
      if (Entry.Operation)
        Existing->Operation = std::move(Entry.Operation);
      if (Entry.Resource)
        Existing->Resource = std::move(Entry.Resource);
      Existing->TraitMethods.insert(Existing->TraitMethods.end(),
                                    Entry.TraitMethods.begin(),
                                    Entry.TraitMethods.end());
      std::ranges::sort(Existing->TraitMethods);
      Existing->TraitMethods.erase(
          std::ranges::unique(Existing->TraitMethods).begin(),
          Existing->TraitMethods.end());
    }
  };
  const auto HasNominal = [&](model::BinderOwnerKind Kind,
                              const model::NominalTypeKey &Key) {
    return std::ranges::any_of(Output, [&](const auto &Entry) {
      const auto *Existing =
          std::get_if<model::NominalTypeKey>(&Entry.Identity);
      return Entry.Kind == Kind && Existing && *Existing == Key;
    });
  };
  const auto HasFunction =
      [&](const model::FunctionDeclarationIdentity &Identity) {
        return std::ranges::any_of(Output, [&](const auto &Entry) {
          const auto *Existing =
              std::get_if<model::FunctionDeclarationIdentity>(&Entry.Identity);
          return Entry.Kind == model::BinderOwnerKind::Function && Existing &&
                 *Existing == Identity;
        });
      };
  const auto HasOperation = [&](const model::EffectOperationKey &Key) {
    return std::ranges::any_of(Output, [&](const auto &Entry) {
      const auto *Existing =
          std::get_if<model::EffectOperationKey>(&Entry.Identity);
      return Entry.Kind == model::BinderOwnerKind::Operation && Existing &&
             *Existing == Key;
    });
  };

  if (import_src_) {
    std::vector<std::string> Modules(registered_import_modules_.begin(),
                                     registered_import_modules_.end());
    std::ranges::sort(Modules);
    for (const auto &Module : Modules) {
      auto Entries = import_src_->imported_declarations(Module);
      std::ranges::sort(Entries, [&](const auto &Left, const auto &Right) {
        return canonicalIdentity(Left) < canonicalIdentity(Right);
      });
      for (auto &Entry : Entries)
        Add(std::move(Entry));
    }
  }

  std::unordered_set<model::FunctionDeclarationIdentity,
                     model::FunctionDeclarationIdentityHasher>
      ReferencedFunctions;
  for (const auto &[_, Instantiation] : instantiations_)
    ReferencedFunctions.insert(Instantiation.Scheme);
  const auto RememberTarget = [&](const TraitMethodSelectionProjection &Use) {
    if (const auto *Resolved =
            std::get_if<ResolvedTraitTargetProjection>(&Use.Evidence))
      ReferencedFunctions.insert(Resolved->SelectedTarget);
  };
  for (const auto &[_, Use] : trait_method_uses_)
    RememberTarget(Use);
  for (const auto &[_, Operations] : collection_key_operations_) {
    RememberTarget(Operations.Hash);
    RememberTarget(Operations.Equals);
  }

  for (const auto &[Identity, Contract] : imported_function_declarations_)
    Add({model::BinderOwnerKind::Function,
         Identity,
         Contract.Scheme.quantified_vars.size(),
         static_cast<std::size_t>(std::ranges::count_if(
             Contract.Scheme.quantified_effect_variables,
             [](const auto &Variable) {
               return Variable.Kind == model::EffectVariableKind::Flexible;
             })),
         {},
         false,
         Range,
         Contract});

  for (const auto &[Identity, Contract] :
       registered_interface_function_declarations_)
    if (ReferencedFunctions.contains(Identity) &&
        !imported_function_declarations_.contains(Identity))
      Add({model::BinderOwnerKind::Function,
           Identity,
           Contract.Scheme.quantified_vars.size(),
           static_cast<std::size_t>(std::ranges::count_if(
               Contract.Scheme.quantified_effect_variables,
               [](const auto &Variable) {
                 return Variable.Kind == model::EffectVariableKind::Flexible;
               })),
           {},
           false,
           Range,
           Contract});

  for (const auto &[Name, Key] : nominal_keys_)
    if (Key.Module != current_module_ &&
        !HasNominal(model::BinderOwnerKind::Nominal, Key)) {
      const auto Parameters = adt_type_params_.find(Name);
      Add({model::BinderOwnerKind::Nominal,
           Key,
           Parameters == adt_type_params_.end() ? 0 : Parameters->second.size(),
           0,
           {},
           false,
           Range});
    }
  for (const auto &[Name, Key] : trait_keys_)
    if (Key.Module != current_module_ &&
        !HasNominal(model::BinderOwnerKind::Trait, Key)) {
      const auto Parameters = trait_type_params_.find(Name);
      std::vector<std::string> Methods;
      if (const auto Found = trait_methods_.find(Name);
          Found != trait_methods_.end())
        Methods.assign(Found->second.begin(), Found->second.end());
      const bool Derivable = Name == "Eq" || Name == "Hash" || Name == "Ord" ||
                             Name == "Show" || Name == "Send" ||
                             Name == "Shareable";
      Add({model::BinderOwnerKind::Trait, Key,
           Parameters == trait_type_params_.end() ? 0
                                                  : Parameters->second.size(),
           0, std::move(Methods), Derivable, Range});
    }
  for (const auto &[_, Scheme] : root_env_->locals()) {
    const auto Identity = scheme_identities_.find(Scheme.body);
    if (Identity == scheme_identities_.end() ||
        !std::visit(
            [&](const auto &Value) { return Value.Module != current_module_; },
            Identity->second))
      continue;
    if (HasFunction(Identity->second))
      continue;
    const auto EffectArity = static_cast<std::size_t>(std::ranges::count_if(
        Scheme.quantified_effect_variables, [](const auto &Variable) {
          return Variable.Kind == model::EffectVariableKind::Flexible;
        }));
    Add({model::BinderOwnerKind::Function,
         Identity->second,
         Scheme.quantified_vars.size(),
         EffectArity,
         {},
         false,
         Range});
  }
  const auto ParameterOwnership = [&](MonoTypePtr Type) {
    Type = unifier_.resolve(Type);
    return Type && Type->tag == MonoType::Con && Type->con != TyCon::String
               ? model::ParameterOwnership::Trivial
               : model::ParameterOwnership::Consume;
  };
  const auto ResultOwnership = [&](MonoTypePtr Type) {
    Type = unifier_.resolve(Type);
    return Type && Type->tag == MonoType::Con && Type->con != TyCon::String
               ? model::ResultOwnership::Trivial
               : model::ResultOwnership::Owned;
  };
  for (const auto &[_, Operation] : effect_ops_)
    if (Operation.key.Effect.Module != current_module_ &&
        !HasOperation(Operation.key)) {
      const auto *Signature = Operation.signature.body;
      if (!Signature || Signature->tag != MonoType::MTuple ||
          Signature->elements.size() != Operation.parameter_count + 1) {
        diag_.error(Range, ErrorCode::E0100,
                    "malformed registered effect signature for '" +
                        Operation.key.Effect.Module.fqn() + "\\" +
                        Operation.key.Effect.Name + "." +
                        Operation.key.Operation + "'");
        ++error_count_;
        continue;
      }
      ImportedOperationDeclarationProjection Contract;
      Contract.TypeParameters = Operation.signature.quantified_vars;
      Contract.EffectVariables =
          Operation.signature.quantified_effect_variables;
      Contract.Constraints = Operation.signature.constraints;
      Contract.ParameterTypes.assign(Signature->elements.begin(),
                                     Signature->elements.begin() +
                                         Operation.parameter_count);
      Contract.ParameterOwnerships.reserve(Operation.parameter_count);
      for (const auto *Parameter : Contract.ParameterTypes)
        Contract.ParameterOwnerships.push_back(ParameterOwnership(Parameter));
      Contract.ResultType = Signature->elements.back();
      Contract.ResultContract = ResultOwnership(Contract.ResultType);
      Contract.Effects = arena_.effect_solver().empty();
      Contract.CallableContracts = Operation.callable_contracts;
      Add({model::BinderOwnerKind::Operation,
           Operation.key,
           Operation.type_argument_count,
           static_cast<std::size_t>(std::ranges::count_if(
               Operation.signature.quantified_effect_variables,
               [](const auto &Variable) {
                 return Variable.Kind == model::EffectVariableKind::Flexible;
               })),
           {},
           false,
           Range,
           std::nullopt,
           std::move(Contract)});
    }
  const auto AddTarget = [&](const TraitMethodSelectionProjection &Selection) {
    const auto *Resolved =
        std::get_if<ResolvedTraitTargetProjection>(&Selection.Evidence);
    if (!Resolved ||
        std::visit(
            [&](const auto &Value) { return Value.Module == current_module_; },
            Resolved->SelectedTarget))
      return;
    if (HasFunction(Resolved->SelectedTarget))
      return;
    Add({model::BinderOwnerKind::Function,
         Resolved->SelectedTarget,
         Resolved->TargetTypeArguments.size(),
         Resolved->TargetEffectArguments.size(),
         {},
         false,
         Range});
  };
  for (const auto &[_, Selection] : trait_method_uses_)
    AddTarget(Selection);
  for (const auto &[_, Operations] : collection_key_operations_) {
    AddTarget(Operations.Hash);
    AddTarget(Operations.Equals);
  }

  std::ranges::sort(Output, [&](const auto &Left, const auto &Right) {
    return canonicalIdentity(Left) < canonicalIdentity(Right);
  });
}

void TypeChecker::refreshStructuralProjection(
    ast::AstNode *Root, MonoTypePtr RootType,
    std::size_t VisibleParameterCount) {
  projection_root_ = Root;
  projection_root_type_ = RootType;
  projection_visible_parameter_count_ = VisibleParameterCount;
  auto &Storage = *projection_storage_;
  Storage.clear();
  if (!Root || !RootType) {
    Storage.publish();
    return;
  }
  Storage.SourceRoot = Root;
  Storage.SourceExtent = sourceExtent(diag_, Root);

  std::unordered_map<MonoTypePtr, MonoTypePtr> Rebuilt;
  std::function<MonoTypePtr(MonoTypePtr)> rebuild = [&](MonoTypePtr Input) {
    Input = unifier_.resolve(Input);
    if (!Input)
      return static_cast<MonoTypePtr>(nullptr);
    if (const auto Found = Rebuilt.find(Input); Found != Rebuilt.end())
      return Found->second;
    MonoTypePtr Result = nullptr;
    switch (Input->tag) {
    case MonoType::Var:
    case MonoType::Con:
      Result = Input;
      break;
    case MonoType::App: {
      std::vector<MonoTypePtr> Arguments;
      Arguments.reserve(Input->args.size());
      for (const auto *Argument : Input->args)
        Arguments.push_back(rebuild(Argument));
      if (Input->type_name == "Promise" && Input->promise_result_contract &&
          !Arguments.empty())
        Result = arena_.make_promise(Arguments.front(),
                                     *Input->promise_result_contract,
                                     Input->promise_effects);
      else
        Result = Input->resolved_key
                     ? arena_.make_app(*Input->resolved_key,
                                       std::move(Arguments), Input->type_name)
                     : arena_.make_app(Input->type_name, std::move(Arguments));
      break;
    }
    case MonoType::Arrow:
      Result =
          arena_.make_arrow(rebuild(Input->param_type),
                            rebuild(Input->return_type), Input->arrow_effect);
      break;
    case MonoType::MTuple: {
      std::vector<MonoTypePtr> Elements;
      Elements.reserve(Input->elements.size());
      for (const auto *Element : Input->elements)
        Elements.push_back(rebuild(Element));
      Result = arena_.make_tuple(std::move(Elements));
      break;
    }
    case MonoType::MRecord: {
      std::vector<std::pair<std::string, MonoTypePtr>> Fields;
      Fields.reserve(Input->record_fields.size());
      for (const auto &[Name, Field] : Input->record_fields)
        Fields.emplace_back(Name, rebuild(Field));
      Result = arena_.make_record(std::move(Fields), rebuild(Input->row_rest));
      break;
    }
    case MonoType::Sum: {
      std::vector<MonoTypePtr> Alternatives;
      Alternatives.reserve(Input->sum_alternatives.size());
      for (const auto *Alternative : Input->sum_alternatives)
        Alternatives.push_back(rebuild(Alternative));
      Result = arena_.make_sum(std::move(Alternatives));
      break;
    }
    }
    Rebuilt.emplace(Input, Result);
    return Result;
  };

  const auto *ZonedRoot = rebuild(RootType);
  Storage.RootScheme =
      generalize(ZonedRoot, -1,
                 dynamic_cast<const ast::FunctionExpr *>(Root)
                     ? constraints_for_function(
                           dynamic_cast<const ast::FunctionExpr *>(Root))
                     : std::vector<Constraint>{});

  std::vector<std::pair<const ast::AstNode *, MonoTypePtr>> OrderedFacts;
  OrderedFacts.reserve(type_map_.size());
  for (const auto &[Node, Type] : type_map_)
    if (Node && Type) {
      const ast::FunctionExpr *NearestFunction = nullptr;
      for (auto *Current = Node; Current; Current = Current->parent)
        if ((NearestFunction =
                 dynamic_cast<const ast::FunctionExpr *>(Current)))
          break;
      if (NearestFunction && NearestFunction != Root)
        continue;
      OrderedFacts.emplace_back(Node, rebuild(Type));
    }
  std::ranges::sort(OrderedFacts, [](const auto &Left, const auto &Right) {
    if (Left.first->Range.Offset != Right.first->Range.Offset)
      return Left.first->Range.Offset < Right.first->Range.Offset;
    if (Left.first->Range.Length != Right.first->Range.Length)
      return Left.first->Range.Length < Right.first->Range.Length;
    return Left.first < Right.first;
  });
  Storage.NodeFacts.reserve(OrderedFacts.size());
  for (const auto &[Node, InferredType] : OrderedFacts) {
    const auto *Type = Node == Root ? Storage.RootScheme->body : InferredType;
    std::optional<SchemeInstantiationProjection> Instantiation;
    if (const auto Found = instantiations_.find(Node);
        Found != instantiations_.end()) {
      Instantiation = Found->second;
      for (auto *&Argument : Instantiation->TypeArguments)
        Argument = rebuild(Argument);
    }
    auto Occurrence = operation_occurrences_.find(Node);
    std::optional<OperationOccurrenceProjection> Operation;
    if (Occurrence != operation_occurrences_.end()) {
      Operation = Occurrence->second;
      std::visit(
          [&](auto &Fact) {
            for (auto *&Argument : Fact.Application.TypeArguments)
              Argument = rebuild(Argument);
            for (auto *&Parameter : Fact.Application.ParameterTypes)
              Parameter = rebuild(Parameter);
            Fact.Application.ResultType = rebuild(Fact.Application.ResultType);
            if constexpr (std::is_same_v<std::decay_t<decltype(Fact)>,
                                         PerformProjection>)
              Fact.ContinuationResultType =
                  rebuild(Fact.ContinuationResultType);
            else
              Fact.SourceResumeType = rebuild(Fact.SourceResumeType);
          },
          *Operation);
    }
    const auto Effects = node_effects_.find(Node);
    std::optional<TraitMethodSelectionProjection> TraitUse;
    if (const auto Found = trait_method_uses_.find(Node);
        Found != trait_method_uses_.end())
      TraitUse = Found->second;
    std::optional<CollectionKeyOperationsProjection> KeyOperations;
    if (const auto Found = collection_key_operations_.find(Node);
        Found != collection_key_operations_.end())
      KeyOperations = Found->second;
    std::optional<SumInjectionProjection> SumInjection;
    if (const auto Found = sum_injections_.find(Node);
        Found != sum_injections_.end())
      SumInjection = SumInjectionProjection{rebuild(Found->second.Alternative),
                                            rebuild(Found->second.Sum)};
    Storage.NodeFacts.push_back(NodeSemanticProjection{
        Node, Type,
        Effects == node_effects_.end() ? arena_.effect_solver().empty()
                                       : Effects->second,
        std::move(Instantiation), std::move(Operation), std::move(TraitUse),
        std::move(KeyOperations), std::move(SumInjection), Node->Range});
  }

  auto parameterOwnership = [](MonoTypePtr Type) {
    if (Type && Type->tag == MonoType::Con && Type->con != TyCon::String)
      return model::ParameterOwnership::Trivial;
    return model::ParameterOwnership::Borrow;
  };
  auto resultOwnership = [](MonoTypePtr Type) {
    if (Type && Type->tag == MonoType::Con && Type->con != TyCon::String)
      return model::ResultOwnership::Trivial;
    return model::ResultOwnership::Owned;
  };

  for (const auto &[Node, _] : OrderedFacts) {
    const auto Recorded = expression_callable_contracts_.find(Node);
    if (Recorded == expression_callable_contracts_.end())
      continue;
    for (const auto &Contract : Recorded->second) {
      auto Copy = Contract;
      Copy.Arrow = rebuild(Contract.Arrow);
      const auto Duplicate = std::ranges::find_if(
          Storage.CallableContracts, [&](const auto &Existing) {
            return Existing.Arrow == Copy.Arrow &&
                   Existing.ParameterOwnerships == Copy.ParameterOwnerships &&
                   Existing.ResultContract == Copy.ResultContract &&
                   Existing.Convention == Copy.Convention;
          });
      if (Duplicate == Storage.CallableContracts.end())
        Storage.CallableContracts.push_back(std::move(Copy));
    }
  }
  const auto remapContractArrows = [&](const auto &Self, MonoTypePtr Before,
                                       MonoTypePtr After) -> void {
    Before = unifier_.resolve(Before);
    After = unifier_.resolve(After);
    if (!Before || !After || Before->tag != After->tag)
      return;
    if (Before->tag == MonoType::Arrow) {
      for (auto &Contract : Storage.CallableContracts)
        if (Contract.Arrow == Before)
          Contract.Arrow = After;
      Self(Self, Before->param_type, After->param_type);
      Self(Self, Before->return_type, After->return_type);
      return;
    }
    if (Before->tag == MonoType::App) {
      for (std::size_t Index = 0;
           Index < Before->args.size() && Index < After->args.size(); ++Index)
        Self(Self, Before->args[Index], After->args[Index]);
    } else if (Before->tag == MonoType::MTuple) {
      for (std::size_t Index = 0;
           Index < Before->elements.size() && Index < After->elements.size();
           ++Index)
        Self(Self, Before->elements[Index], After->elements[Index]);
    } else if (Before->tag == MonoType::MRecord) {
      for (std::size_t Index = 0; Index < Before->record_fields.size() &&
                                  Index < After->record_fields.size();
           ++Index)
        Self(Self, Before->record_fields[Index].second,
             After->record_fields[Index].second);
      Self(Self, Before->row_rest, After->row_rest);
    } else if (Before->tag == MonoType::Sum) {
      for (std::size_t Index = 0; Index < Before->sum_alternatives.size() &&
                                  Index < After->sum_alternatives.size();
           ++Index)
        Self(Self, Before->sum_alternatives[Index],
             After->sum_alternatives[Index]);
    }
  };
  remapContractArrows(remapContractArrows, ZonedRoot, Storage.RootScheme->body);
  refine_callable_contract_ownership(Storage.CallableContracts);

  if (ZonedRoot->tag == MonoType::Arrow) {
    auto RootContract = std::ranges::find_if(
        Storage.CallableContracts, [&](const auto &Contract) {
          return Contract.Arrow == Storage.RootScheme->body;
        });
    if (RootContract != Storage.CallableContracts.end())
      Storage.RootOwnerships = RootContract->ParameterOwnerships;
    else {
      Storage.clear();
      Storage.publish();
      return;
    }
  }

  const model::FunctionDeclarationIdentity Identity =
      model::LocalFunctionIdentity{current_module_, {0}};
  Storage.Scopes.push_back(DeclarationScopeProjection{
      1, std::nullopt, model::BinderOwnerKind::Function, Identity,
      Storage.RootScheme->quantified_vars,
      Storage.RootScheme->quantified_effect_variables,
      Storage.CallableContracts, Root->Range});
  const auto RootContract = std::ranges::find_if(
      Storage.CallableContracts, [&](const auto &Contract) {
        return Contract.Arrow == Storage.RootScheme->body;
      });
  const auto ResultContract = ZonedRoot->tag == MonoType::Arrow
                                  ? RootContract->ResultContract
                                  : resultOwnership(ZonedRoot);
  Storage.Functions.push_back(FunctionSchemeProjection{
      1, *Storage.RootScheme, Storage.RootOwnerships, ResultContract,
      model::CallingConvention::DirectYona, Identity, std::nullopt,
      DeclarationVisibility::Private, Storage.NodeFacts, Root->Range, Root});

  ProjectionDeclarationId NextScope = 2;
  std::vector<const ast::FunctionExpr *> LocalFunctions;
  LocalFunctions.reserve(function_schemes_.size());
  for (const auto &[Function, _] : projection_function_types_)
    if (Function != Root)
      LocalFunctions.push_back(Function);
  std::ranges::sort(LocalFunctions, [](const auto *Left, const auto *Right) {
    if (Left->Range.Offset != Right->Range.Offset)
      return Left->Range.Offset < Right->Range.Offset;
    return Left < Right;
  });
  std::unordered_map<const ast::FunctionExpr *, ProjectionDeclarationId>
      LocalScopes;
  for (const auto *Function : LocalFunctions)
    LocalScopes.emplace(Function, NextScope++);

  for (const auto *Function : LocalFunctions) {
    const auto Type = type_map_.find(const_cast<ast::FunctionExpr *>(Function));
    if (Type == type_map_.end() || !Type->second)
      continue;
    Storage.ModuleSchemes.push_back(
        generalize(Type->second, -1, constraints_for_function(Function)));
    auto &Scheme = Storage.ModuleSchemes.back();

    Storage.ModuleCallableContracts.emplace_back();
    auto &Contracts = Storage.ModuleCallableContracts.back();
    if (const auto Found = function_callable_contracts_.find(Function);
        Found != function_callable_contracts_.end())
      for (const auto &Recorded : Found->second) {
        auto Copy = Recorded;
        Copy.Arrow = rebuild(Recorded.Arrow);
        Contracts.push_back(std::move(Copy));
      }
    const auto BeforeRoot = rebuild(Type->second);
    const auto remapLocalContracts = [&](const auto &Self, MonoTypePtr Before,
                                         MonoTypePtr After) -> void {
      Before = unifier_.resolve(Before);
      After = unifier_.resolve(After);
      if (!Before || !After || Before->tag != After->tag)
        return;
      if (Before->tag == MonoType::Arrow) {
        for (auto &Contract : Contracts)
          if (Contract.Arrow == Before)
            Contract.Arrow = After;
        Self(Self, Before->param_type, After->param_type);
        Self(Self, Before->return_type, After->return_type);
      } else if (Before->tag == MonoType::App) {
        for (std::size_t Index = 0;
             Index < Before->args.size() && Index < After->args.size(); ++Index)
          Self(Self, Before->args[Index], After->args[Index]);
      } else if (Before->tag == MonoType::MTuple) {
        for (std::size_t Index = 0;
             Index < Before->elements.size() && Index < After->elements.size();
             ++Index)
          Self(Self, Before->elements[Index], After->elements[Index]);
      } else if (Before->tag == MonoType::MRecord) {
        for (std::size_t Index = 0; Index < Before->record_fields.size() &&
                                    Index < After->record_fields.size();
             ++Index)
          Self(Self, Before->record_fields[Index].second,
               After->record_fields[Index].second);
        Self(Self, Before->row_rest, After->row_rest);
      } else if (Before->tag == MonoType::Sum) {
        for (std::size_t Index = 0; Index < Before->sum_alternatives.size() &&
                                    Index < After->sum_alternatives.size();
             ++Index)
          Self(Self, Before->sum_alternatives[Index],
               After->sum_alternatives[Index]);
      }
    };
    remapLocalContracts(remapLocalContracts, BeforeRoot, Scheme.body);
    refine_callable_contract_ownership(Contracts);
    auto RootContract =
        std::ranges::find_if(Contracts, [&](const auto &Contract) {
          return Contract.Arrow == Scheme.body;
        });
    Storage.ModuleOwnerships.emplace_back();
    auto &Ownerships = Storage.ModuleOwnerships.back();
    auto LocalResultContract = resultOwnership(Scheme.body);
    if (RootContract != Contracts.end()) {
      RootContract->Convention = model::CallingConvention::ClosureEntry;
      Ownerships = RootContract->ParameterOwnerships;
      LocalResultContract = RootContract->ResultContract;
    }

    Storage.ModuleNodeFacts.emplace_back();
    auto &Facts = Storage.ModuleNodeFacts.back();
    for (const auto &[Node, NodeType] : type_map_) {
      const ast::FunctionExpr *NearestFunction = nullptr;
      for (auto *Current = Node; Current; Current = Current->parent)
        if ((NearestFunction =
                 dynamic_cast<const ast::FunctionExpr *>(Current)))
          break;
      if (NearestFunction != Function || !NodeType)
        continue;
      std::optional<SchemeInstantiationProjection> Instantiation;
      if (const auto Found = instantiations_.find(Node);
          Found != instantiations_.end()) {
        Instantiation = Found->second;
        for (auto *&Argument : Instantiation->TypeArguments)
          Argument = rebuild(Argument);
      }
      std::optional<OperationOccurrenceProjection> Operation;
      if (const auto Found = operation_occurrences_.find(Node);
          Found != operation_occurrences_.end()) {
        Operation = Found->second;
        std::visit(
            [&](auto &Fact) {
              for (auto *&Argument : Fact.Application.TypeArguments)
                Argument = rebuild(Argument);
              for (auto *&Parameter : Fact.Application.ParameterTypes)
                Parameter = rebuild(Parameter);
              Fact.Application.ResultType =
                  rebuild(Fact.Application.ResultType);
              if constexpr (std::is_same_v<std::decay_t<decltype(Fact)>,
                                           PerformProjection>)
                Fact.ContinuationResultType =
                    rebuild(Fact.ContinuationResultType);
              else
                Fact.SourceResumeType = rebuild(Fact.SourceResumeType);
            },
            *Operation);
      }
      const auto Effects = node_effects_.find(Node);
      std::optional<TraitMethodSelectionProjection> TraitUse;
      if (const auto Found = trait_method_uses_.find(Node);
          Found != trait_method_uses_.end())
        TraitUse = Found->second;
      std::optional<CollectionKeyOperationsProjection> KeyOperations;
      if (const auto Found = collection_key_operations_.find(Node);
          Found != collection_key_operations_.end())
        KeyOperations = Found->second;
      std::optional<SumInjectionProjection> SumInjection;
      if (const auto Found = sum_injections_.find(Node);
          Found != sum_injections_.end())
        SumInjection = SumInjectionProjection{
            rebuild(Found->second.Alternative), rebuild(Found->second.Sum)};
      Facts.push_back(NodeSemanticProjection{
          Node, Node == Function ? Scheme.body : rebuild(NodeType),
          Effects == node_effects_.end() ? arena_.effect_solver().empty()
                                         : Effects->second,
          std::move(Instantiation), std::move(Operation), std::move(TraitUse),
          std::move(KeyOperations), std::move(SumInjection), Node->Range});
    }
    std::ranges::sort(Facts, [](const auto &Left, const auto &Right) {
      if (Left.Range.Offset != Right.Range.Offset)
        return Left.Range.Offset < Right.Range.Offset;
      if (Left.Range.Length != Right.Range.Length)
        return Left.Range.Length < Right.Range.Length;
      return Left.Node < Right.Node;
    });

    std::optional<ProjectionDeclarationId> Parent = 1;
    for (auto *Current = Function->parent; Current; Current = Current->parent) {
      const auto *Enclosing = dynamic_cast<const ast::FunctionExpr *>(Current);
      if (!Enclosing)
        continue;
      if (const auto Found = LocalScopes.find(Enclosing);
          Found != LocalScopes.end())
        Parent = Found->second;
      break;
    }
    const auto Scope = LocalScopes.at(Function);
    const model::FunctionDeclarationIdentity LocalIdentity =
        model::LocalFunctionIdentity{
            current_module_,
            {static_cast<std::uint32_t>(Function->Range.Offset)}};
    Storage.Scopes.push_back(DeclarationScopeProjection{
        Scope, Parent, model::BinderOwnerKind::Function, LocalIdentity,
        Scheme.quantified_vars, Scheme.quantified_effect_variables, Contracts,
        Function->Range});
    Storage.Functions.push_back(FunctionSchemeProjection{
        Scope, Scheme, Ownerships, LocalResultContract,
        model::CallingConvention::ClosureEntry, LocalIdentity, std::nullopt,
        DeclarationVisibility::Private, Facts, Function->Range, Function});
  }

  std::vector<const EffectOpInfo *> RegisteredOperations;
  RegisteredOperations.reserve(effect_ops_.size());
  for (const auto &[_, Operation] : effect_ops_)
    RegisteredOperations.push_back(&Operation);
  std::ranges::sort(RegisteredOperations,
                    [](const EffectOpInfo *Left, const EffectOpInfo *Right) {
                      return Left->key < Right->key;
                    });
  for (const auto *Operation : RegisteredOperations) {
    if (Operation->key.Effect.Module != current_module_)
      continue;
    const auto *Signature = Operation->signature.body;
    if (!Signature || Signature->tag != MonoType::MTuple ||
        Signature->elements.size() != Operation->parameter_count + 1)
      continue;
    Storage.OperationOwnerships.emplace_back();
    auto &Ownerships = Storage.OperationOwnerships.back();
    Ownerships.reserve(Operation->parameter_count);
    for (std::size_t Index = 0; Index < Operation->parameter_count; ++Index)
      Ownerships.push_back(parameterOwnership(Signature->elements[Index]));

    Storage.OperationCallableContracts.emplace_back();
    auto &Contracts = Storage.OperationCallableContracts.back();
    Contracts = Operation->callable_contracts;

    const auto ScopeId = NextScope++;
    Storage.Scopes.push_back(DeclarationScopeProjection{
        ScopeId, std::nullopt, model::BinderOwnerKind::Operation,
        Operation->key, Operation->signature.quantified_vars,
        Operation->signature.quantified_effect_variables, Contracts,
        Root->Range});
    Storage.Operations.push_back(OperationSchemeProjection{
        ScopeId, Operation->key, Operation->signature.quantified_vars,
        Operation->signature.quantified_effect_variables,
        Operation->signature.constraints,
        std::span<const MonoTypePtr>(Signature->elements.data(),
                                     Operation->parameter_count),
        Ownerships, Signature->elements.back(),
        resultOwnership(Signature->elements.back()),
        DeclarationVisibility::Module, arena_.effect_solver().empty(),
        Root->Range});
  }
  populateImportedProjectionCatalog(Root->Range);
  Storage.publish();
}

void TypeChecker::refreshModuleStructuralProjection(
    ast::ModuleDecl *Module,
    const std::vector<std::pair<ast::FunctionExpr *, MonoTypePtr>> &Functions) {
  auto &Storage = *projection_storage_;
  Storage.clear();
  if (!Module) {
    Storage.publish();
    return;
  }
  Storage.SourceRoot = Module;
  Storage.SourceExtent = sourceExtent(diag_, Module);

  const auto parameterOwnership = [](MonoTypePtr Type, bool Borrowed = false) {
    if (Type && Type->tag == MonoType::Con && Type->con != TyCon::String)
      return model::ParameterOwnership::Trivial;
    return Borrowed ? model::ParameterOwnership::Borrow
                    : model::ParameterOwnership::Consume;
  };
  const auto resultOwnership = [](MonoTypePtr Type) {
    if (Type && Type->tag == MonoType::Con && Type->con != TyCon::String)
      return model::ResultOwnership::Trivial;
    return model::ResultOwnership::Owned;
  };
  const auto exported = [](std::span<const std::string> Names,
                           std::string_view Name) {
    return std::ranges::find(Names, Name) != Names.end();
  };

  ProjectionDeclarationId NextScope = 1;
  for (const auto &Extern : projection_extern_declarations_) {
    if (!Extern.Node)
      continue;
    Storage.ModuleSchemes.push_back(Extern.Scheme);
    auto &Scheme = Storage.ModuleSchemes.back();
    Storage.ModuleOwnerships.push_back(Extern.ParameterOwnerships);
    auto &Ownerships = Storage.ModuleOwnerships.back();
    Storage.ModuleCallableContracts.push_back(Extern.CallableContracts);
    auto &Contracts = Storage.ModuleCallableContracts.back();
    const auto Scope = NextScope++;
    const model::FunctionDeclarationIdentity Identity =
        model::SymbolIdentity{current_module_, Extern.Node->name};
    Storage.Scopes.push_back(DeclarationScopeProjection{
        Scope, std::nullopt, model::BinderOwnerKind::Function, Identity,
        Scheme.quantified_vars, Scheme.quantified_effect_variables, Contracts,
        Extern.Node->Range});
    const bool Public = exported(Module->exports, Extern.Node->name);
    Storage.Functions.push_back(FunctionSchemeProjection{
        Scope,
        Scheme,
        Ownerships,
        Extern.ResultContract,
        model::CallingConvention::NativeExtern,
        Identity,
        Public ? std::optional<model::SymbolIdentity>{model::SymbolIdentity{
                     current_module_, Extern.Node->name}}
               : std::nullopt,
        Public ? DeclarationVisibility::Public : DeclarationVisibility::Private,
        {},
        Extern.Node->Range,
        Extern.Node});
  }
  for (auto *Declaration : Module->adt_declarations) {
    if (!Declaration)
      continue;
    const model::NominalTypeKey Key{current_module_, Declaration->name};
    const auto Recorded = projection_nominal_declarations_.find(Declaration);
    if (Recorded == projection_nominal_declarations_.end())
      continue;
    Storage.ModuleTypeParameters.emplace_back();
    auto &Parameters = Storage.ModuleTypeParameters.back();
    for (const auto *Variable : Recorded->second.TypeParameters)
      Parameters.push_back(Variable->var_id);
    Storage.ModuleCallableContracts.emplace_back();
    auto &Contracts = Storage.ModuleCallableContracts.back();
    Contracts = Recorded->second.CallableContracts;

    Storage.Constructors.emplace_back();
    auto &Constructors = Storage.Constructors.back();
    Constructors.reserve(Declaration->variants.size());
    for (std::size_t Tag = 0; Tag < Declaration->variants.size(); ++Tag) {
      const auto *Constructor = Declaration->variants[Tag];
      if (!Constructor)
        continue;
      Storage.ConstructorFields.emplace_back();
      auto &Fields = Storage.ConstructorFields.back();
      if (Tag < Recorded->second.ConstructorFields.size())
        Fields = Recorded->second.ConstructorFields[Tag];
      Constructors.push_back(ConstructorProjection{
          Constructor->name, static_cast<std::uint32_t>(Tag), Fields,
          Constructor->Range});
    }
    const auto Scope = NextScope++;
    Storage.Scopes.push_back(
        DeclarationScopeProjection{Scope,
                                   std::nullopt,
                                   model::BinderOwnerKind::Nominal,
                                   Key,
                                   Parameters,
                                   {},
                                   Contracts,
                                   Declaration->Range});
    Storage.Nominals.push_back(NominalDeclarationProjection{
        Scope, Key, Constructors,
        exported(Module->exported_types, Declaration->name)
            ? DeclarationVisibility::Public
            : DeclarationVisibility::Private,
        exported(Module->opaque_exported_types, Declaration->name),
        Declaration->Range});
    if (!Declaration->derive_traits.empty()) {
      Storage.DerivedTraits.emplace_back();
      auto &Traits = Storage.DerivedTraits.back();
      Traits.reserve(Declaration->derive_traits.size());
      for (const auto &Trait : Declaration->derive_traits)
        Traits.push_back(resolved_trait_key(Trait));
      Storage.Derivations.push_back(
          DeriveRequestProjection{Key, Traits, Declaration->Range});
    }
  }

  std::unordered_map<std::string, ProjectionDeclarationId> TraitMethodScopes;
  for (auto *Declaration : Module->trait_declarations) {
    if (!Declaration)
      continue;
    const model::NominalTypeKey Key{current_module_, Declaration->name};
    Storage.ModuleTypeParameters.emplace_back();
    auto &Parameters = Storage.ModuleTypeParameters.back();
    std::unordered_map<std::string, MonoTypePtr> TraitVariables;
    const auto RecordedParameters =
        projection_trait_type_parameters_.find(Declaration);
    for (std::size_t Index = 0; Index < Declaration->type_params.size();
         ++Index) {
      const auto &Name = Declaration->type_params[Index];
      auto *Variable =
          RecordedParameters != projection_trait_type_parameters_.end() &&
                  Index < RecordedParameters->second.size()
              ? RecordedParameters->second[Index]
              : arena_.fresh_var(0);
      if (RecordedParameters == projection_trait_type_parameters_.end() ||
          Index >= RecordedParameters->second.size())
        uf_.add_var(Variable->var_id, 0);
      TraitVariables.emplace(Name, Variable);
      Parameters.push_back(Variable->var_id);
    }
    Storage.DeclarationConstraints.emplace_back();
    auto &Superclasses = Storage.DeclarationConstraints.back();
    for (const auto &[Superclass, ParameterName] : Declaration->superclasses) {
      const auto Name =
          ParameterName.empty() && !Declaration->type_params.empty()
              ? Declaration->type_params.front()
              : ParameterName;
      const auto Parameter = TraitVariables.find(Name);
      if (Parameter != TraitVariables.end())
        Superclasses.emplace_back(resolved_trait_key(Superclass),
                                  Parameter->second);
    }
    const auto TraitScope = NextScope++;
    Storage.Scopes.push_back(
        DeclarationScopeProjection{TraitScope,
                                   std::nullopt,
                                   model::BinderOwnerKind::Trait,
                                   Key,
                                   Parameters,
                                   {},
                                   {},
                                   Declaration->Range});

    Storage.TraitMethods.emplace_back();
    auto &Methods = Storage.TraitMethods.back();
    const auto RecordedMethods = projection_trait_methods_.find(Declaration);
    if (RecordedMethods == projection_trait_methods_.end() ||
        RecordedMethods->second.size() != Declaration->methods.size())
      continue;
    for (std::size_t MethodIndex = 0; MethodIndex < Declaration->methods.size();
         ++MethodIndex) {
      const auto &Method = Declaration->methods[MethodIndex];
      const auto &Seed = RecordedMethods->second[MethodIndex];
      Storage.ModuleSchemes.push_back(Seed.Scheme);
      auto &Scheme = Storage.ModuleSchemes.back();

      Storage.ModuleOwnerships.emplace_back();
      auto &Ownerships = Storage.ModuleOwnerships.back();
      Ownerships = Seed.ParameterOwnerships;
      Storage.ModuleCallableContracts.emplace_back();
      auto &Contracts = Storage.ModuleCallableContracts.back();
      Contracts = Seed.CallableContracts;
      const auto MethodScope = NextScope++;
      const auto Identity =
          model::FunctionDeclarationIdentity{model::SymbolIdentity{
              current_module_, Declaration->name + "_" + Method.name}};
      Storage.Scopes.push_back(DeclarationScopeProjection{
          MethodScope, TraitScope, model::BinderOwnerKind::Function, Identity,
          Scheme.quantified_vars, Scheme.quantified_effect_variables, Contracts,
          Declaration->Range});
      TraitMethodScopes.emplace(Declaration->name + "." + Method.name,
                                MethodScope);
      std::optional<ResolvedTraitTargetProjection> DefaultTarget;
      if (Method.default_impl) {
        const auto Target =
            projection_function_identities_.find(Method.default_impl);
        if (Target != projection_function_identities_.end())
          DefaultTarget = ResolvedTraitTargetProjection{Target->second, {}, {}};
      }
      Methods.push_back(TraitMethodProjection{
          MethodScope, Method.name, Scheme, Ownerships, Seed.ResultContract,
          model::CallingConvention::DirectYona, std::move(DefaultTarget),
          Declaration->Range});
    }
    Storage.Traits.push_back(TraitDeclarationProjection{
        TraitScope, Key, Superclasses, Methods,
        exported(Module->exported_traits, Declaration->name)
            ? DeclarationVisibility::Public
            : DeclarationVisibility::Private,
        Declaration->Range,
        Declaration->name == "Eq" || Declaration->name == "Hash" ||
            Declaration->name == "Ord" || Declaration->name == "Show" ||
            Declaration->name == "Send" || Declaration->name == "Shareable"});
  }

  const auto simpleInstanceType = [&](std::string_view Name) -> MonoTypePtr {
    if (Name == "Int")
      return arena_.make_con(TyCon::Int);
    if (Name == "Float")
      return arena_.make_con(TyCon::Float);
    if (Name == "Bool")
      return arena_.make_con(TyCon::Bool);
    if (Name == "String")
      return arena_.make_con(TyCon::String);
    if (Name == "Char")
      return arena_.make_con(TyCon::Char);
    if (Name == "Byte")
      return arena_.make_con(TyCon::Byte);
    if (Name == "Symbol")
      return arena_.make_con(TyCon::Symbol);
    if (Name == "Unit" || Name == "()")
      return arena_.make_con(TyCon::Unit);
    if (Name == "ByteArray")
      return arena_.make_con(TyCon::ByteArray);
    if (Name == "IntArray" || Name == "FloatArray")
      return arena_.make_app(std::string(Name), {});
    if (const auto Key = resolved_nominal_key(Name))
      return arena_.make_app(*Key, {}, std::string(Name));
    return arena_.make_app(
        model::NominalTypeKey{current_module_, std::string(Name)}, {},
        std::string(Name));
  };
  std::unordered_map<const ast::InstanceDeclNode *, ProjectionDeclarationId>
      InstanceScopes;
  for (auto *Declaration : Module->instance_declarations) {
    if (!Declaration)
      continue;
    Storage.ModuleTypeParameters.emplace_back();
    auto &Parameters = Storage.ModuleTypeParameters.back();
    std::unordered_map<std::string, MonoTypePtr> Variables;
    const auto RecordedParameters =
        projection_instance_type_parameters_.find(Declaration);
    for (std::size_t Index = 0; Index < Declaration->type_params.size();
         ++Index) {
      const auto &Name = Declaration->type_params[Index];
      auto *Variable =
          RecordedParameters != projection_instance_type_parameters_.end() &&
                  Index < RecordedParameters->second.size()
              ? RecordedParameters->second[Index]
              : arena_.fresh_var(0);
      if (RecordedParameters == projection_instance_type_parameters_.end() ||
          Index >= RecordedParameters->second.size())
        uf_.add_var(Variable->var_id, 0);
      Variables.emplace(Name, Variable);
      Parameters.push_back(Variable->var_id);
    }
    Storage.InstanceArguments.emplace_back();
    auto &Arguments = Storage.InstanceArguments.back();
    for (std::size_t Index = 0; Index < Declaration->type_names.size();
         ++Index) {
      const auto &Name = Declaration->type_names[Index];
      if (const auto Variable = Variables.find(Name);
          Variable != Variables.end()) {
        Arguments.push_back(Variable->second);
        continue;
      }
      auto *Head = simpleInstanceType(Name);
      if (!Variables.empty() && (Index == 0 || Name == "Tuple")) {
        std::vector<MonoTypePtr> TypeArguments;
        TypeArguments.reserve(Declaration->type_params.size());
        for (const auto &Parameter : Declaration->type_params)
          TypeArguments.push_back(Variables.at(Parameter));
        if (Name == "Tuple")
          Head = arena_.make_tuple(std::move(TypeArguments));
        else if (Name == "Seq" || Name == "Set" || Name == "Dict")
          Head = arena_.make_app(Name, std::move(TypeArguments));
        else if (Head->tag == MonoType::App)
          Head =
              Head->resolved_key
                  ? arena_.make_app(*Head->resolved_key,
                                    std::move(TypeArguments), Head->type_name)
                  : arena_.make_app(Head->type_name, std::move(TypeArguments));
      }
      Arguments.push_back(Head);
    }
    Storage.DeclarationConstraints.emplace_back();
    auto &Constraints = Storage.DeclarationConstraints.back();
    for (const auto &[Trait, Parameter] : Declaration->constraints) {
      if (const auto Found = Variables.find(Parameter);
          Found != Variables.end())
        Constraints.emplace_back(resolved_trait_key(Trait), Found->second);
    }
    const auto Trait = resolved_trait_key(Declaration->trait_name);
    const auto Scope = NextScope++;
    Storage.Scopes.push_back(DeclarationScopeProjection{
        Scope,
        std::nullopt,
        model::BinderOwnerKind::Instance,
        InstanceScopeIdentityProjection{Trait, Arguments, Constraints},
        Parameters,
        {},
        {},
        Declaration->Range});
    InstanceScopes.emplace(Declaration, Scope);
    Storage.InstanceMethods.emplace_back();
    auto &Methods = Storage.InstanceMethods.back();
    for (const auto *Method : Declaration->methods) {
      if (!Method)
        continue;
      const auto MethodScope =
          TraitMethodScopes.find(Declaration->trait_name + "." + Method->name);
      if (MethodScope == TraitMethodScopes.end())
        continue;
      const auto Target = projection_function_identities_.find(Method);
      if (Target == projection_function_identities_.end())
        continue;
      Methods.push_back(TraitMethodBindingProjection{
          Scope, MethodScope->second, Method->name,
          ResolvedTraitTargetProjection{Target->second, {}, {}},
          Method->Range});
    }
    Storage.Instances.push_back(TraitInstanceProjection{
        Scope, Trait, Arguments, Constraints, Methods,
        DeclarationVisibility::Module, Declaration->Range});
  }

  std::unordered_map<const ast::FunctionExpr *, ProjectionDeclarationId>
      FunctionScopes;
  for (const auto &[Function, Type] : Functions) {
    if (!Function || !Type)
      continue;
    Storage.ModuleSchemes.push_back(generalize(
        unifier_.resolve(Type), -1, constraints_for_function(Function)));
    auto &Scheme = Storage.ModuleSchemes.back();
    const auto eraseInherited = [&](const auto &Owners) {
      if (const auto Owner = Owners.find(Function); Owner != Owners.end())
        if (const auto Parameters = [&]() -> const std::vector<MonoTypePtr> * {
              if constexpr (std::is_same_v<std::decay_t<decltype(Owners)>,
                                           decltype(trait_default_owners_)>) {
                const auto Found =
                    projection_trait_type_parameters_.find(Owner->second);
                return Found == projection_trait_type_parameters_.end()
                           ? nullptr
                           : &Found->second;
              } else {
                const auto Found =
                    projection_instance_type_parameters_.find(Owner->second);
                return Found == projection_instance_type_parameters_.end()
                           ? nullptr
                           : &Found->second;
              }
            }())
          std::erase_if(Scheme.quantified_vars, [&](TypeId Variable) {
            return std::ranges::any_of(*Parameters, [&](MonoTypePtr Parameter) {
              return Parameter && Parameter->var_id == Variable;
            });
          });
    };
    eraseInherited(trait_default_owners_);
    eraseInherited(instance_method_owners_);
    Storage.ModuleOwnerships.emplace_back();
    auto &Ownerships = Storage.ModuleOwnerships.back();
    auto *Stage = Scheme.body;
    for (std::size_t Index = 0; Index < Function->patterns.size() && Stage &&
                                Stage->tag == MonoType::Arrow;
         ++Index) {
      const bool Borrowed = Index < Function->param_borrow.size() &&
                            Function->param_borrow[Index];
      Ownerships.push_back(parameterOwnership(Stage->param_type, Borrowed));
      Stage = Stage->return_type;
    }

    Storage.ModuleNodeFacts.emplace_back();
    auto &Facts = Storage.ModuleNodeFacts.back();
    for (const auto &[Node, NodeType] : type_map_) {
      const ast::FunctionExpr *NearestFunction = nullptr;
      for (auto *Current = Node; Current; Current = Current->parent)
        if ((NearestFunction =
                 dynamic_cast<const ast::FunctionExpr *>(Current)))
          break;
      if (NearestFunction == Function && NodeType) {
        std::optional<SchemeInstantiationProjection> Instantiation;
        if (const auto Found = instantiations_.find(Node);
            Found != instantiations_.end()) {
          Instantiation = Found->second;
          for (auto *&Argument : Instantiation->TypeArguments)
            Argument = zonk(Argument);
        }
        auto Occurrence = operation_occurrences_.find(Node);
        std::optional<OperationOccurrenceProjection> Operation;
        if (Occurrence != operation_occurrences_.end()) {
          Operation = Occurrence->second;
          std::visit(
              [&](auto &Fact) {
                for (auto *&Argument : Fact.Application.TypeArguments)
                  Argument = zonk(Argument);
                for (auto *&Parameter : Fact.Application.ParameterTypes)
                  Parameter = zonk(Parameter);
                Fact.Application.ResultType = zonk(Fact.Application.ResultType);
                if constexpr (std::is_same_v<std::decay_t<decltype(Fact)>,
                                             PerformProjection>)
                  Fact.ContinuationResultType =
                      zonk(Fact.ContinuationResultType);
                else
                  Fact.SourceResumeType = zonk(Fact.SourceResumeType);
              },
              *Operation);
        }
        const auto Effects = node_effects_.find(Node);
        std::optional<TraitMethodSelectionProjection> TraitUse;
        if (const auto Found = trait_method_uses_.find(Node);
            Found != trait_method_uses_.end())
          TraitUse = Found->second;
        std::optional<CollectionKeyOperationsProjection> KeyOperations;
        if (const auto Found = collection_key_operations_.find(Node);
            Found != collection_key_operations_.end())
          KeyOperations = Found->second;
        std::optional<SumInjectionProjection> SumInjection;
        if (const auto Found = sum_injections_.find(Node);
            Found != sum_injections_.end())
          SumInjection = SumInjectionProjection{zonk(Found->second.Alternative),
                                                zonk(Found->second.Sum)};
        Facts.push_back(NodeSemanticProjection{
            Node, Node == Function ? Scheme.body : zonk(NodeType),
            Effects == node_effects_.end() ? arena_.effect_solver().empty()
                                           : Effects->second,
            std::move(Instantiation), std::move(Operation), std::move(TraitUse),
            std::move(KeyOperations), std::move(SumInjection), Node->Range});
      }
    }
    std::ranges::sort(Facts, [](const auto &Left, const auto &Right) {
      if (Left.Range.Offset != Right.Range.Offset)
        return Left.Range.Offset < Right.Range.Offset;
      if (Left.Range.Length != Right.Range.Length)
        return Left.Range.Length < Right.Range.Length;
      return Left.Node < Right.Node;
    });

    Storage.ModuleCallableContracts.emplace_back();
    auto &Contracts = Storage.ModuleCallableContracts.back();
    if (const auto Recorded = function_callable_contracts_.find(Function);
        Recorded != function_callable_contracts_.end())
      Contracts.insert(Contracts.end(), Recorded->second.begin(),
                       Recorded->second.end());
    const auto remapContractArrows = [&](const auto &Self, MonoTypePtr Before,
                                         MonoTypePtr After) -> void {
      Before = unifier_.resolve(Before);
      After = unifier_.resolve(After);
      if (!Before || !After || Before->tag != After->tag)
        return;
      if (Before->tag == MonoType::Arrow) {
        for (auto &Contract : Contracts)
          if (Contract.Arrow == Before)
            Contract.Arrow = After;
        Self(Self, Before->param_type, After->param_type);
        Self(Self, Before->return_type, After->return_type);
      } else if (Before->tag == MonoType::App) {
        for (std::size_t Index = 0;
             Index < Before->args.size() && Index < After->args.size(); ++Index)
          Self(Self, Before->args[Index], After->args[Index]);
      } else if (Before->tag == MonoType::MTuple) {
        for (std::size_t Index = 0;
             Index < Before->elements.size() && Index < After->elements.size();
             ++Index)
          Self(Self, Before->elements[Index], After->elements[Index]);
      } else if (Before->tag == MonoType::MRecord) {
        for (std::size_t Index = 0; Index < Before->record_fields.size() &&
                                    Index < After->record_fields.size();
             ++Index)
          Self(Self, Before->record_fields[Index].second,
               After->record_fields[Index].second);
        Self(Self, Before->row_rest, After->row_rest);
      } else if (Before->tag == MonoType::Sum) {
        for (std::size_t Index = 0; Index < Before->sum_alternatives.size() &&
                                    Index < After->sum_alternatives.size();
             ++Index)
          Self(Self, Before->sum_alternatives[Index],
               After->sum_alternatives[Index]);
      }
    };
    remapContractArrows(remapContractArrows, Type, Scheme.body);
    refine_callable_contract_ownership(Contracts);
    const bool IsLocal = std::holds_alternative<model::LocalFunctionIdentity>(
        projection_function_identities_.contains(Function)
            ? projection_function_identities_.at(Function)
            : model::FunctionDeclarationIdentity{
                  model::SymbolIdentity{current_module_, Function->name}});
    const auto Convention = IsLocal ? model::CallingConvention::ClosureEntry
                                    : model::CallingConvention::DirectYona;
    if (Scheme.body && Scheme.body->tag == MonoType::Arrow) {
      const auto Root =
          std::ranges::find_if(Contracts, [&](const auto &Contract) {
            return Contract.Arrow == Scheme.body;
          });
      if (Root != Contracts.end()) {
        Root->ParameterOwnerships = Ownerships;
        Root->ResultContract = resultOwnership(Stage);
        Root->Convention = Convention;
      }
    }

    const auto ProjectedIdentity =
        projection_function_identities_.find(Function);
    const auto Identity =
        ProjectedIdentity == projection_function_identities_.end()
            ? model::FunctionDeclarationIdentity{model::SymbolIdentity{
                  current_module_, Function->name}}
            : ProjectedIdentity->second;
    std::optional<ProjectionDeclarationId> Parent;
    if (const auto Owner = trait_default_owners_.find(Function);
        Owner != trait_default_owners_.end()) {
      const auto Method =
          TraitMethodScopes.find(Owner->second->name + "." + Function->name);
      if (Method != TraitMethodScopes.end())
        Parent = Method->second;
    } else if (const auto Owner = instance_method_owners_.find(Function);
               Owner != instance_method_owners_.end()) {
      if (const auto Instance = InstanceScopes.find(Owner->second);
          Instance != InstanceScopes.end())
        Parent = Instance->second;
    } else {
      for (auto *Current = Function->parent; Current;
           Current = Current->parent) {
        const auto *Enclosing =
            dynamic_cast<const ast::FunctionExpr *>(Current);
        if (!Enclosing)
          continue;
        if (const auto Found = FunctionScopes.find(Enclosing);
            Found != FunctionScopes.end())
          Parent = Found->second;
        break;
      }
    }
    const auto Scope = NextScope++;
    FunctionScopes.emplace(Function, Scope);
    Storage.Scopes.push_back(DeclarationScopeProjection{
        Scope, Parent, model::BinderOwnerKind::Function, Identity,
        Scheme.quantified_vars, Scheme.quantified_effect_variables, Contracts,
        Function->Range});
    const auto ResultContract =
        Scheme.body && Scheme.body->tag == MonoType::Arrow
            ? std::ranges::find_if(Contracts,
                                   [&](const auto &Contract) {
                                     return Contract.Arrow == Scheme.body;
                                   })
                  ->ResultContract
            : resultOwnership(Scheme.body);
    const bool IsNested = Parent.has_value();
    const bool Public = !IsNested && exported(Module->exports, Function->name);
    Storage.Functions.push_back(FunctionSchemeProjection{
        Scope, Scheme, Ownerships, ResultContract, Convention, Identity,
        Public ? std::optional<model::SymbolIdentity>{model::SymbolIdentity{
                     current_module_, Function->name}}
               : std::nullopt,
        Public ? DeclarationVisibility::Public : DeclarationVisibility::Private,
        Facts, Function->Range, Function});
  }

  std::vector<const EffectOpInfo *> RegisteredOperations;
  RegisteredOperations.reserve(effect_ops_.size());
  for (const auto &[_, Operation] : effect_ops_)
    RegisteredOperations.push_back(&Operation);
  std::ranges::sort(RegisteredOperations,
                    [](const EffectOpInfo *Left, const EffectOpInfo *Right) {
                      return Left->key < Right->key;
                    });
  for (const auto *Operation : RegisteredOperations) {
    if (Operation->key.Effect.Module != current_module_)
      continue;
    const auto *Signature = Operation->signature.body;
    if (!Signature || Signature->tag != MonoType::MTuple ||
        Signature->elements.size() != Operation->parameter_count + 1)
      continue;
    Storage.OperationOwnerships.emplace_back();
    auto &Ownerships = Storage.OperationOwnerships.back();
    for (std::size_t Index = 0; Index < Operation->parameter_count; ++Index)
      Ownerships.push_back(parameterOwnership(Signature->elements[Index]));
    Storage.OperationCallableContracts.emplace_back();
    auto &Contracts = Storage.OperationCallableContracts.back();
    Contracts = Operation->callable_contracts;
    const auto Scope = NextScope++;
    Storage.Scopes.push_back(DeclarationScopeProjection{
        Scope, std::nullopt, model::BinderOwnerKind::Operation, Operation->key,
        Operation->signature.quantified_vars,
        Operation->signature.quantified_effect_variables, Contracts,
        Module->Range});
    Storage.Operations.push_back(OperationSchemeProjection{
        Scope, Operation->key, Operation->signature.quantified_vars,
        Operation->signature.quantified_effect_variables,
        Operation->signature.constraints,
        std::span<const MonoTypePtr>(Signature->elements.data(),
                                     Operation->parameter_count),
        Ownerships, Signature->elements.back(),
        resultOwnership(Signature->elements.back()),
        DeclarationVisibility::Module, arena_.effect_solver().empty(),
        Module->Range});
  }

  populateImportedProjectionCatalog(Module->Range);
  Storage.publish();
}

void TypeChecker::add_module_path(std::string path) {
  ModulePaths.push_back(std::move(path));
}

std::vector<std::string> TypeChecker::closed_effect_ops(MonoTypePtr type) {
  return effect_row_info(type).ops;
}

TypeChecker::EffectRowInfo TypeChecker::effect_row_info(MonoTypePtr type) {
  EffectRowInfo info;
  auto *z = unifier_.resolve(type);
  if (!z)
    return info;
  std::vector<EffectRef> stages;
  for (auto *stage = z; stage && stage->tag == MonoType::Arrow;
       stage = unifier_.resolve(stage->return_type)) {
    if (const auto effect = callee_effect(stage))
      stages.push_back(*effect);
  }
  if (!stages.empty()) {
    const auto summary = arena_.effect_solver().summarize(
        arena_.effect_solver().join(std::move(stages)));
    info.ops.reserve(summary.known_atoms.size());
    for (const auto Atom : summary.known_atoms) {
      const auto Canonical = arena_.effect_solver().canonicalAtom(Atom);
      const auto &Solved = arena_.effect_atoms().at(Canonical);
      if (const auto *Operation = std::get_if<OperationEffectAtom>(&Solved)) {
        std::string Name;
        const auto Module = Operation->Key.Effect.Module.fqn();
        if (Module != "Main" && Module != Operation->Key.Effect.Name)
          Name += Module + "\\";
        Name += Operation->Key.Effect.Name + "." + Operation->Key.Operation;
        info.ops.push_back(std::move(Name));
      } else {
        info.ops.push_back(arena_.effect_atoms().format(Canonical));
      }
    }
    std::ranges::sort(info.ops);
    info.open_rest = summary.is_open();
  } else if (z->tag == MonoType::Var) {
    // An unresolved function type has no proof of effect-freedom.
    info.open_rest = true;
  }
  if (z && z->tag == MonoType::Arrow) {
    auto *p = unifier_.resolve(z->param_type);
    info.hof = p && p->tag == MonoType::Arrow;
  }
  return info;
}

TypeChecker::InterfaceSignature TypeChecker::serialize_interface_signature(
    MonoTypePtr type, std::size_t visible_parameter_count) {
  auto *visible_type = zonk(type);
  if (!visible_type)
    throw std::invalid_argument(
        "cannot serialize a null inferred interface type");

  // A module definition with no source patterns is inferred as a Unit thunk
  // so its body can carry effects. That arrow is an implementation detail of
  // the checker, not a parameter in the exported interface contract.
  if (visible_parameter_count == 0) {
    auto *hidden = unifier_.resolve(visible_type);
    if (!hidden || hidden->tag != MonoType::Arrow) {
      throw std::invalid_argument(
          "parameterless module binding is missing its hidden Unit arrow");
    }
    auto *parameter = unifier_.resolve(hidden->param_type);
    if (!parameter || parameter->tag != MonoType::Con ||
        parameter->con != TyCon::Unit) {
      throw std::invalid_argument(
          "parameterless module binding has a non-Unit hidden parameter");
    }
    visible_type = hidden->return_type;
  }

  std::vector<MonoTypePtr> parameter_types;
  parameter_types.reserve(visible_parameter_count);
  auto *return_type = visible_type;
  for (std::size_t index = 0; index < visible_parameter_count; ++index) {
    auto *arrow = unifier_.resolve(return_type);
    if (!arrow || arrow->tag != MonoType::Arrow) {
      throw std::invalid_argument(
          "inferred interface type has fewer arrows than source parameters");
    }
    parameter_types.push_back(arrow->param_type);
    return_type = arrow->return_type;
  }

  std::unordered_map<TypeId, std::string> variable_names;
  auto variable_name = [&](TypeId id) -> const std::string & {
    if (const auto found = variable_names.find(id);
        found != variable_names.end())
      return found->second;
    const std::size_t ordinal = variable_names.size();
    std::string name;
    if (ordinal < 26)
      name.assign(1, static_cast<char>('a' + ordinal));
    else
      name = "t" + std::to_string(ordinal);
    return variable_names.emplace(id, std::move(name)).first->second;
  };

  std::function<std::string(MonoTypePtr)> descriptor;
  descriptor = [&](MonoTypePtr current) -> std::string {
    current = unifier_.resolve(current);
    if (!current)
      throw std::invalid_argument(
          "cannot serialize a null nested interface type");
    switch (current->tag) {
    case MonoType::Var:
      return "VAR(" + variable_name(current->var_id) + ")";
    case MonoType::Con:
      switch (current->con) {
      case TyCon::Int:
        return "INT";
      case TyCon::Float:
        return "FLOAT";
      case TyCon::Bool:
        return "BOOL";
      case TyCon::String:
        return "STRING";
      case TyCon::Char:
        return "CHAR";
      case TyCon::Byte:
        return "BYTE";
      case TyCon::Symbol:
        return "SYMBOL";
      case TyCon::Unit:
        return "UNIT";
      case TyCon::ByteArray:
        return "BYTE_ARRAY";
      case TyCon::Seq:
        return "SEQ";
      case TyCon::Set:
        return "SET";
      case TyCon::Dict:
        return "DICT";
      case TyCon::Tuple:
        return "TUPLE";
      case TyCon::Function:
        return "FUNCTION";
      case TyCon::Promise:
        return "PROMISE";
      }
      break;
    case MonoType::Arrow:
      return "FUNCTION(" + descriptor(current->param_type) + "," +
             descriptor(current->return_type) + ")";
    case MonoType::MTuple: {
      std::string result = "TUPLE(";
      for (std::size_t index = 0; index < current->elements.size(); ++index) {
        if (index)
          result += ',';
        result += descriptor(current->elements[index]);
      }
      return result + ")";
    }
    case MonoType::MRecord:
      // The current interface grammar has only the ABI-level RECORD token and
      // cannot preserve structural fields or a row rest. Do not publish a
      // descriptor that would silently weaken the inferred contract.
      throw std::invalid_argument(
          "structural record types are not representable in interfaces");
    case MonoType::Sum:
      throw std::invalid_argument(
          "anonymous sum types require structural interfaces");
    case MonoType::App: {
      const auto application = [&](std::string_view name,
                                   std::size_t arity) -> std::string {
        if (current->args.size() != arity)
          throw std::invalid_argument("interface type '" + current->type_name +
                                      "' has an unsupported arity");
        std::string result(name);
        result += '(';
        for (std::size_t index = 0; index < current->args.size(); ++index) {
          if (index)
            result += ',';
          result += descriptor(current->args[index]);
        }
        return result + ')';
      };
      if (current->type_name == "Linear")
        return application("LINEAR", 1);
      if (current->type_name == "Seq")
        return application("Seq", 1);
      if (current->type_name == "Set")
        return application("Set", 1);
      if (current->type_name == "Dict")
        return application("Dict", 2);
      if (current->type_name == "Promise")
        return application("Promise", 1);
      // A legacy bare ADT descriptor is imported as this anonymous wildcard
      // application. It has no nominal name that can be made more precise by
      // a forwarding source binding, so preserve the wildcard spelling.
      if (current->type_name == "ADT" && current->args.size() == 1)
        return "ADT";
      if (current->type_name == "ByteArray" && current->args.empty())
        return "BYTE_ARRAY";
      if (current->type_name == "IntArray" && current->args.empty())
        return "INT_ARRAY";
      if (current->type_name == "FloatArray" && current->args.empty())
        return "FLOAT_ARRAY";
      if (current->type_name.empty())
        throw std::invalid_argument(
            "cannot serialize an unnamed interface type application");
      const std::string Identity = current->resolved_key
                                       ? current->resolved_key->Module.fqn() +
                                             "\\" + current->resolved_key->Name
                                       : current->type_name;
      std::string result = "ADT(" + Identity;
      for (auto *argument : current->args)
        result += ',' + descriptor(argument);
      return result + ')';
    }
    }
    throw std::invalid_argument("unsupported inferred interface type");
  };

  InterfaceSignature signature;
  signature.parameter_descriptors.reserve(parameter_types.size());
  for (auto *parameter : parameter_types)
    signature.parameter_descriptors.push_back(descriptor(parameter));
  signature.return_descriptor = descriptor(return_type);
  signature.effect_scheme = serialize_effect_scheme(visible_type);
  return signature;
}

bool TypeChecker::is_effect_free(MonoTypePtr type) {
  if (!type)
    return false;
  auto row = effect_row_info(zonk(type));
  return row.ops.empty() && !row.open_rest;
}

std::string TypeChecker::serialize_effect_scheme(MonoTypePtr type) {
  std::vector<std::pair<std::string, EffectRef>> arrows;
  std::function<void(MonoTypePtr, const std::string &)> collect =
      [&](MonoTypePtr current, const std::string &path) {
        current = unifier_.resolve(current);
        if (!current)
          return;
        if (current->tag == MonoType::Arrow) {
          if (const auto effect = callee_effect(current))
            arrows.emplace_back(path, *effect);
          collect(current->param_type, path + "/p");
          collect(current->return_type, path + "/r");
        } else if (current->tag == MonoType::App) {
          for (size_t index = 0; index < current->args.size(); ++index)
            collect(current->args[index], path + "/a" + std::to_string(index));
        } else if (current->tag == MonoType::MTuple) {
          for (size_t index = 0; index < current->elements.size(); ++index)
            collect(current->elements[index],
                    path + "/t" + std::to_string(index));
        } else if (current->tag == MonoType::MRecord) {
          for (const auto &[name, field] : current->record_fields)
            collect(field, path + "/f" + name);
          if (current->row_rest)
            collect(current->row_rest, path + "/row");
        } else if (current->tag == MonoType::Sum) {
          for (size_t index = 0; index < current->sum_alternatives.size();
               ++index)
            collect(current->sum_alternatives[index],
                    path + "/s" + std::to_string(index));
        }
      };
  collect(type, "$");
  std::sort(arrows.begin(), arrows.end(),
            [](const auto &left, const auto &right) {
              return left.first < right.first;
            });

  std::map<std::pair<bool, EffectVarId>, unsigned> external_variables;
  unsigned next_variable = 0;
  const auto encode_atoms = [&](const std::vector<EffectAtomId> &Atoms) {
    std::vector<std::string> Labels;
    Labels.reserve(Atoms.size());
    for (const auto Atom : Atoms) {
      const auto Canonical = arena_.effect_solver().canonicalAtom(Atom);
      const auto *Operation = std::get_if<OperationEffectAtom>(
          &arena_.effect_atoms().at(Canonical));
      if (!Operation)
        throw std::invalid_argument(
            "built-in effect facts require structural interfaces");
      // The checked-in v1 interface grammar stores only qualified operation
      // keys. This is the one lossy compatibility boundary: inference and the
      // v2-ready structural projection retain the complete ordered type and
      // effect application, while legacy serialization deliberately emits
      // only its key until Task 17 retires v1.
      Labels.push_back(Operation->Key.Effect.Module.fqn() + "\\" +
                       Operation->Key.Effect.Name + "." +
                       Operation->Key.Operation);
    }
    std::ranges::sort(Labels);
    std::string encoded;
    for (size_t index = 0; index < Labels.size(); ++index) {
      if (index)
        encoded += ',';
      encoded += Labels[index];
    }
    return encoded;
  };

  std::string encoded;
  for (const auto &[path, effect] : arrows) {
    const auto summary = arena_.effect_solver().summarize(effect);
    if (!encoded.empty())
      encoded += ';';
    encoded += path + "#" + encode_atoms(summary.known_atoms) + "#";
    for (size_t index = 0; index < summary.tails.size(); ++index) {
      if (index)
        encoded += '|';
      const auto &tail = summary.tails[index];
      const auto key = std::make_pair(tail.opaque, tail.variable);
      const auto [it, inserted] =
          external_variables.emplace(key, next_variable);
      if (inserted)
        ++next_variable;
      encoded += tail.opaque ? "o" : "f";
      encoded += std::to_string(it->second);
      if (!tail.excluded_atoms.empty())
        encoded += "~" + encode_atoms(tail.excluded_atoms);
    }
  }
  return encoded;
}

MonoTypePtr TypeChecker::apply_effect_scheme(MonoTypePtr type,
                                             std::string_view encoded) {
  struct Tail {
    EffectRef source;
    std::vector<EffectAtomId> excluded_atoms;
  };
  struct Entry {
    std::vector<EffectAtomId> atoms;
    std::vector<Tail> tails;
  };
  std::map<std::string, Entry> entries;
  std::map<std::string, EffectRef> variables;
  bool valid = true;

  const auto split = [](std::string_view text, char delimiter) {
    std::vector<std::string_view> parts;
    size_t start = 0;
    while (start <= text.size()) {
      const size_t end = text.find(delimiter, start);
      parts.push_back(text.substr(start, end == std::string_view::npos
                                             ? std::string_view::npos
                                             : end - start));
      if (end == std::string_view::npos)
        break;
      start = end + 1;
    }
    return parts;
  };
  const auto atoms_from = [&](std::string_view text) {
    std::vector<EffectAtomId> Atoms;
    if (text.empty())
      return Atoms;
    for (const auto Label : split(text, ',')) {
      if (Label.empty()) {
        valid = false;
        continue;
      }
      const auto Dot = Label.rfind('.');
      if (Dot == std::string_view::npos || Dot == 0 ||
          Dot + 1 == Label.size()) {
        valid = false;
        continue;
      }
      const auto Atom = intern_operation_atom(
          resolved_effect_key(Label.substr(0, Dot), Label.substr(Dot + 1)));
      legacy_untyped_effect_atoms_.insert(Atom);
      Atoms.push_back(Atom);
    }
    std::sort(Atoms.begin(), Atoms.end());
    Atoms.erase(std::unique(Atoms.begin(), Atoms.end()), Atoms.end());
    return Atoms;
  };

  if (valid) {
    for (const auto field : split(encoded, ';')) {
      if (field.empty()) {
        if (!encoded.empty())
          valid = false;
        continue;
      }
      const auto first = field.find('#');
      const auto second = first == std::string_view::npos
                              ? std::string_view::npos
                              : field.find('#', first + 1);
      if (first == std::string_view::npos || second == std::string_view::npos ||
          field.find('#', second + 1) != std::string_view::npos) {
        valid = false;
        break;
      }
      const std::string path(field.substr(0, first));
      if (path.empty() || entries.contains(path)) {
        valid = false;
        break;
      }
      Entry entry;
      entry.atoms = atoms_from(field.substr(first + 1, second - first - 1));
      const auto tails = field.substr(second + 1);
      if (!tails.empty()) {
        for (const auto tail : split(tails, '|')) {
          if (tail.size() < 2 || (tail.front() != 'f' && tail.front() != 'o')) {
            valid = false;
            break;
          }
          const auto mask = tail.find('~');
          const auto id_text = tail.substr(1, mask == std::string_view::npos
                                                  ? std::string_view::npos
                                                  : mask - 1);
          if (id_text.empty() ||
              !std::all_of(id_text.begin(), id_text.end(),
                           [](char c) { return c >= '0' && c <= '9'; })) {
            valid = false;
            break;
          }
          const std::string key(tail.substr(0, mask));
          auto variable = variables.find(key);
          if (variable == variables.end()) {
            const bool opaque = tail.front() == 'o';
            variable =
                variables
                    .emplace(key, opaque ? arena_.effect_solver().opaque()
                                         : arena_.effect_solver().flexible())
                    .first;
          }
          entry.tails.push_back({
              variable->second,
              mask == std::string_view::npos
                  ? std::vector<EffectAtomId>{}
                  : atoms_from(tail.substr(mask + 1)),
          });
        }
      }
      if (!valid)
        break;
      entries.emplace(path, std::move(entry));
    }
  }

  const auto opaque_all = [&](const auto &self,
                              MonoTypePtr current) -> MonoTypePtr {
    current = unifier_.resolve(current);
    if (!current)
      return current;
    if (current->tag == MonoType::Arrow)
      return arena_.make_arrow(self(self, current->param_type),
                               self(self, current->return_type),
                               arena_.effect_solver().opaque());
    if (current->tag == MonoType::App) {
      std::vector<MonoTypePtr> args;
      for (const auto input : current->args)
        args.push_back(self(self, input));
      return current->resolved_key
                 ? arena_.make_app(*current->resolved_key, std::move(args),
                                   current->type_name)
                 : arena_.make_app(current->type_name, std::move(args));
    }
    if (current->tag == MonoType::MTuple) {
      std::vector<MonoTypePtr> elements;
      for (const auto input : current->elements)
        elements.push_back(self(self, input));
      return arena_.make_tuple(std::move(elements));
    }
    if (current->tag == MonoType::MRecord) {
      std::vector<std::pair<std::string, MonoTypePtr>> fields;
      fields.reserve(current->record_fields.size());
      for (const auto &[name, field] : current->record_fields)
        fields.emplace_back(name, self(self, field));
      return arena_.make_record(
          std::move(fields),
          current->row_rest ? self(self, current->row_rest) : nullptr);
    }
    if (current->tag == MonoType::Sum) {
      std::vector<MonoTypePtr> Alternatives;
      for (const auto *Alternative : current->sum_alternatives)
        Alternatives.push_back(self(self, Alternative));
      return arena_.make_sum(std::move(Alternatives));
    }
    return current;
  };
  if (!valid)
    return opaque_all(opaque_all, type);

  const auto rebuild = [&](const auto &self, MonoTypePtr current,
                           const std::string &path) -> MonoTypePtr {
    current = unifier_.resolve(current);
    if (!current)
      return current;
    if (current->tag == MonoType::Arrow) {
      const auto found = entries.find(path);
      if (found == entries.end())
        return opaque_all(opaque_all, current);
      std::vector<EffectRef> sources;
      if (!found->second.atoms.empty())
        sources.push_back(arena_.effect_solver().atoms(found->second.atoms));
      for (const auto &tail : found->second.tails) {
        if (!tail.source.valid())
          return opaque_all(opaque_all, current);
        sources.push_back(
            arena_.effect_solver().mask(tail.source, tail.excluded_atoms));
      }
      return arena_.make_arrow(self(self, current->param_type, path + "/p"),
                               self(self, current->return_type, path + "/r"),
                               arena_.effect_solver().join(std::move(sources)));
    }
    if (current->tag == MonoType::App) {
      std::vector<MonoTypePtr> args;
      for (size_t index = 0; index < current->args.size(); ++index)
        args.push_back(self(self, current->args[index],
                            path + "/a" + std::to_string(index)));
      return current->resolved_key
                 ? arena_.make_app(*current->resolved_key, std::move(args),
                                   current->type_name)
                 : arena_.make_app(current->type_name, std::move(args));
    }
    if (current->tag == MonoType::MTuple) {
      std::vector<MonoTypePtr> elements;
      for (size_t index = 0; index < current->elements.size(); ++index)
        elements.push_back(self(self, current->elements[index],
                                path + "/t" + std::to_string(index)));
      return arena_.make_tuple(std::move(elements));
    }
    if (current->tag == MonoType::MRecord) {
      std::vector<std::pair<std::string, MonoTypePtr>> fields;
      fields.reserve(current->record_fields.size());
      for (const auto &[name, field] : current->record_fields)
        fields.emplace_back(name, self(self, field, path + "/f" + name));
      return arena_.make_record(
          std::move(fields), current->row_rest
                                 ? self(self, current->row_rest, path + "/row")
                                 : nullptr);
    }
    if (current->tag == MonoType::Sum) {
      std::vector<MonoTypePtr> Alternatives;
      for (size_t Index = 0; Index < current->sum_alternatives.size(); ++Index)
        Alternatives.push_back(self(self, current->sum_alternatives[Index],
                                    path + "/s" + std::to_string(Index)));
      return arena_.make_sum(std::move(Alternatives));
    }
    return current;
  };
  return rebuild(rebuild, type, "$");
}

void TypeChecker::check_module(ast::ModuleDecl *mod) {
  begin_check_session();
  if (!mod)
    return;
  current_module_ = model::ModuleIdentity(mod->fqn->to_string());

  // Constructors must be visible while checking the module that declares
  // them: smart constructors and destructor-style functions commonly use
  // them in their own definitions.  Imported ADTs are registered by the
  // caller, but a module's declarations have not crossed that boundary yet.
  for (auto *adt : mod->adt_declarations) {
    if (!adt)
      continue;
    std::vector<std::pair<std::string, int>> constructors;
    std::vector<std::vector<ast::FieldType>> field_types;
    std::vector<std::vector<std::string>> field_names;
    constructors.reserve(adt->variants.size());
    for (auto *ctor : adt->variants) {
      if (!ctor)
        continue;
      constructors.emplace_back(
          ctor->name, static_cast<int>(ctor->field_type_names.size()));
      field_types.push_back(ctor->field_type_names);
      field_names.push_back(ctor->field_names);
    }
    register_adt(adt->name, adt->type_params, constructors, field_types,
                 field_names);
    // Preserve the exact declaration-owned graph now, while the annotation
    // variables and source convention are known. Projection later only copies
    // this seed; it never guesses callable semantics from Arrow shape.
    auto &NominalSeed = projection_nominal_declarations_[adt];
    std::unordered_map<std::string, MonoTypePtr> DeclarationVariables;
    for (const auto &Parameter : adt->type_params) {
      auto *Variable = arena_.fresh_var(0);
      uf_.add_var(Variable->var_id, 0);
      DeclarationVariables.emplace(Parameter, Variable);
      NominalSeed.TypeParameters.push_back(Variable);
    }
    std::function<MonoTypePtr(const ast::FieldType &)> ProjectField =
        [&](const ast::FieldType &Field) -> MonoTypePtr {
      if (Field.is_function_type) {
        MonoTypePtr Result = Field.return_types.empty()
                                 ? arena_.make_con(TyCon::Unit)
                                 : ProjectField(Field.return_types.front());
        for (auto Iterator = Field.param_types.rbegin();
             Iterator != Field.param_types.rend(); ++Iterator)
          Result = arena_.make_arrow(ProjectField(*Iterator), Result);
        return Result;
      }
      if (Field.is_tuple_type) {
        std::vector<MonoTypePtr> Elements;
        for (const auto &Element : Field.tuple_types)
          Elements.push_back(ProjectField(Element));
        return arena_.make_tuple(std::move(Elements));
      }
      if (const auto Found = DeclarationVariables.find(Field.name);
          Found != DeclarationVariables.end())
        return Found->second;
      const auto Builtin = [&](TyCon Type) { return arena_.make_con(Type); };
      if (Field.name == "Int")
        return Builtin(TyCon::Int);
      if (Field.name == "Float")
        return Builtin(TyCon::Float);
      if (Field.name == "Bool")
        return Builtin(TyCon::Bool);
      if (Field.name == "String")
        return Builtin(TyCon::String);
      if (Field.name == "Char")
        return Builtin(TyCon::Char);
      if (Field.name == "Byte")
        return Builtin(TyCon::Byte);
      if (Field.name == "Symbol")
        return Builtin(TyCon::Symbol);
      if (Field.name == "Unit" || Field.name == "()")
        return Builtin(TyCon::Unit);
      std::vector<MonoTypePtr> Arguments;
      for (const auto &Argument : Field.type_arguments)
        Arguments.push_back(ProjectField(Argument));
      if (Field.name == "Promise" && Arguments.size() == 1) {
        const auto *Success = unifier_.resolve(Arguments.front());
        return arena_.make_promise(Arguments.front(),
                                   Success && Success->tag == MonoType::Con &&
                                           Success->con != TyCon::String
                                       ? model::ResultOwnership::Trivial
                                       : model::ResultOwnership::Owned,
                                   arena_.effect_solver().empty());
      }
      if (const auto Resolved = resolved_nominal_key(Field.name))
        return arena_.make_app(*Resolved, std::move(Arguments), Field.name);
      return arena_.make_app(model::NominalTypeKey{current_module_, Field.name},
                             std::move(Arguments), Field.name);
    };
    std::vector<MonoTypePtr> DeclarationRoots;
    for (auto *Constructor : adt->variants) {
      NominalSeed.ConstructorFields.emplace_back();
      auto &Fields = NominalSeed.ConstructorFields.back();
      if (Constructor)
        for (const auto &Field : Constructor->field_type_names) {
          Fields.push_back(ProjectField(Field));
          DeclarationRoots.push_back(Fields.back());
        }
    }
    NominalSeed.CallableContracts = declaration_callable_contracts(
        DeclarationRoots, model::CallingConvention::ClosureEntry, adt->Range);
    for (const auto &trait_name : adt->derive_traits) {
      bool lawful = true;
      if (trait_name == "Eq" || trait_name == "Ord" || trait_name == "Hash" ||
          trait_name == "Show" || trait_name == "Send" ||
          trait_name == "Shareable") {
        for (auto *ctor : adt->variants) {
          for (size_t field_index = 0;
               field_index < ctor->field_type_names.size(); ++field_index) {
            const auto &field = ctor->field_type_names[field_index];
            const bool unsupported =
                field.is_function_type || field.name == "Function" ||
                field.name == "Promise" || field.name == "Channel" ||
                field.name == "Linear" || field.name == "ByteArray" ||
                field.name == "IntArray" || field.name == "FloatArray";
            if (!unsupported)
              continue;
            diag_.error(
                adt->Range, ErrorCode::E0400,
                "cannot derive " + trait_name + " for '" + adt->name +
                    "': field " + ctor->name + "." +
                    std::to_string(field_index + 1) + " has non-lawful type '" +
                    (field.is_function_type ? "Function" : field.name) + "'");
            ++error_count_;
            lawful = false;
            break;
          }
          if (!lawful)
            break;
        }
      }
      if ((trait_name == "Ord" || trait_name == "Hash") &&
          std::find(adt->derive_traits.begin(), adt->derive_traits.end(),
                    "Eq") == adt->derive_traits.end()) {
        bool explicit_eq = false;
        for (auto *instance : mod->instance_declarations)
          if (instance && instance->trait_name == "Eq" &&
              !instance->type_names.empty() &&
              instance->type_names.front() == adt->name) {
            explicit_eq = true;
            break;
          }
        if (!explicit_eq) {
          diag_.error(adt->Range, ErrorCode::E0400,
                      "deriving " + trait_name + " for '" + adt->name +
                          "' requires Eq; add `deriving Eq, " + trait_name +
                          "`");
          ++error_count_;
          lawful = false;
        }
      }
      if (lawful) {
        if (trait_name == "Send" || trait_name == "Shareable") {
          std::vector<std::pair<std::string, std::string>> constraints;
          for (const auto &parameter : adt->type_params)
            constraints.emplace_back(trait_name, parameter);
          register_instance(trait_name, {adt->name}, adt->type_params,
                            std::move(constraints));
        } else {
          register_instance(trait_name, {adt->name});
        }
      }
    }
  }

  for (auto *trait : mod->trait_declarations) {
    if (!trait)
      continue;
    register_trait(trait->name, trait->type_params);
    for (const auto &[superclass, _] : trait->superclasses)
      register_trait_superclass(trait->name, superclass);
    for (const auto &method : trait->methods) {
      std::unordered_map<std::string, MonoTypePtr> variables;
      auto *method_type =
          from_ast_type_impl(method.type_signature, 0, variables);
      std::vector<MonoTypePtr> trait_arguments;
      for (const auto &parameter : trait->type_params) {
        auto found = variables.find(parameter);
        if (found == variables.end()) {
          auto *variable = arena_.fresh_var(0);
          uf_.add_var(variable->var_id, 0);
          found = variables.emplace(parameter, variable).first;
        }
        trait_arguments.push_back(found->second);
      }
      register_trait_method(trait->name, method.name, method_type);
      if (auto scheme = root_env_->lookup(method.name);
          scheme && !trait_arguments.empty()) {
        root_env_->bind_scheme(
            method.name, generalize(scheme->body, -1,
                                    {Constraint(resolved_trait_key(trait->name),
                                                std::move(trait_arguments))}));
      }
    }
  }

  std::unordered_set<std::string> local_instance_keys;
  for (auto *instance : mod->instance_declarations) {
    if (!instance)
      continue;
    std::string key = instance->trait_name;
    for (const auto &type_name : instance->type_names)
      key += ":" + type_name;
    if (!local_instance_keys.insert(key).second) {
      diag_.error(instance->Range, ErrorCode::E0400,
                  "duplicate visible trait instance '" + key +
                      "'; instance selection must be coherent");
      ++error_count_;
      continue;
    }
    register_instance(
        instance->trait_name, instance->type_names, instance->type_params,
        {instance->constraints.begin(), instance->constraints.end()});
  }

  auto env = root_env_->child();
  // Module-level externs are declarations in the same lexical scope as the
  // module's Yona definitions.  The expression form handles its own nested
  // body, but module declarations have no body and therefore must be bound
  // explicitly before any function is inferred.
  for (auto *ext : mod->extern_declarations) {
    if (!ext)
      continue;
    auto *declared = from_ast_type(ext->declared_type, 0);
    auto Scheme = generalize(declared, -1);
    auto Contracts = declaration_callable_contracts(
        std::span<MonoTypePtr const>(&Scheme.body, 1),
        model::CallingConvention::NativeExtern, ext->Range);
    std::vector<model::ParameterOwnership> Ownerships;
    model::ResultOwnership ResultContract = model::ResultOwnership::Trivial;
    if (const auto Root = std::ranges::find_if(Contracts,
                                               [&](const auto &Contract) {
                                                 return Contract.Arrow ==
                                                        Scheme.body;
                                               });
        Root != Contracts.end()) {
      Ownerships = Root->ParameterOwnerships;
      ResultContract = Root->ResultContract;
    }
    const model::FunctionDeclarationIdentity Identity =
        model::SymbolIdentity{current_module_, ext->name};
    scheme_identities_.insert_or_assign(Scheme.body, Identity);
    scheme_callable_contracts_.insert_or_assign(Scheme.body, Contracts);
    projection_extern_declarations_.push_back(
        ExternDeclarationSeed{ext, Scheme, std::move(Ownerships),
                              ResultContract, std::move(Contracts)});
    env->bind_scheme(ext->name, std::move(Scheme));
  }
  // Infer dependency SCCs in callee-first order. Only members of the current
  // recursive component are preliminary and monomorphic; completed sibling
  // components are generalized before any dependent is checked.
  ModuleExportResolver resolve_exports;
  if (import_src_)
    resolve_exports = [this](const std::string &module_fqn) {
      return import_src_->imported_module_exports(module_fqn);
    };
  const auto Components = module_function_components(
      mod, std::move(resolve_exports), [this](const std::string &ModuleFqn) {
        register_imported_nominals(ModuleFqn);
      });
  std::vector<std::pair<ast::FunctionExpr *, MonoTypePtr>> ProjectionFunctions;
  for (const auto &component : Components) {
    std::unordered_map<std::string, MonoTypePtr> prelim;
    if (component.recursive)
      for (auto *func : component.functions) {
        if (!func || func->name.empty())
          continue;
        const auto effect = arena_.effect_solver().derived();
        predeclared_function_body_effects_[func] = effect;
        MonoTypePtr declaration = nullptr;
        if (func->patterns.empty()) {
          declaration = arena_.fresh_var(0);
          uf_.add_var(declaration->var_id, 0);
        } else {
          auto *result = arena_.fresh_var(0);
          uf_.add_var(result->var_id, 0);
          declaration = result;
          for (int index = static_cast<int>(func->patterns.size()) - 1;
               index >= 0; --index) {
            auto *parameter = arena_.fresh_var(0);
            uf_.add_var(parameter->var_id, 0);
            declaration = arena_.make_arrow(
                parameter, declaration,
                index == static_cast<int>(func->patterns.size()) - 1
                    ? effect
                    : arena_.effect_solver().empty());
          }
        }
        env->bind(func->name, declaration);
        prelim[func->name] = declaration;
      }

    std::vector<std::pair<ast::FunctionExpr *, MonoTypePtr>> inferred_functions;
    inferred_functions.reserve(component.functions.size());
    for (auto *func : component.functions) {
      if (!func)
        continue;
      const auto pit = prelim.find(func->name);
      if (pit != prelim.end())
        recursive_self_contexts_.push_back({pit->second});
      auto *inferred = infer(func, env, 0);
      if (pit != prelim.end())
        recursive_self_contexts_.pop_back();
      if (!inferred)
        continue;
      // A parameterless module definition is a value (a CAF), whereas an
      // expression lambda with no written parameters is a Unit thunk.
      auto *type = unifier_.resolve(inferred);
      if (func->patterns.empty() && type && type->tag == MonoType::Arrow &&
          unifier_.resolve(type->param_type)->tag == MonoType::Con &&
          unifier_.resolve(type->param_type)->con == TyCon::Unit)
        type = type->return_type;
      if (func->type_signature.has_value()) {
        auto *declared = from_ast_type(*func->type_signature, 0);
        unifier_.unify(type, declared, func->Range,
                       "against the declared type of '" + func->name + "'");
      }
      if (pit != prelim.end())
        unifier_.unify(pit->second, type, func->Range,
                       "in module function '" + func->name + "'");
      inferred_functions.emplace_back(func, type);
    }

    for (auto &[func, type] : inferred_functions) {
      auto Scheme = generalize(unifier_.resolve(type), -1,
                               constraints_for_function(func));
      if (const auto Contracts = expression_callable_contracts_.find(func);
          Contracts != expression_callable_contracts_.end()) {
        auto Remapped =
            remap_callable_contracts(type, Scheme.body, Contracts->second);
        if (!Remapped.empty())
          scheme_callable_contracts_.insert_or_assign(Scheme.body,
                                                      std::move(Remapped));
      }
      scheme_identities_.insert_or_assign(
          Scheme.body, model::FunctionDeclarationIdentity{
                           model::SymbolIdentity{current_module_, func->name}});
      env->bind_scheme(func->name, std::move(Scheme));
      ProjectionFunctions.emplace_back(func, unifier_.resolve(type));
    }
    for (auto *func : component.functions)
      predeclared_function_body_effects_.erase(func);
  }

  const auto projectMethodBody =
      [&](ast::FunctionExpr *Function, MonoTypePtr Expected,
          model::FunctionDeclarationIdentity Identity,
          const ast::TraitDeclNode *TraitOwner,
          const ast::InstanceDeclNode *InstanceOwner) {
        if (!Function)
          return;
        auto *Inferred = infer(Function, env, 0);
        if (!Inferred)
          return;
        if (Expected)
          unifier_.unify(Inferred, Expected, Function->Range,
                         "against the declared trait method type of '" +
                             Function->name + "'");
        auto *Resolved = unifier_.resolve(Inferred);
        auto Scheme =
            generalize(Resolved, -1, constraints_for_function(Function));
        scheme_identities_.insert_or_assign(Scheme.body, Identity);
        function_schemes_.insert_or_assign(Function, Scheme);
        projection_function_identities_.insert_or_assign(Function, Identity);
        if (TraitOwner)
          trait_default_owners_.insert_or_assign(Function, TraitOwner);
        if (InstanceOwner)
          instance_method_owners_.insert_or_assign(Function, InstanceOwner);
        ProjectionFunctions.emplace_back(Function, Resolved);
      };

  // Trait defaults and instance methods are executable declarations, not
  // metadata-only aliases. Infer them in the completed module environment so
  // their function schemes, callable contracts, and body facts are available
  // to the structural projection just like ordinary module functions.
  for (const auto *Trait : mod->trait_declarations) {
    if (!Trait)
      continue;
    std::unordered_map<std::string, MonoTypePtr> Variables;
    auto &TraitParameters = projection_trait_type_parameters_[Trait];
    for (const auto &Parameter : Trait->type_params) {
      auto *Variable = arena_.fresh_var(0);
      uf_.add_var(Variable->var_id, 0);
      Variables.emplace(Parameter, Variable);
      TraitParameters.push_back(Variable);
    }
    auto &MethodSeeds = projection_trait_methods_[Trait];
    const auto TraitKey = resolved_trait_key(Trait->name);
    for (const auto &Method : Trait->methods) {
      auto MethodVariables = Variables;
      auto *Expected =
          from_ast_type_impl(Method.type_signature, 0, MethodVariables);
      auto Scheme = generalize(
          Expected, -1,
          TraitParameters.empty()
              ? std::vector<Constraint>{}
              : std::vector<Constraint>{Constraint(TraitKey, TraitParameters)});
      std::erase_if(Scheme.quantified_vars, [&](TypeId Variable) {
        return std::ranges::any_of(TraitParameters, [&](MonoTypePtr Parameter) {
          return Parameter && Parameter->var_id == Variable;
        });
      });
      auto Contracts = declaration_callable_contracts(
          std::span<MonoTypePtr const>(&Scheme.body, 1),
          model::CallingConvention::DirectYona, Trait->Range);
      std::vector<model::ParameterOwnership> Ownerships;
      model::ResultOwnership ResultContract = model::ResultOwnership::Trivial;
      if (const auto Root = std::ranges::find_if(Contracts,
                                                 [&](const auto &Contract) {
                                                   return Contract.Arrow ==
                                                          Scheme.body;
                                                 });
          Root != Contracts.end()) {
        Ownerships = Root->ParameterOwnerships;
        ResultContract = Root->ResultContract;
      }
      MethodSeeds.push_back(
          TraitMethodDeclarationSeed{std::move(Scheme), std::move(Ownerships),
                                     ResultContract, std::move(Contracts)});
      if (Method.default_impl)
        projectMethodBody(
            Method.default_impl, Expected,
            model::FunctionDeclarationIdentity{model::SymbolIdentity{
                current_module_, Trait->name + "_default_" + Method.name}},
            Trait, nullptr);
    }
  }

  const auto simpleInstanceType = [&](std::string_view Name,
                                      const auto &Variables) -> MonoTypePtr {
    if (const auto Found = Variables.find(std::string(Name));
        Found != Variables.end())
      return Found->second;
    if (Name == "Int")
      return arena_.make_con(TyCon::Int);
    if (Name == "Float")
      return arena_.make_con(TyCon::Float);
    if (Name == "Bool")
      return arena_.make_con(TyCon::Bool);
    if (Name == "String")
      return arena_.make_con(TyCon::String);
    if (Name == "Char")
      return arena_.make_con(TyCon::Char);
    if (Name == "Byte")
      return arena_.make_con(TyCon::Byte);
    if (Name == "Symbol")
      return arena_.make_con(TyCon::Symbol);
    if (Name == "Unit" || Name == "()")
      return arena_.make_con(TyCon::Unit);
    if (Name == "ByteArray")
      return arena_.make_con(TyCon::ByteArray);
    if (Name == "IntArray" || Name == "FloatArray")
      return arena_.make_app(std::string(Name), {});
    const auto Key = resolved_nominal_key(Name).value_or(
        model::NominalTypeKey{current_module_, std::string(Name)});
    return arena_.make_app(Key, {}, std::string(Name));
  };
  for (const auto *Instance : mod->instance_declarations) {
    if (!Instance)
      continue;
    const auto Trait = std::ranges::find_if(
        mod->trait_declarations, [&](const ast::TraitDeclNode *Candidate) {
          return Candidate && Candidate->name == Instance->trait_name;
        });
    std::unordered_map<std::string, MonoTypePtr> InstanceVariables;
    auto &InstanceParameters = projection_instance_type_parameters_[Instance];
    for (const auto &Parameter : Instance->type_params) {
      auto *Variable = arena_.fresh_var(0);
      uf_.add_var(Variable->var_id, 0);
      InstanceVariables.emplace(Parameter, Variable);
      InstanceParameters.push_back(Variable);
    }
    std::unordered_map<std::string, MonoTypePtr> TraitVariables;
    if (Trait != mod->trait_declarations.end()) {
      for (std::size_t Index = 0; Index < (*Trait)->type_params.size() &&
                                  Index < Instance->type_names.size();
           ++Index) {
        auto *Argument =
            simpleInstanceType(Instance->type_names[Index], InstanceVariables);
        if (!InstanceVariables.empty() &&
            (Index == 0 || Instance->type_names[Index] == "Tuple")) {
          std::vector<MonoTypePtr> TypeArguments;
          TypeArguments.reserve(Instance->type_params.size());
          for (const auto &Parameter : Instance->type_params)
            TypeArguments.push_back(InstanceVariables.at(Parameter));
          const auto &Name = Instance->type_names[Index];
          if (Name == "Tuple")
            Argument = arena_.make_tuple(std::move(TypeArguments));
          else if (Name == "Seq" || Name == "Set" || Name == "Dict")
            Argument = arena_.make_app(Name, std::move(TypeArguments));
          else if (Argument->tag == MonoType::App)
            Argument = Argument->resolved_key
                           ? arena_.make_app(*Argument->resolved_key,
                                             std::move(TypeArguments),
                                             Argument->type_name)
                           : arena_.make_app(Argument->type_name,
                                             std::move(TypeArguments));
        }
        TraitVariables.emplace((*Trait)->type_params[Index], Argument);
      }
    }
    for (auto *Method : Instance->methods) {
      if (!Method)
        continue;
      MonoTypePtr Expected = nullptr;
      if (Trait != mod->trait_declarations.end()) {
        const auto Signature = std::ranges::find_if(
            (*Trait)->methods, [&](const ast::TraitMethodSig &Candidate) {
              return Candidate.name == Method->name;
            });
        if (Signature != (*Trait)->methods.end()) {
          auto Variables = TraitVariables;
          Expected =
              from_ast_type_impl(Signature->type_signature, 0, Variables);
        }
      }
      projectMethodBody(
          Method, Expected,
          model::FunctionDeclarationIdentity{model::SymbolIdentity{
              current_module_,
              instance_method_symbol(*Instance, Method->name)}},
          nullptr, Instance);
    }
  }
  std::unordered_set<const ast::FunctionExpr *> ProjectedFunctions;
  for (const auto &[Function, _] : ProjectionFunctions)
    ProjectedFunctions.insert(Function);
  std::vector<const ast::FunctionExpr *> LocalFunctions;
  for (const auto &[Function, _] : projection_function_types_) {
    if (!Function || ProjectedFunctions.contains(Function))
      continue;
    bool InModule = false;
    for (auto *Current = Function->parent; Current; Current = Current->parent) {
      if (Current == mod) {
        InModule = true;
        break;
      }
      if (const auto *Enclosing =
              dynamic_cast<const ast::FunctionExpr *>(Current);
          Enclosing && ProjectedFunctions.contains(Enclosing)) {
        InModule = true;
        break;
      }
    }
    if (InModule)
      LocalFunctions.push_back(Function);
  }
  std::ranges::sort(LocalFunctions, [](const auto *Left, const auto *Right) {
    if (Left->Range.Offset != Right->Range.Offset)
      return Left->Range.Offset < Right->Range.Offset;
    return Left < Right;
  });
  for (const auto *Function : LocalFunctions) {
    const auto Type = projection_function_types_.find(Function);
    if (Type == projection_function_types_.end() || !Type->second)
      continue;
    projection_function_identities_.try_emplace(
        Function,
        model::FunctionDeclarationIdentity{model::LocalFunctionIdentity{
            current_module_,
            {static_cast<std::uint32_t>(Function->Range.Offset)}}});
    ProjectionFunctions.emplace_back(const_cast<ast::FunctionExpr *>(Function),
                                     unifier_.resolve(Type->second));
  }
  projection_module_ = mod;
  projection_module_functions_ = std::move(ProjectionFunctions);
}

MonoTypePtr TypeChecker::check(AstNode *node) {
  begin_check_session();
  auto *Result = infer(node, root_env_, 0);
  projection_root_ = node;
  projection_root_type_ = Result;
  if (const auto *Function = dynamic_cast<ast::FunctionExpr *>(node))
    projection_visible_parameter_count_ = Function->patterns.size();
  return Result;
}

MonoTypePtr TypeChecker::type_of(AstNode *node) const {
  auto it = type_map_.find(node);
  return (it != type_map_.end()) ? it->second : nullptr;
}

std::optional<TypeChecker::SelectedTraitInstance>
TypeChecker::selected_trait_instance(const ApplyExpr *application) const {
  const auto found = selected_trait_instances_.find(application);
  if (found == selected_trait_instances_.end())
    return std::nullopt;
  return found->second;
}

void TypeChecker::record(AstNode *node, MonoTypePtr type) {
  type_map_[node] = type;
}

MonoTypePtr TypeChecker::constrain_contextual_type(ast::AstNode *Node,
                                                   MonoTypePtr Actual,
                                                   MonoTypePtr Expected,
                                                   const SourceRange &Range,
                                                   const std::string &Context) {
  Actual = unifier_.resolve(Actual);
  Expected = unifier_.resolve(Expected);
  if (!Actual || !Expected)
    return Actual;
  if (Actual->tag == MonoType::App && Actual->type_name == "Promise" &&
      Actual->promise_result_contract && !Actual->args.empty() &&
      !(Expected->tag == MonoType::App && Expected->type_name == "Promise")) {
    const auto AwaitEffects = Actual->promise_effects;
    include_evaluation_effect(AwaitEffects);
    const auto Existing = node_effects_.find(Node);
    node_effects_.insert_or_assign(
        Node,
        Existing == node_effects_.end()
            ? AwaitEffects
            : arena_.effect_solver().join({Existing->second, AwaitEffects}));
    unifier_.unify(Actual->args.front(), Expected, Range,
                   Context + " after awaiting Promise");
    return unifier_.resolve(Expected);
  }
  if (Actual->tag != MonoType::Sum && Expected->tag == MonoType::Sum) {
    MonoTypePtr Match = nullptr;
    for (const auto *Alternative : Expected->sum_alternatives) {
      const auto *Resolved = unifier_.resolve(Alternative);
      if (!Resolved || Resolved->tag == MonoType::Var)
        continue;
      if (pretty_print(Resolved) == pretty_print(Actual) &&
          (!(Resolved->resolved_key || Actual->resolved_key) ||
           (Resolved->resolved_key && Actual->resolved_key &&
            *Resolved->resolved_key == *Actual->resolved_key))) {
        Match = Alternative;
        break;
      }
    }
    if (!Match) {
      const auto Variable = std::ranges::find_if(
          Expected->sum_alternatives, [&](MonoTypePtr Alternative) {
            const auto *Resolved = unifier_.resolve(Alternative);
            return Resolved && Resolved->tag == MonoType::Var;
          });
      if (Variable != Expected->sum_alternatives.end())
        Match = *Variable;
    }
    if (Match && unifier_.unify(Actual, Match, Range, Context)) {
      sum_injections_.insert_or_assign(
          Node, SumInjectionProjection{unifier_.resolve(Match), Expected});
      return Expected;
    }
  }
  unifier_.unify(Actual, Expected, Range, Context);
  return unifier_.resolve(Actual);
}

MonoTypePtr TypeChecker::zonk(MonoTypePtr type) {
  type = unifier_.resolve(type);
  if (!type)
    return nullptr;
  switch (type->tag) {
  case MonoType::Var:
    return type; // unresolved var
  case MonoType::Con:
    return type;
  case MonoType::Arrow:
    return arena_.make_arrow(zonk(type->param_type), zonk(type->return_type),
                             type->arrow_effect);
  case MonoType::App: {
    std::vector<MonoTypePtr> args;
    for (auto *a : type->args)
      args.push_back(zonk(a));
    if (type->type_name == "Promise" && type->promise_result_contract &&
        !args.empty())
      return arena_.make_promise(args.front(), *type->promise_result_contract,
                                 type->promise_effects);
    return type->resolved_key
               ? arena_.make_app(*type->resolved_key, std::move(args),
                                 type->type_name)
               : arena_.make_app(type->type_name, std::move(args));
  }
  case MonoType::MTuple: {
    std::vector<MonoTypePtr> elems;
    for (auto *e : type->elements)
      elems.push_back(zonk(e));
    return arena_.make_tuple(elems);
  }
  case MonoType::MRecord: {
    std::vector<std::pair<std::string, MonoTypePtr>> fields;
    for (auto &[name, ft] : type->record_fields)
      fields.push_back({name, zonk(ft)});
    MonoTypePtr rest = type->row_rest ? zonk(type->row_rest) : nullptr;
    return arena_.make_record(fields, rest);
  }
  case MonoType::Sum: {
    std::vector<MonoTypePtr> Alternatives;
    for (const auto *Alternative : type->sum_alternatives)
      Alternatives.push_back(zonk(Alternative));
    return arena_.make_sum(std::move(Alternatives));
  }
  default:
    return type;
  }
}

// ===== Main Dispatch =====

MonoTypePtr TypeChecker::infer(AstNode *node, std::shared_ptr<TypeEnv> env,
                               int level) {
  if (!node)
    return arena_.make_con(TyCon::Unit);

  evaluation_effect_stack_.push_back({});
  MonoTypePtr result = nullptr;
  auto ty = node->get_type();

  switch (ty) {
  case AST_INTEGER_EXPR:
    result = infer_integer(node);
    break;
  case AST_FLOAT_EXPR:
    result = infer_float(node);
    break;
  case AST_STRING_EXPR:
    result = infer_string(node);
    break;
  case AST_TRUE_LITERAL_EXPR:
  case AST_FALSE_LITERAL_EXPR:
    result = infer_bool(node);
    break;
  case AST_SYMBOL_EXPR:
    result = infer_symbol(node);
    break;
  case AST_LITERAL_EXPR: {
    // LiteralExpr<T> â€” determine type from the actual literal value type
    if (dynamic_cast<LiteralExpr<float> *>(node))
      result = infer_float(node);
    else if (dynamic_cast<LiteralExpr<bool> *>(node))
      result = infer_bool(node);
    else if (dynamic_cast<LiteralExpr<std::string> *>(node))
      result = infer_string(node);
    else if (dynamic_cast<LiteralExpr<int> *>(node))
      result = infer_integer(node);
    else
      result = arena_.make_con(TyCon::Unit);
    break;
  }
  case AST_UNIT_EXPR:
    result = arena_.make_con(TyCon::Unit);
    break;
  case AST_MAIN: {
    auto *Child = static_cast<MainNode *>(node)->node;
    result = infer(Child, env, level);
    propagate_callable_contracts(node, Child, result);
    break;
  }
  case AST_IDENTIFIER_EXPR:
    result = infer_identifier(static_cast<IdentifierExpr *>(node), env, level);
    break;
  case AST_LET_EXPR:
    result = infer_let(static_cast<LetExpr *>(node), env, level);
    break;
  case AST_FUNCTION_EXPR:
    result = infer_function(static_cast<FunctionExpr *>(node), env, level);
    break;
  case AST_APPLY_EXPR:
    result = infer_apply(static_cast<ApplyExpr *>(node), env, level);
    break;
  case AST_IF_EXPR:
    result = infer_if(static_cast<IfExpr *>(node), env, level);
    break;
  case AST_TUPLE_EXPR:
    result = infer_tuple(static_cast<TupleExpr *>(node), env, level);
    break;
  case AST_VALUES_SEQUENCE_EXPR:
    result = infer_seq(static_cast<ValuesSequenceExpr *>(node), env, level);
    break;
  case AST_DO_EXPR:
    result = infer_do(static_cast<DoExpr *>(node), env, level);
    break;
  case AST_CASE_EXPR:
    result = infer_case(static_cast<CaseExpr *>(node), env, level);
    break;
  case AST_CONS_LEFT_EXPR:
    result = infer_cons(static_cast<ConsLeftExpr *>(node), env, level);
    break;
  case AST_RECORD_LITERAL_EXPR: {
    auto *rec = static_cast<RecordLiteralExpr *>(node);
    std::vector<std::pair<std::string, MonoTypePtr>> fields;
    for (auto &[name, expr] : rec->fields) {
      auto *field_type = infer(expr, env, level);
      fields.push_back({name, field_type});
    }
    result = arena_.make_record(fields);
    for (std::size_t Index = 0; Index < rec->fields.size(); ++Index)
      merge_callable_contracts(node, rec->fields[Index].second,
                               fields[Index].second);
    break;
  }
  case AST_FIELD_ACCESS_EXPR: {
    auto *fa = static_cast<FieldAccessExpr *>(node);
    auto *obj_type = infer(fa->identifier, env, level);
    auto *resolved_object = unifier_.resolve(obj_type);

    // Named-record ADTs retain their nominal identity. Resolve their
    // declared field directly instead of forcing the nominal App type
    // to unify with an unrelated structural open row.
    if (resolved_object && resolved_object->tag == MonoType::App) {
      for (const auto &[_, constructor] : constructor_registry_) {
        if (resolved_object->resolved_key
                ? *resolved_object->resolved_key != constructor.adt_key
                : constructor.adt_name != resolved_object->type_name)
          continue;
        const auto field =
            std::find(constructor.field_names.begin(),
                      constructor.field_names.end(), fa->name->value);
        if (field == constructor.field_names.end())
          continue;
        const size_t field_index = static_cast<size_t>(
            std::distance(constructor.field_names.begin(), field));
        if (field_index >= constructor.field_types.size())
          break;

        std::function<MonoTypePtr(const ast::FieldType &)> declared_type;
        declared_type = [&](const ast::FieldType &value) -> MonoTypePtr {
          if (value.is_tuple_type) {
            std::vector<MonoTypePtr> elements;
            for (const auto &element : value.tuple_types)
              elements.push_back(declared_type(element));
            return arena_.make_tuple(elements);
          }
          if (value.is_function_type) {
            MonoTypePtr function_result =
                value.return_types.empty()
                    ? arena_.make_con(TyCon::Unit)
                    : declared_type(value.return_types.front());
            for (auto it = value.param_types.rbegin();
                 it != value.param_types.rend(); ++it)
              function_result =
                  arena_.make_arrow(declared_type(*it), function_result);
            return function_result;
          }
          for (size_t i = 0; i < constructor.type_params.size(); ++i)
            if (value.name == constructor.type_params[i] &&
                i < resolved_object->args.size())
              return resolved_object->args[i];
          if (value.name == "Int")
            return arena_.make_con(TyCon::Int);
          if (value.name == "Float")
            return arena_.make_con(TyCon::Float);
          if (value.name == "Bool")
            return arena_.make_con(TyCon::Bool);
          if (value.name == "String")
            return arena_.make_con(TyCon::String);
          if (value.name == "Symbol")
            return arena_.make_con(TyCon::Symbol);
          if (value.name == "()" || value.name == "Unit")
            return arena_.make_con(TyCon::Unit);
          std::vector<MonoTypePtr> arguments;
          for (const auto &argument : value.type_arguments)
            arguments.push_back(declared_type(argument));
          return arena_.make_app(value.name, arguments);
        };
        result = declared_type(constructor.field_types[field_index]);
        break;
      }
      if (result)
        break;
    }
    // Constrain obj to be a record with this field
    auto *field_var = arena_.fresh_var(level);
    uf_.add_var(field_var->var_id, level);
    auto *row_var = arena_.fresh_var(level);
    uf_.add_var(row_var->var_id, level);
    auto *expected_record =
        arena_.make_record({{fa->name->value, field_var}}, row_var);
    unifier_.unify(obj_type, expected_record, node->Range,
                   "in field access '." + fa->name->value + "'");
    result = unifier_.resolve(field_var);
    break;
  }
  case AST_PERFORM_EXPR:
    result = infer_perform(static_cast<PerformExpr *>(node), env, level);
    break;
  case AST_HANDLE_EXPR:
    result = infer_handle(static_cast<HandleExpr *>(node), env, level);
    break;

    // === Phase 1: Quick wins ===

  case AST_BYTE_EXPR:
    result = arena_.make_con(TyCon::Int);
    break; // byte as Int
  case AST_CHARACTER_EXPR:
    result = arena_.make_con(TyCon::Int);
    break; // char code as Int

  case AST_RANGE_SEQUENCE_EXPR:
    // [start..end] or [start..end..step] â€” all Int, returns Seq Int
    result = arena_.make_app("Seq", {arena_.make_con(TyCon::Int)});
    break;

  case AST_LOGICAL_NOT_OP_EXPR: {
    auto *e = static_cast<LogicalNotOpExpr *>(node);
    auto *t = infer(e->expr, env, level);
    unifier_.unify(t, arena_.make_con(TyCon::Bool), node->Range,
                   "in logical not");
    result = arena_.make_con(TyCon::Bool);
    break;
  }

  case AST_RAISE_EXPR: {
    // raise expr â€” type of the raise is a fresh var (bottom/never returns)
    auto *re = static_cast<RaiseExpr *>(node);
    infer(re->value, env, level);
    auto Transaction = arena_.effect_atoms().beginAppend();
    const auto MayRaise = Transaction.intern(MayRaiseEffectAtom{});
    if (auto Prepared = Transaction.prepareCommit(); !Prepared)
      throw std::logic_error(Prepared.error());
    Transaction.commitPrepared();
    include_ambient_effect(arena_.effect_solver().atoms({MayRaise}),
                           node->Range, "in raise expression");
    result = arena_.fresh_var(level);
    uf_.add_var(result->var_id, level);
    break;
  }

  case AST_IMPORT_EXPR: {
    auto *imp = static_cast<ImportExpr *>(node);
    auto import_env = env->child();
    auto saved_boundaries = concurrency_boundaries_;
    for (auto *clause : imp->clauses) {
      if (auto *Functions = dynamic_cast<FunctionsImport *>(clause)) {
        if (Functions->fromFqn)
          register_imported_nominals(Functions->fromFqn->to_string());
      } else if (auto *Module = dynamic_cast<ModuleImport *>(clause)) {
        if (Module->fqn)
          register_imported_nominals(Module->fqn->to_string());
      }
    }
    for (auto *clause : imp->clauses) {
      if (auto *fi = dynamic_cast<FunctionsImport *>(clause)) {
        std::string mod_fqn = fqn_to_string(fi->fromFqn);
        if (import_src_) {
          const model::ModuleIdentity ImportedModule{mod_fqn};
          for (const auto &instance : import_src_->imported_instances(mod_fqn))
            register_instance(instance.trait_name, instance.type_names,
                              instance.type_params, instance.constraints,
                              &ImportedModule);
        }
        for (auto *fa : fi->aliases) {
          std::string src_name = fa->name->value;
          std::string bind_name = (fa->alias && !fa->alias->value.empty())
                                      ? fa->alias->value
                                      : src_name;
          bind_import_name(import_env, mod_fqn, src_name, bind_name, level);
          if (mod_fqn == "Std\\Task" && src_name == "spawn")
            concurrency_boundaries_[bind_name] = ConcurrencyBoundary::TaskSpawn;
          else if (mod_fqn == "Std\\Channel" && src_name == "send")
            concurrency_boundaries_[bind_name] =
                ConcurrencyBoundary::ChannelSend;
        }
      } else if (auto *mi = dynamic_cast<ModuleImport *>(clause)) {
        if (import_src_ && mi->fqn) {
          std::string mod = mi->fqn->to_string();
          const model::ModuleIdentity ImportedModule{mod};
          for (const auto &instance : import_src_->imported_instances(mod))
            register_instance(instance.trait_name, instance.type_names,
                              instance.type_params, instance.constraints,
                              &ImportedModule);
          for (auto &name : import_src_->imported_module_exports(mod))
            bind_import_name(import_env, mod, name, name, level);
          if (mod == "Std\\Task")
            concurrency_boundaries_["spawn"] = ConcurrencyBoundary::TaskSpawn;
          else if (mod == "Std\\Channel")
            concurrency_boundaries_["send"] = ConcurrencyBoundary::ChannelSend;
        }
      }
    }
    result = infer(imp->expr, import_env, level);
    propagate_callable_contracts(node, imp->expr, result);
    concurrency_boundaries_ = std::move(saved_boundaries);
    break;
  }

  case AST_EXTERN_DECL: {
    // extern name : Type in body â€” bind the declared type (so Linear
    // returns are visible to LinearityChecker) and infer the body.
    auto *ext = static_cast<ExternDeclExpr *>(node);
    auto child_env = env->child();
    auto *declared = from_ast_type(ext->declared_type, level);
    child_env->bind_scheme(ext->name, generalize(declared, -1));
    result = infer(ext->body, child_env, level);
    break;
  }

    // === Phase 1: Generators ===

  case AST_SEQ_GENERATOR_EXPR: {
    auto *gen = static_cast<SeqGeneratorExpr *>(node);
    auto gen_env = env->child();
    if (gen->is_parallel)
      capture_frames_.push_back({gen_env.get(), {}, {}});
    bind_collection_extractor(gen->collectionExtractor, gen_env, level);
    auto *body_type = infer(gen->reducerExpr, gen_env, level);
    if (gen->is_parallel) {
      auto captures = std::move(capture_frames_.back().types);
      capture_frames_.pop_back();
      require_trait("Send", body_type, gen->Range,
                    "result of a parallel comprehension iteration");
      require_captures_shareable(
          captures, gen->Range,
          "value shared with a parallel comprehension iteration");
      if (auto *extractor = dynamic_cast<ValueCollectionExtractorExpr *>(
              gen->collectionExtractor)) {
        auto *source = extractor->collection
                           ? zonk(type_of(extractor->collection))
                           : nullptr;
        if (source && source->tag == MonoType::App && !source->args.empty())
          require_trait("Shareable", source->args.front(), gen->Range,
                        "element of a parallel comprehension source");
      }
    }
    result = arena_.make_app("Seq", {body_type});
    break;
  }

  case AST_SET_GENERATOR_EXPR: {
    auto *gen = static_cast<SetGeneratorExpr *>(node);
    auto gen_env = env->child();
    bind_collection_extractor(gen->collectionExtractor, gen_env, level);
    auto *body_type = infer(gen->reducerExpr, gen_env, level);
    result = arena_.make_app("Set", {body_type});
    require_collection_key_operations(node, body_type);
    break;
  }

  case AST_DICT_GENERATOR_EXPR: {
    auto *gen = static_cast<DictGeneratorExpr *>(node);
    auto gen_env = env->child();
    bind_collection_extractor(gen->collectionExtractor, gen_env, level);
    auto *key_type = infer(gen->reducerExpr->key, gen_env, level);
    auto *val_type = infer(gen->reducerExpr->value, gen_env, level);
    result = arena_.make_app("Dict", {key_type, val_type});
    require_collection_key_operations(node, key_type);
    break;
  }

  case AST_SET_EXPR: {
    auto *se = static_cast<SetExpr *>(node);
    if (se->values.empty()) {
      // `{}` is the shared empty literal for persistent sets and
      // dictionaries. Keep its kind context-polymorphic; the first
      // consuming operation (`put`, union, etc.) selects the
      // concrete collection type. Codegen uses their common HAMT
      // empty representation.
      result = arena_.fresh_var(level);
      uf_.add_var(result->var_id, level);
    } else {
      auto *elem_type = infer(se->values[0], env, level);
      for (size_t i = 1; i < se->values.size(); i++) {
        auto *t = infer(se->values[i], env, level);
        unifier_.unify(elem_type, t, node->Range,
                       "in set literal (all elements must have same type)");
      }
      result = arena_.make_app("Set", {unifier_.resolve(elem_type)});
      for (auto *Value : se->values)
        merge_callable_contracts(node, Value, result->args.front());
      require_collection_key_operations(node, elem_type);
    }
    break;
  }

  case AST_DICT_EXPR: {
    auto *de = static_cast<DictExpr *>(node);
    auto *key_var = arena_.fresh_var(level);
    uf_.add_var(key_var->var_id, level);
    auto *val_var = arena_.fresh_var(level);
    uf_.add_var(val_var->var_id, level);
    for (auto &[k, v] : de->values) {
      auto *kt = infer(k, env, level);
      auto *vt = infer(v, env, level);
      unifier_.unify(key_var, kt, node->Range, "in dict literal key");
      unifier_.unify(val_var, vt, node->Range, "in dict literal value");
    }
    result = arena_.make_app(
        "Dict", {unifier_.resolve(key_var), unifier_.resolve(val_var)});
    for (const auto &[Key, Value] : de->values) {
      merge_callable_contracts(node, Key, result->args[0]);
      merge_callable_contracts(node, Value, result->args[1]);
    }
    if (!de->values.empty())
      require_collection_key_operations(node, key_var);
    break;
  }

    // === Phase 2: Control flow completions ===

  case AST_WITH_EXPR: {
    auto *we = static_cast<WithExpr *>(node);
    infer(we->contextExpr, env, level);
    auto child_env = env->child();
    auto *ctx_var = arena_.fresh_var(level);
    uf_.add_var(ctx_var->var_id, level);
    child_env->bind(we->name->value, ctx_var);
    result = infer(we->bodyExpr, child_env, level);
    break;
  }

  case AST_TRY_CATCH_EXPR: {
    auto *tc = static_cast<TryCatchExpr *>(node);
    // A catch is the built-in MayRaise handler. Infer only the protected
    // expression in its own row, then mask MayRaise before publishing that
    // row to the enclosing expression. MayRaise is deliberately illegal in
    // algebraic-operation masks, so a closed row is filtered directly; an
    // open tail remains conservative because it may acquire another raise.
    // Catch bodies remain outside this boundary, so a re-raise correctly
    // reaches an enclosing catch.
    const auto TryEffects = arena_.effect_solver().derived();
    latent_effect_stack_.push_back({TryEffects});
    isolated_evaluation_roots_.insert(tc->tryExpr);
    auto *try_type = infer(tc->tryExpr, env, level);
    isolated_evaluation_roots_.erase(tc->tryExpr);
    latent_effect_stack_.pop_back();
    const auto TrySummary = arena_.effect_solver().summarize(TryEffects);
    EffectRef Residual = TryEffects;
    if (TrySummary.tails.empty()) {
      auto ResidualAtoms = TrySummary.known_atoms;
      std::erase_if(ResidualAtoms, [&](EffectAtomId Atom) {
        Atom = arena_.effect_solver().canonicalAtom(Atom);
        return arena_.effect_atoms().valid(Atom) &&
               std::holds_alternative<MayRaiseEffectAtom>(
                   arena_.effect_atoms().at(Atom));
      });
      Residual = arena_.effect_solver().atoms(std::move(ResidualAtoms));
    }
    include_ambient_effect(Residual, node->Range, "in try/catch");
    if (tc->catchExpr) {
      for (auto *cp : tc->catchExpr->patterns) {
        // Extract body from the catch pattern's variant
        if (auto *pwog = std::get_if<PatternWithoutGuards *>(&cp->pattern)) {
          if (*pwog && (*pwog)->expr) {
            // A catch pattern introduces the same lexical bindings as a case
            // pattern. Keep each clause isolated so its bindings neither leak
            // into sibling clauses nor overwrite an enclosing name.
            auto catch_env = env->child();
            infer_pattern(cp->matchPattern, catch_env, level);
            auto *catch_type = infer((*pwog)->expr, catch_env, level);
            unifier_.unify(try_type, catch_type, node->Range,
                           "in try/catch (all branches must have same type)");
          }
        }
      }
    }
    result = unifier_.resolve(try_type);
    break;
  }

  case AST_CONS_RIGHT_EXPR: {
    // Same as cons left but reversed: seq :> elem
    auto *cr = static_cast<ConsRightExpr *>(node);
    auto *seq_type = infer(cr->left, env, level);
    auto *elem_type = infer(cr->right, env, level);
    auto *expected_seq = arena_.make_app("Seq", {elem_type});
    unifier_.unify(seq_type, expected_seq, node->Range, "in cons right (:>)");
    result = unifier_.resolve(expected_seq);
    break;
  }

  case AST_IN_EXPR: {
    // x in coll â€” Seq a / Set a (element) or Dict k v (key) â†’ Bool
    auto *ie = static_cast<InExpr *>(node);
    auto *elem_type = infer(ie->left, env, level);
    auto *coll_type = infer(ie->right, env, level);
    auto *resolved = unifier_.resolve(coll_type);
    if (resolved && resolved->tag == MonoType::App &&
        resolved->type_name == "Set" && !resolved->args.empty()) {
      unifier_.unify(elem_type, resolved->args[0], node->Range,
                     "in membership (in) set element");
      require_collection_key_operations(node, resolved->args[0]);
    } else if (resolved && resolved->tag == MonoType::App &&
               resolved->type_name == "Dict" && !resolved->args.empty()) {
      unifier_.unify(elem_type, resolved->args[0], node->Range,
                     "in membership (in) dict key");
      require_collection_key_operations(node, resolved->args[0]);
    } else {
      auto *expected_seq = arena_.make_app("Seq", {elem_type});
      unifier_.unify(coll_type, expected_seq, node->Range,
                     "in membership (in) sequence");
    }
    result = arena_.make_con(TyCon::Bool);
    break;
  }

  case AST_REMOVE_EXPR: {
    // a -- b â€” remove elements of b from a (same collection type)
    auto *re = static_cast<RemoveExpr *>(node);
    auto *left_type = infer(re->left, env, level);
    auto *right_type = infer(re->right, env, level);
    unifier_.unify(
        left_type, right_type, node->Range,
        "in remove (--) (both sides must have the same collection type)");
    result = unifier_.resolve(left_type);
    break;
  }

  case AST_FIELD_UPDATE_EXPR: {
    auto *fu = static_cast<FieldUpdateExpr *>(node);
    auto *obj_type = infer(fu->identifier, env, level);
    for (auto &[name, expr] : fu->updates)
      infer(expr, env, level);
    result = obj_type; // update returns same type as original
    break;
  }

  default:
    // Binary operators
    if (auto *binop = dynamic_cast<BinaryOpExpr *>(node)) {
      result = infer_binary(binop, env, level);
      break;
    }
    // Fallback: return a fresh variable
    result = arena_.fresh_var(level);
    uf_.add_var(result->var_id, level);
    break;
  }

  EffectRef EvaluationEffects = arena_.effect_solver().empty();
  if (!evaluation_effect_stack_.back().Sources.empty())
    EvaluationEffects = arena_.effect_solver().join(
        std::move(evaluation_effect_stack_.back().Sources));
  evaluation_effect_stack_.pop_back();
  node_effects_.insert_or_assign(node, EvaluationEffects);
  if (!evaluation_effect_stack_.empty() &&
      !isolated_evaluation_roots_.contains(node))
    evaluation_effect_stack_.back().Sources.push_back(EvaluationEffects);

  if (result)
    record(node, result);
  return result;
}

// ===== Literals =====

MonoTypePtr TypeChecker::infer_integer(AstNode *) {
  return arena_.make_con(TyCon::Int);
}
MonoTypePtr TypeChecker::infer_float(AstNode *) {
  return arena_.make_con(TyCon::Float);
}
MonoTypePtr TypeChecker::infer_string(AstNode *) {
  return arena_.make_con(TyCon::String);
}
MonoTypePtr TypeChecker::infer_bool(AstNode *) {
  return arena_.make_con(TyCon::Bool);
}
MonoTypePtr TypeChecker::infer_symbol(AstNode *) {
  return arena_.make_con(TyCon::Symbol);
}

// ===== Identifier =====

MonoTypePtr TypeChecker::infer_identifier(IdentifierExpr *node,
                                          std::shared_ptr<TypeEnv> env,
                                          int level) {
  auto scheme = env->lookup(node->name->value);
  if (!scheme) {
    std::string msg = "undefined variable '" + node->name->value + "'";
    // Suggest closest match
    auto names = env->all_names();
    std::string best;
    size_t best_dist = 4; // max distance to suggest
    for (auto &n : names) {
      auto d = edit_distance(node->name->value, n);
      if (d < best_dist) {
        best_dist = d;
        best = n;
      }
    }
    if (!best.empty())
      msg += "; did you mean '" + best + "'?";
    diag_.error(node->Range, ErrorCode::E0103, msg);
    error_count_++;
    return arena_.fresh_var(level);
  }
  std::vector<MonoTypePtr> TypeArguments;
  std::vector<EffectRef> EffectArguments;
  const auto ConstraintBegin = deferred_constraints_.size();
  auto *inferred =
      instantiate_binding(*scheme, level, &TypeArguments, &EffectArguments);
  for (std::size_t Index = ConstraintBegin;
       Index < deferred_constraints_.size(); ++Index) {
    deferred_constraints_[Index].origin = node;
    deferred_constraints_[Index].owning_function =
        function_inference_stack_.empty() ? nullptr
                                          : function_inference_stack_.back();
  }
  if (const auto Contracts = scheme_callable_contracts_.find(scheme->body);
      Contracts != scheme_callable_contracts_.end()) {
    auto Instantiated =
        remap_callable_contracts(scheme->body, inferred, Contracts->second);
    if (!Instantiated.empty())
      expression_callable_contracts_.insert_or_assign(node,
                                                      std::move(Instantiated));
  }
  if (const auto Identity = scheme_identities_.find(scheme->body);
      Identity != scheme_identities_.end())
    instantiations_.insert_or_assign(
        node, SchemeInstantiationProjection{
                  Identity->second, std::move(TypeArguments),
                  std::move(EffectArguments), node->Range});
  for (auto &frame : capture_frames_) {
    if (env->bound_through(node->name->value, frame.local_root))
      continue;
    if (frame.names.insert(node->name->value).second)
      frame.types.push_back(inferred);
  }
  return inferred;
}

// ===== Let Binding =====

/// Pre-scan nested let expressions and bind all lambda alias names with fresh
/// type vars. This enables mutual recursion across nested let blocks â€”
/// `let f = ... g ... in let g = ... f ... in expr` â€” matching the codegen's
/// deferred compilation behavior.
static void prescan_let_lambdas(AstNode *node, std::shared_ptr<TypeEnv> env,
                                TypeArena &arena, UnionFind &uf, int level) {
  auto *let_node = dynamic_cast<LetExpr *>(node);
  if (!let_node)
    return;

  for (auto *alias : let_node->aliases) {
    if (auto *la = dynamic_cast<LambdaAlias *>(alias)) {
      // Only pre-bind if not already bound (avoids overwriting)
      if (!env->lookup(la->name->value)) {
        auto *v = arena.fresh_var(level + 1);
        uf.add_var(v->var_id, level + 1);
        env->bind(la->name->value, v);
      }
    }
  }
  // Recurse into the body to find nested lets
  prescan_let_lambdas(let_node->expr, env, arena, uf, level);
}

MonoTypePtr TypeChecker::infer_let(LetExpr *node, std::shared_ptr<TypeEnv> env,
                                   int level) {
  auto child_env = env->child();

  // Pass 0: Pre-scan this and nested let blocks for all lambda names.
  // Enables mutual recursion across nested lets.
  prescan_let_lambdas(node, child_env, arena_, uf_, level);

  // Pass 1: Pre-bind every local lambda with the same complete curried
  // skeleton used for a recursive module component.  A bare value variable
  // loses the fact that a sibling call reaches a derived body cell, leaving
  // a pure local cycle spuriously open.  Only the final source application
  // runs the body; earlier curried stages are pure closures.
  struct LambdaPrelim {
    LambdaAlias *la;
    MonoTypePtr declaration;
    MonoTypePtr inferred = nullptr;
  };
  std::vector<LambdaPrelim> lambda_prelims;
  for (auto *alias : node->aliases) {
    if (auto *la = dynamic_cast<LambdaAlias *>(alias)) {
      const auto body_effect = arena_.effect_solver().derived();
      predeclared_function_body_effects_[la->lambda] = body_effect;
      auto *result = arena_.fresh_var(level + 1);
      uf_.add_var(result->var_id, level + 1);
      MonoTypePtr declaration = result;
      if (la->lambda->patterns.empty()) {
        declaration = arena_.make_arrow(arena_.make_con(TyCon::Unit), result,
                                        body_effect);
      } else {
        for (int index = static_cast<int>(la->lambda->patterns.size()) - 1;
             index >= 0; --index) {
          auto *parameter = arena_.fresh_var(level + 1);
          uf_.add_var(parameter->var_id, level + 1);
          declaration = arena_.make_arrow(
              parameter, declaration,
              index == static_cast<int>(la->lambda->patterns.size()) - 1
                  ? body_effect
                  : arena_.effect_solver().empty());
        }
      }
      child_env->bind(la->name->value, declaration);
      lambda_prelims.push_back({la, declaration});
    }
  }

  // Pass 2: Infer all alias types.
  size_t lambda_idx = 0;
  for (auto *alias : node->aliases) {
    if (auto *va = dynamic_cast<ValueAlias *>(alias)) {
      auto *rhs_type = infer(va->expr, child_env, level + 1);
      auto scheme = generalize(rhs_type, level);
      if (const auto Contracts = expression_callable_contracts_.find(va->expr);
          Contracts != expression_callable_contracts_.end()) {
        auto Remapped =
            remap_callable_contracts(rhs_type, scheme.body, Contracts->second);
        if (!Remapped.empty())
          scheme_callable_contracts_.insert_or_assign(scheme.body,
                                                      std::move(Remapped));
      }
      child_env->bind_scheme(va->identifier->name->value, scheme);
    } else if (auto *pa = dynamic_cast<PatternAlias *>(alias)) {
      auto *rhs_type = infer(pa->expr, child_env, level + 1);
      auto *pat_type = infer_pattern(pa->pattern, child_env, level + 1);
      unifier_.unify(rhs_type, pat_type, pa->Range, "in pattern destructuring");
    } else if (dynamic_cast<LambdaAlias *>(alias)) {
      auto &prelim = lambda_prelims[lambda_idx++];
      recursive_self_contexts_.push_back({prelim.declaration});
      auto *fn_type = infer(prelim.la->lambda, child_env, level + 1);
      recursive_self_contexts_.pop_back();
      unifier_.unify(prelim.declaration, fn_type, prelim.la->lambda->Range,
                     "in recursive function '" + prelim.la->name->value + "'");
      // A recursive let group has one evolving effect graph.  Freezing
      // a scheme here would snapshot a sibling-derived cell before the
      // sibling has added its body/callback edges.  Generalize the
      // complete group below after every alias has been inferred.
      prelim.inferred = fn_type;
    }
  }

  for (const auto &prelim : lambda_prelims) {
    if (!prelim.inferred)
      continue;
    auto scheme = generalize(prelim.inferred, level,
                             constraints_for_function(prelim.la->lambda));
    scheme_identities_.insert_or_assign(
        scheme.body,
        model::FunctionDeclarationIdentity{model::LocalFunctionIdentity{
            current_module_,
            {static_cast<std::uint32_t>(prelim.la->lambda->Range.Offset)}}});
    function_schemes_.insert_or_assign(prelim.la->lambda, scheme);
    if (const auto Contracts =
            expression_callable_contracts_.find(prelim.la->lambda);
        Contracts != expression_callable_contracts_.end()) {
      auto Remapped = remap_callable_contracts(prelim.inferred, scheme.body,
                                               Contracts->second);
      if (!Remapped.empty())
        scheme_callable_contracts_.insert_or_assign(scheme.body,
                                                    std::move(Remapped));
    }
    child_env->bind_scheme(prelim.la->name->value, scheme);
    if (const auto captures = function_capture_types_.find(prelim.la->lambda);
        captures != function_capture_types_.end())
      named_function_capture_types_[prelim.la->name->value] = captures->second;
  }

  for (const auto &prelim : lambda_prelims)
    predeclared_function_body_effects_.erase(prelim.la->lambda);

  auto *Result = infer(node->expr, child_env, level);
  propagate_callable_contracts(node, node->expr, Result);
  return Result;
}

// ===== Function =====

MonoTypePtr TypeChecker::infer_function(FunctionExpr *node,
                                        std::shared_ptr<TypeEnv> env,
                                        int level) {
  auto fn_env = env->child();

  MonoTypePtr declared_cursor = nullptr;
  if (node->type_signature.has_value())
    declared_cursor = from_ast_type(*node->type_signature, level);

  // Seed annotated parameters before checking the body. This is essential
  // for nominal record field access: `f : Request -> String; f r = r.path`
  // must bind `r` as Request while `.path` is inferred, not only unify the
  // completed function with its signature afterward.
  std::vector<MonoTypePtr> param_types;
  for (auto *pat : node->patterns) {
    MonoTypePtr param_var = nullptr;
    auto *resolved_declared = unifier_.resolve(declared_cursor);
    if (resolved_declared && resolved_declared->tag == MonoType::Arrow) {
      param_var = resolved_declared->param_type;
      declared_cursor = resolved_declared->return_type;
    } else {
      param_var = arena_.fresh_var(level);
      uf_.add_var(param_var->var_id, level);
    }
    param_types.push_back(param_var);

    // A function parameter is a pattern, not just an identifier.  Infer it
    // in the function scope so nested bindings (for example `Some x` or
    // `Pair left right`) are visible to the body, then constrain it to the
    // parameter's fresh type variable.
    auto *pattern_type = infer_pattern(pat, fn_env, level);
    unifier_.unify(param_var, pattern_type, pat->Range,
                   "in function parameter pattern");
  }

  // Capture types are retained as compile-time metadata for concurrency
  // boundaries. Sequential closures remain unrestricted; spawn and parallel
  // constructs later require each captured value to be Shareable.
  capture_frames_.push_back({fn_env.get(), {}, {}});
  function_inference_stack_.push_back(node);

  // A function body is a least-derived effect cell.  Calls include their
  // callee expression into this cell; independent callbacks therefore form
  // an ACI join rather than competing for a single row tail.
  const auto predeclared = predeclared_function_body_effects_.find(node);
  const auto body_effect =
      predeclared == predeclared_function_body_effects_.end()
          ? arena_.effect_solver().derived()
          : predeclared->second;
  latent_effect_stack_.push_back({body_effect});
  MonoTypePtr body_type = nullptr;
  std::vector<std::pair<ast::AstNode *, MonoTypePtr>> BodyResults;
  for (auto *body : node->bodies) {
    if (auto *bwg = dynamic_cast<BodyWithoutGuards *>(body)) {
      isolated_evaluation_roots_.insert(bwg->expr);
      auto *bt = infer(bwg->expr, fn_env, level);
      isolated_evaluation_roots_.erase(bwg->expr);
      BodyResults.emplace_back(bwg->expr, bt);
      if (!body_type)
        body_type = bt;
      else
        unifier_.unify(body_type, bt, node->Range, "in function body");
    }
  }
  if (!body_type)
    body_type = arena_.make_con(TyCon::Unit);
  if (declared_cursor) {
    auto *ExpectedResult = unifier_.resolve(declared_cursor);
    const bool DeclaredPromise = ExpectedResult &&
                                 ExpectedResult->tag == MonoType::App &&
                                 ExpectedResult->type_name == "Promise" &&
                                 ExpectedResult->promise_result_contract &&
                                 !ExpectedResult->args.empty();
    if (DeclaredPromise)
      ExpectedResult = ExpectedResult->args.front();
    for (const auto &[Body, Type] : BodyResults)
      constrain_contextual_type(Body, Type, ExpectedResult, Body->Range,
                                "against the declared function result type");
    body_type =
        constrain_contextual_type(node, body_type, ExpectedResult, node->Range,
                                  "against the declared function result type");
    if (DeclaredPromise)
      body_type = declared_cursor;
    sum_injections_.erase(node);
  }

  function_capture_types_[node] = capture_frames_.back().types;
  capture_frames_.pop_back();
  function_inference_stack_.pop_back();

  const CollectedRow collected = latent_effect_stack_.back();
  latent_effect_stack_.pop_back();

  // Build curried arrow type: a -> b -> c -> ret. Evaluation begins only
  // once the final source parameter is supplied; earlier partial
  // applications merely allocate a closure and therefore have an empty row.
  // Module summaries intentionally aggregate all stages in effect_row_info.
  // `\() -> body` parses with zero patterns; it is still a thunk (Unit -> ret).
  MonoTypePtr fn_type = body_type;
  if (param_types.empty())
    fn_type = arena_.make_arrow(arena_.make_con(TyCon::Unit), body_type,
                                collected.effect);
  else {
    for (int i = (int)param_types.size() - 1; i >= 0; i--)
      fn_type = arena_.make_arrow(param_types[i], fn_type,
                                  i == (int)param_types.size() - 1
                                      ? collected.effect
                                      : arena_.effect_solver().empty());
  }

  check_param_borrow_annotations(node);

  auto parameterOwnership = [&](std::size_t Index, MonoTypePtr Type) {
    const auto *Resolved = unifier_.resolve(Type);
    if (Resolved && Resolved->tag == MonoType::Con &&
        Resolved->con != TyCon::String)
      return model::ParameterOwnership::Trivial;
    return Index < node->param_borrow.size() && node->param_borrow[Index]
               ? model::ParameterOwnership::Borrow
               : model::ParameterOwnership::Consume;
  };
  const auto resultOwnership = [&](MonoTypePtr Type) {
    const auto *Resolved = unifier_.resolve(Type);
    return Resolved && Resolved->tag == MonoType::Con &&
                   Resolved->con != TyCon::String
               ? model::ResultOwnership::Trivial
               : model::ResultOwnership::Owned;
  };
  const std::size_t Arity = std::max<std::size_t>(param_types.size(), 1);
  std::vector<model::ParameterOwnership> Ownerships;
  Ownerships.reserve(Arity);
  auto *Stage = fn_type;
  for (std::size_t Index = 0;
       Index < Arity && Stage && Stage->tag == MonoType::Arrow; ++Index) {
    Ownerships.push_back(parameterOwnership(Index, Stage->param_type));
    Stage = Stage->return_type;
  }
  auto &Contracts = function_callable_contracts_[node];
  Stage = fn_type;
  for (std::size_t Index = 0;
       Index < Ownerships.size() && Stage && Stage->tag == MonoType::Arrow;
       ++Index) {
    Contracts.push_back(CallableTypeContract{
        Stage,
        {Ownerships.begin() + static_cast<std::ptrdiff_t>(Index),
         Ownerships.end()},
        resultOwnership(body_type),
        Index == 0 ? model::CallingConvention::DirectYona
                   : model::CallingConvention::ClosureEntry,
        node->Range});
    Stage = Stage->return_type;
  }
  const std::vector<CallableTypeContract> *ReturnedContracts = nullptr;
  bool ReturnedContractsAgree = true;
  for (const auto &[Body, _] : BodyResults) {
    const auto Found = expression_callable_contracts_.find(Body);
    if (Found == expression_callable_contracts_.end()) {
      ReturnedContractsAgree = false;
      break;
    }
    if (!ReturnedContracts)
      ReturnedContracts = &Found->second;
    else if (!same_callable_contracts(*ReturnedContracts, Found->second)) {
      ReturnedContractsAgree = false;
      break;
    }
  }
  if (ReturnedContractsAgree && ReturnedContracts)
    for (auto Contract : *ReturnedContracts) {
      const auto Duplicate =
          std::ranges::find_if(Contracts, [&](const auto &Existing) {
            return unifier_.resolve(Existing.Arrow) ==
                       unifier_.resolve(Contract.Arrow) &&
                   Existing.ParameterOwnerships ==
                       Contract.ParameterOwnerships &&
                   Existing.ResultContract == Contract.ResultContract &&
                   Existing.Convention == Contract.Convention;
          });
      if (Duplicate == Contracts.end())
        Contracts.push_back(std::move(Contract));
    }
  expression_callable_contracts_.insert_or_assign(node, Contracts);
  projection_function_types_.insert_or_assign(node, fn_type);
  return fn_type;
}

void TypeChecker::check_param_borrow_annotations(FunctionExpr *node) {
  if (node->param_borrow.empty())
    return;

  for (size_t i = 0; i < node->patterns.size(); ++i) {
    bool want = i < node->param_borrow.size() && node->param_borrow[i];
    if (!want)
      continue;

    auto *pat = node->patterns[i];
    if (pat->get_type() != AST_PATTERN_VALUE) {
      diag_.error(pat->Range, ErrorCode::E0603,
                  "`@borrow` is only allowed on simple identifier parameters");
      error_count_++;
      continue;
    }
    auto *pv = static_cast<PatternValue *>(pat);
    if (!std::holds_alternative<IdentifierExpr *>(pv->expr)) {
      diag_.error(pat->Range, ErrorCode::E0603,
                  "`@borrow` is only allowed on simple identifier parameters");
      error_count_++;
      continue;
    }
    const std::string &pname =
        std::get<IdentifierExpr *>(pv->expr)->name->value;

    bool escapes = false;
    for (auto *body : node->bodies) {
      if (auto *bwg = dynamic_cast<BodyWithoutGuards *>(body)) {
        if (compiler::analysis::heap_param_may_escape(bwg->expr, pname, true))
          escapes = true;
      } else if (auto *g = dynamic_cast<BodyWithGuards *>(body)) {
        if (compiler::analysis::heap_param_may_escape(g->guard, pname, false) ||
            compiler::analysis::heap_param_may_escape(g->expr, pname, true))
          escapes = true;
      }
    }
    if (escapes) {
      diag_.error(pat->Range, ErrorCode::E0603,
                  "borrowed parameter '" + pname +
                      "' must not escape "
                      "(return, store in literal, capture in closure, or be a "
                      "case scrutinee)");
      error_count_++;
    }
  }
}

// ===== Application =====

MonoTypePtr TypeChecker::infer_apply(ApplyExpr *node,
                                     std::shared_ptr<TypeEnv> env, int level) {
  // Infer callee
  MonoTypePtr callee_type = nullptr;
  std::vector<CallableTypeContract> CalleeContracts;
  size_t constraint_begin = deferred_constraints_.size();
  size_t constraint_end = constraint_begin;
  if (auto *nc = dynamic_cast<NameCall *>(node->call)) {
    auto scheme = env->lookup(nc->name->value);
    if (scheme) {
      std::vector<MonoTypePtr> TypeArguments;
      std::vector<EffectRef> EffectArguments;
      callee_type =
          instantiate_binding(*scheme, level, &TypeArguments, &EffectArguments);
      if (const auto Contracts = scheme_callable_contracts_.find(scheme->body);
          Contracts != scheme_callable_contracts_.end())
        CalleeContracts = remap_callable_contracts(scheme->body, callee_type,
                                                   Contracts->second);
      if (const auto Identity = scheme_identities_.find(scheme->body);
          Identity != scheme_identities_.end())
        instantiations_.insert_or_assign(
            node, SchemeInstantiationProjection{
                      Identity->second, std::move(TypeArguments),
                      std::move(EffectArguments), node->Range});
      constraint_end = deferred_constraints_.size();
      for (size_t i = constraint_begin; i < constraint_end; ++i) {
        deferred_constraints_[i].origin = node;
        deferred_constraints_[i].owning_function =
            function_inference_stack_.empty()
                ? nullptr
                : function_inference_stack_.back();
      }
    } else {
      std::string msg = "undefined function '" + nc->name->value + "'";
      auto names = env->all_names();
      std::string best;
      size_t best_dist = 4;
      for (auto &n : names) {
        auto d = edit_distance(nc->name->value, n);
        if (d < best_dist) {
          best_dist = d;
          best = n;
        }
      }
      if (!best.empty())
        msg += "; did you mean '" + best + "'?";
      diag_.error(node->Range, ErrorCode::E0104, msg);
      error_count_++;
      return arena_.fresh_var(level);
    }
  } else if (auto *ec = dynamic_cast<ExprCall *>(node->call)) {
    callee_type = infer(ec->expr, env, level);
    if (const auto Contracts = expression_callable_contracts_.find(ec->expr);
        Contracts != expression_callable_contracts_.end())
      CalleeContracts = Contracts->second;
  } else {
    callee_type = arena_.fresh_var(level);
    uf_.add_var(callee_type->var_id, level);
  }

  bool direct_recursive_call = false;
  if (auto *initial = unifier_.resolve(callee_type)) {
    for (const auto &self : recursive_self_contexts_) {
      if (self.preliminary && unifier_.resolve(self.preliminary) == initial) {
        direct_recursive_call = true;
        break;
      }
      for (const auto continuation : self.continuations) {
        if (continuation == callee_type ||
            (continuation && unifier_.resolve(continuation) == initial)) {
          direct_recursive_call = true;
          break;
        }
      }
      if (direct_recursive_call)
        break;
    }
  }

  // Apply each argument
  MonoTypePtr result_type = callee_type;
  for (auto &arg_variant : node->args) {
    AstNode *arg_node =
        std::holds_alternative<ExprNode *>(arg_variant)
            ? static_cast<AstNode *>(std::get<ExprNode *>(arg_variant))
            : static_cast<AstNode *>(std::get<ValueExpr *>(arg_variant));

    auto *arg_type = infer(arg_node, env, level);

    auto *resolved = unifier_.resolve(result_type);

    if (resolved && resolved->tag == MonoType::Arrow)
      arg_type = constrain_contextual_type(
          arg_node, arg_type, resolved->param_type, arg_node->Range,
          "at Promise value demand in function application");

    auto *result_var = arena_.fresh_var(level);
    uf_.add_var(result_var->var_id, level);
    auto *expected_fn = arena_.make_arrow(arg_type, result_var,
                                          arena_.effect_solver().flexible());

    if (!unifier_.unify(result_type, expected_fn, node->Range,
                        "in function application")) {
      // Imported `.yonai` tags distinguish SEQ from ADT (Stream). A
      // failed apply here is a hard mismatch, not partial inference â€”
      // increment so CLI/`has_direct_errors()` reject the program.
      auto is_app = [](MonoTypePtr t, const char *name) {
        return t && t->tag == MonoType::App && t->type_name == name;
      };
      auto is_collection = [&](MonoTypePtr t) {
        return is_app(t, "Seq") || is_app(t, "Set") || is_app(t, "Dict");
      };
      auto *expected_param = unifier_.resolve(result_type);
      if (expected_param && expected_param->tag == MonoType::Arrow)
        expected_param = unifier_.resolve(expected_param->param_type);
      auto *actual = unifier_.resolve(arg_type);
      if ((is_app(expected_param, "ADT") && is_collection(actual)) ||
          (is_collection(expected_param) && is_app(actual, "ADT"))) {
        if (is_app(actual, "Seq") || is_app(expected_param, "Seq"))
          diag_.note(node->Range,
                     "a Seq is not a Stream; wrap a sequence with fromSeq");
        error_count_++;
      }
      return result_var;
    }

    // After unification a formerly-unknown callee resolves to the
    // expected arrow.  Its solver effect is now equal to the declared or
    // inferred arrow effect, so inclusion records the exact call edge.
    auto *after = unifier_.resolve(resolved);
    if (!direct_recursive_call)
      apply_callee_effects(after, node->Range);

    if (direct_recursive_call && after && after->tag == MonoType::Arrow) {
      const auto continuation = after->return_type;
      for (auto &self : recursive_self_contexts_)
        if (std::find(self.continuations.begin(), self.continuations.end(),
                      continuation) == self.continuations.end())
          self.continuations.push_back(continuation);
    }

    // Keep the original return type so inner-arrow effects survive multi-arg
    // apply
    if (after && after->tag == MonoType::Arrow && after->return_type)
      result_type = after->return_type;
    else
      result_type = result_var;
  }

  std::string concurrency_callee;
  std::optional<ConcurrencyBoundary> concurrency_boundary;
  for (ApplyExpr *current = node; current;) {
    if (auto *named = dynamic_cast<NameCall *>(current->call)) {
      concurrency_callee = named->name->value;
      if (const auto found = concurrency_boundaries_.find(concurrency_callee);
          found != concurrency_boundaries_.end())
        concurrency_boundary = found->second;
      break;
    }
    if (auto *expression = dynamic_cast<ExprCall *>(current->call)) {
      current = dynamic_cast<ApplyExpr *>(expression->expr);
      continue;
    }
    break;
  }
  if (concurrency_boundary)
    enforce_concurrency_boundary(node, concurrency_callee,
                                 concurrency_boundary);

  auto *Result = unifier_.resolve(result_type);
  if (Result) {
    auto Contracts = remap_callable_contracts(Result, Result, CalleeContracts);
    if (!Contracts.empty())
      expression_callable_contracts_.insert_or_assign(node,
                                                      std::move(Contracts));
  }
  return Result;
}

void TypeChecker::require_trait(const std::string &trait_name, MonoTypePtr type,
                                const SourceRange &loc, std::string context) {
  if (!type)
    return;
  deferred_constraints_.emplace_back(trait_name, unifier_.resolve(type), loc,
                                     std::move(context));
  deferred_constraints_.back().owning_function =
      function_inference_stack_.empty() ? nullptr
                                        : function_inference_stack_.back();
  deferred_constraints_.back().evidence_role =
      DeferredConstraint::EvidenceRole::None;
}

void TypeChecker::require_collection_key_operations(const ast::AstNode *Node,
                                                    MonoTypePtr KeyType) {
  if (!Node || !KeyType)
    return;
  const auto Add = [&](const char *Trait, const char *Context,
                       DeferredConstraint::EvidenceRole Role) {
    deferred_constraints_.emplace_back(Trait, KeyType, Node->Range, Context);
    auto &Constraint = deferred_constraints_.back();
    Constraint.origin = Node;
    Constraint.owning_function = function_inference_stack_.empty()
                                     ? nullptr
                                     : function_inference_stack_.back();
    Constraint.evidence_role = Role;
  };
  Add("Hash", "required by collection key operation",
      DeferredConstraint::EvidenceRole::CollectionHash);
  Add("Eq", "required by collection key operation",
      DeferredConstraint::EvidenceRole::CollectionEquals);
}

void TypeChecker::require_captures_shareable(
    const std::vector<MonoTypePtr> &captures, const SourceRange &loc,
    const std::string &context) {
  for (auto *capture : captures)
    if (auto *resolved = unifier_.resolve(capture);
        resolved && resolved->tag != MonoType::Arrow)
      require_trait("Shareable", resolved, loc, context);
}

void TypeChecker::enforce_concurrency_boundary(
    ApplyExpr *node, const std::string &callee_name,
    std::optional<ConcurrencyBoundary> boundary) {
  if (!boundary)
    return;
  std::vector<AstNode *> arguments;
  std::vector<ApplyExpr *> chain;
  for (ApplyExpr *current = node; current;) {
    chain.push_back(current);
    if (auto *expression = dynamic_cast<ExprCall *>(current->call))
      current = dynamic_cast<ApplyExpr *>(expression->expr);
    else
      break;
  }
  for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
    for (auto &argument : (*it)->args)
      arguments.push_back(
          std::holds_alternative<ExprNode *>(argument)
              ? static_cast<AstNode *>(std::get<ExprNode *>(argument))
              : static_cast<AstNode *>(std::get<ValueExpr *>(argument)));
  }

  if (*boundary == ConcurrencyBoundary::ChannelSend && arguments.size() == 2) {
    require_trait("Send", type_of(arguments[1]), node->Range,
                  "value sent through Std\\Channel.send");
    return;
  }
  if (*boundary != ConcurrencyBoundary::TaskSpawn || arguments.size() != 1)
    return;

  auto *thunk_type = zonk(type_of(arguments.front()));
  if (thunk_type && thunk_type->tag == MonoType::Arrow)
    require_trait("Send", thunk_type->return_type, node->Range,
                  "result returned from Std\\Task.spawn");

  std::vector<MonoTypePtr> captures;
  if (auto *function = dynamic_cast<FunctionExpr *>(arguments.front())) {
    if (const auto found = function_capture_types_.find(function);
        found != function_capture_types_.end())
      captures = found->second;
  } else if (auto *identifier =
                 dynamic_cast<IdentifierExpr *>(arguments.front())) {
    if (const auto found =
            named_function_capture_types_.find(identifier->name->value);
        found != named_function_capture_types_.end())
      captures = found->second;
  }
  require_captures_shareable(
      captures, node->Range,
      "value captured by the closure passed to Std\\Task.spawn");
}

std::optional<EffectRef> TypeChecker::callee_effect(MonoTypePtr callee) {
  callee = unifier_.resolve(callee);
  if (!callee)
    return std::nullopt;
  if (callee->tag == MonoType::Arrow &&
      callee->effect_solver == &arena_.effect_solver() &&
      callee->arrow_effect.valid())
    return callee->arrow_effect;
  return std::nullopt;
}

bool TypeChecker::materialize_effect_masks(EffectRef effect,
                                           const SourceRange &loc,
                                           const std::string &context) {
  std::function<bool(EffectRef, EffectRef)> EffectsMatch;
  std::function<bool(MonoTypePtr, MonoTypePtr,
                     std::set<std::pair<MonoTypePtr, MonoTypePtr>> &)>
      TypesMatch;
  EffectsMatch = [&](EffectRef Pattern, EffectRef Candidate) {
    if (Pattern == Candidate)
      return true;
    const auto PatternSummary = arena_.effect_solver().summarize(Pattern);
    if (PatternSummary.is_open())
      return true;
    return PatternSummary == arena_.effect_solver().summarize(Candidate);
  };
  TypesMatch = [&](MonoTypePtr Pattern, MonoTypePtr Candidate,
                   std::set<std::pair<MonoTypePtr, MonoTypePtr>> &Seen) {
    Pattern = unifier_.resolve(Pattern);
    Candidate = unifier_.resolve(Candidate);
    if (Pattern == Candidate)
      return true;
    if (!Pattern || !Candidate || !Seen.emplace(Pattern, Candidate).second)
      return Pattern && Candidate;
    if (Pattern->tag == MonoType::Var || Candidate->tag == MonoType::Var)
      return true;
    if (Pattern->tag != Candidate->tag)
      return false;
    switch (Pattern->tag) {
    case MonoType::Var:
      return true;
    case MonoType::Con:
      return Pattern->con == Candidate->con;
    case MonoType::Arrow:
      return TypesMatch(Pattern->param_type, Candidate->param_type, Seen) &&
             TypesMatch(Pattern->return_type, Candidate->return_type, Seen) &&
             EffectsMatch(Pattern->arrow_effect, Candidate->arrow_effect);
    case MonoType::App: {
      const bool SameIdentity =
          (Pattern->resolved_key || Candidate->resolved_key)
              ? Pattern->resolved_key && Candidate->resolved_key &&
                    *Pattern->resolved_key == *Candidate->resolved_key
              : Pattern->type_name == Candidate->type_name;
      if (!SameIdentity || Pattern->args.size() != Candidate->args.size())
        return false;
      for (std::size_t Index = 0; Index < Pattern->args.size(); ++Index)
        if (!TypesMatch(Pattern->args[Index], Candidate->args[Index], Seen))
          return false;
      return true;
    }
    case MonoType::MTuple:
      if (Pattern->elements.size() != Candidate->elements.size())
        return false;
      for (std::size_t Index = 0; Index < Pattern->elements.size(); ++Index)
        if (!TypesMatch(Pattern->elements[Index], Candidate->elements[Index],
                        Seen))
          return false;
      return true;
    case MonoType::MRecord:
      if (Pattern->record_fields.size() != Candidate->record_fields.size() ||
          static_cast<bool>(Pattern->row_rest) !=
              static_cast<bool>(Candidate->row_rest))
        return false;
      for (std::size_t Index = 0; Index < Pattern->record_fields.size();
           ++Index)
        if (Pattern->record_fields[Index].first !=
                Candidate->record_fields[Index].first ||
            !TypesMatch(Pattern->record_fields[Index].second,
                        Candidate->record_fields[Index].second, Seen))
          return false;
      return !Pattern->row_rest ||
             TypesMatch(Pattern->row_rest, Candidate->row_rest, Seen);
    case MonoType::Sum: {
      if (Pattern->sum_alternatives.size() !=
          Candidate->sum_alternatives.size())
        return false;
      std::vector<bool> Matched(Candidate->sum_alternatives.size());
      for (const auto *PatternAlternative : Pattern->sum_alternatives) {
        bool Found = false;
        for (std::size_t Index = 0; Index < Candidate->sum_alternatives.size();
             ++Index) {
          if (Matched[Index])
            continue;
          auto CandidateSeen = Seen;
          if (!TypesMatch(PatternAlternative,
                          Candidate->sum_alternatives[Index], CandidateSeen))
            continue;
          Seen = std::move(CandidateSeen);
          Matched[Index] = true;
          Found = true;
          break;
        }
        if (!Found)
          return false;
      }
      return true;
    }
    }
    return false;
  };

  const auto Result = arena_.effect_solver().materializeMaskMatches(
      effect, [&](EffectAtomId Pattern, EffectAtomId Candidate) {
        const auto *PatternOperation = std::get_if<OperationEffectAtom>(
            &arena_.effect_atoms().at(Pattern));
        const auto *CandidateOperation = std::get_if<OperationEffectAtom>(
            &arena_.effect_atoms().at(Candidate));
        if (!PatternOperation || !CandidateOperation)
          return false;
        // V1 interfaces have no operation declarations: their effect rows
        // contain a key but no declaration-ordered type/effect application.
        // They also historically exposed source-local effects by short name,
        // so their defining module is not usable at a client handler site.
        // Treat only those explicitly tracked legacy candidates as opaque,
        // short-key matches. Typed v2/source applications below still require
        // exact qualified identity, arity, and ordered arguments.
        if (legacy_untyped_effect_atoms_.contains(Candidate))
          return PatternOperation->Key.Effect.Name ==
                     CandidateOperation->Key.Effect.Name &&
                 PatternOperation->Key.Operation ==
                     CandidateOperation->Key.Operation;
        if (PatternOperation->Key != CandidateOperation->Key)
          return false;
        if (PatternOperation->TypeArguments.size() !=
                CandidateOperation->TypeArguments.size() ||
            PatternOperation->EffectArguments.size() !=
                CandidateOperation->EffectArguments.size())
          return false;
        std::set<std::pair<MonoTypePtr, MonoTypePtr>> Seen;
        for (std::size_t Index = 0;
             Index < PatternOperation->TypeArguments.size(); ++Index)
          if (!TypesMatch(PatternOperation->TypeArguments[Index],
                          CandidateOperation->TypeArguments[Index], Seen))
            return false;
        for (std::size_t Index = 0;
             Index < PatternOperation->EffectArguments.size(); ++Index)
          if (!EffectsMatch(PatternOperation->EffectArguments[Index],
                            CandidateOperation->EffectArguments[Index]))
            return false;
        return true;
      });
  if (Result != EffectConstraintResult::Conflict)
    return true;
  diag_.error(loc, ErrorCode::E0100,
              "incompatible effect constraints " + context);
  ++error_count_;
  return false;
}

void TypeChecker::include_ambient_effect(EffectRef effect,
                                         const SourceRange &loc,
                                         const std::string &context) {
  include_evaluation_effect(effect);
  if (latent_effect_stack_.empty())
    return;
  if (!materialize_effect_masks(effect, loc, context))
    return;
  const auto result = arena_.effect_solver().include(
      effect, latent_effect_stack_.back().effect);
  if (result == EffectConstraintResult::Conflict) {
    diag_.error(loc, ErrorCode::E0100,
                "incompatible effect constraints " + context);
    ++error_count_;
  }
}

void TypeChecker::include_evaluation_effect(EffectRef effect) {
  if (effect.valid() && !evaluation_effect_stack_.empty())
    evaluation_effect_stack_.back().Sources.push_back(effect);
}

void TypeChecker::apply_callee_effects(MonoTypePtr callee,
                                       const SourceRange &apply_loc) {
  const auto effect = callee_effect(callee);
  if (!effect)
    return;
  if (!latent_effect_stack_.empty()) {
    include_ambient_effect(*effect, apply_loc, "in function application");
    return;
  }

  include_evaluation_effect(*effect);

  if (!materialize_effect_masks(*effect, apply_loc, "in function application"))
    return;

  const auto summary = arena_.effect_solver().summarize(*effect);
  for (const auto RawAtom : summary.known_atoms) {
    const auto Atom = arena_.effect_solver().canonicalAtom(RawAtom);
    const auto Operation = arena_.effect_atoms().format(Atom);
    const auto origin = effect_origins_.find(Atom);
    const auto &loc =
        origin != effect_origins_.end() && origin->second.isValid()
            ? origin->second
            : apply_loc;
    diag_.error(loc, ErrorCode::E0202,
                "unhandled effect operation '" + Operation + "'");
    diag_.note(apply_loc,
               "applied here with no covering handler for " + Operation);
    ++error_count_;
  }
}

// ===== If =====

MonoTypePtr TypeChecker::infer_if(IfExpr *node, std::shared_ptr<TypeEnv> env,
                                  int level) {
  auto *cond_type = infer(node->condition, env, level);
  unifier_.unify(cond_type, arena_.make_con(TyCon::Bool), node->Range,
                 "in if condition (expected Bool)");

  auto *then_type = infer(node->thenExpr, env, level);
  auto *else_type = infer(node->elseExpr, env, level);
  unifier_.unify(then_type, else_type, node->Range,
                 "in if branches (then and else must have same type)");

  auto *Result = unifier_.resolve(then_type);
  const auto ThenContracts =
      expression_callable_contracts_.find(node->thenExpr);
  const auto ElseContracts =
      expression_callable_contracts_.find(node->elseExpr);
  if (ThenContracts != expression_callable_contracts_.end() &&
      ElseContracts != expression_callable_contracts_.end() &&
      same_callable_contracts(ThenContracts->second, ElseContracts->second))
    propagate_callable_contracts(node, node->thenExpr, Result);
  return Result;
}

// ===== Binary Operators =====

std::string TypeChecker::op_name(AstNodeType type) {
  switch (type) {
  case AST_ADD_EXPR:
    return "+";
  case AST_SUBTRACT_EXPR:
    return "-";
  case AST_MULTIPLY_EXPR:
    return "*";
  case AST_DIVIDE_EXPR:
    return "/";
  case AST_MODULO_EXPR:
    return "%";
  case AST_POWER_EXPR:
    return "**";
  case AST_EQ_EXPR:
    return "==";
  case AST_NEQ_EXPR:
    return "!=";
  case AST_LT_EXPR:
    return "<";
  case AST_GT_EXPR:
    return ">";
  case AST_LTE_EXPR:
    return "<=";
  case AST_GTE_EXPR:
    return ">=";
  case AST_LOGICAL_AND_EXPR:
    return "&&";
  case AST_LOGICAL_OR_EXPR:
    return "||";
  case AST_JOIN_EXPR:
    return "++";
  case AST_REMOVE_EXPR:
    return "--";
  case AST_IN_EXPR:
    return "in";
  case AST_CONS_LEFT_EXPR:
    return "::";
  case AST_PIPE_LEFT_EXPR:
    return "<|";
  case AST_PIPE_RIGHT_EXPR:
    return "|>";
  default:
    return "?";
  }
}

MonoTypePtr TypeChecker::infer_binary(BinaryOpExpr *node,
                                      std::shared_ptr<TypeEnv> env, int level) {
  // The parser makes ++ chains left-associative. Infer the left spine
  // iteratively so long normal concatenations do not consume one native
  // stack frame per operand in Windows Debug builds.
  if (node->get_type() == AST_JOIN_EXPR &&
      dynamic_cast<JoinExpr *>(node->left)) {
    std::vector<ExprNode *> operands;
    ExprNode *current = node;
    while (auto *join = dynamic_cast<JoinExpr *>(current)) {
      operands.push_back(join->right);
      current = join->left;
    }
    operands.push_back(current);
    std::reverse(operands.begin(), operands.end());

    auto *result = infer(operands.front(), env, level);
    const auto is_string = [](MonoTypePtr type) {
      return type && type->tag == MonoType::Con && type->con == TyCon::String;
    };
    for (size_t i = 1; i < operands.size(); ++i) {
      auto *right_type = infer(operands[i], env, level);
      if (is_string(unifier_.resolve(result)) ||
          is_string(unifier_.resolve(right_type))) {
        auto *string = arena_.make_con(TyCon::String);
        unifier_.unify(
            result, string, node->Range,
            "in operator ++ (both operands must be String or both Seq)");
        unifier_.unify(
            right_type, string, node->Range,
            "in operator ++ (both operands must be String or both Seq)");
        result = string;
      } else {
        auto *element = arena_.fresh_var(level);
        uf_.add_var(element->var_id, level);
        auto *sequence = arena_.make_app("Seq", {element});
        unifier_.unify(result, sequence, node->Range,
                       "in operator ++ (left sequence)");
        unifier_.unify(right_type, sequence, node->Range,
                       "in operator ++ (right sequence)");
        result = unifier_.resolve(sequence);
      }
    }
    return result;
  }

  auto *left_type = infer(node->left, env, level);
  auto *right_type = infer(node->right, env, level);

  std::string name = op_name(node->get_type());

  // Equality and ordering are trait-governed source operations. Keep the
  // ordinary operator scheme for operand/result unification, but record the
  // semantic obligation here so every frontend mode validates it before
  // LLVM lowering. `!=` shares Eq.eq and the relational operators share
  // Ord.compare; there is deliberately no second operator-specific contract.
  if (name == "==" || name == "!=") {
    deferred_constraints_.push_back(
        {"Eq", left_type, node->Range, "required by operator '" + name + "'"});
    deferred_constraints_.back().origin = node;
    deferred_constraints_.back().owning_function =
        function_inference_stack_.empty() ? nullptr
                                          : function_inference_stack_.back();
  } else if (name == "<" || name == ">" || name == "<=" || name == ">=") {
    deferred_constraints_.push_back(
        {"Ord", left_type, node->Range, "required by operator '" + name + "'"});
    deferred_constraints_.back().origin = node;
    deferred_constraints_.back().owning_function =
        function_inference_stack_.empty() ? nullptr
                                          : function_inference_stack_.back();
  }

  if (name == "++") {
    auto *left = unifier_.resolve(left_type);
    auto *right = unifier_.resolve(right_type);
    const auto is_string = [](MonoTypePtr type) {
      return type && type->tag == MonoType::Con && type->con == TyCon::String;
    };
    if (is_string(left) || is_string(right)) {
      auto *string = arena_.make_con(TyCon::String);
      unifier_.unify(
          left_type, string, node->Range,
          "in operator ++ (both operands must be String or both Seq)");
      unifier_.unify(
          right_type, string, node->Range,
          "in operator ++ (both operands must be String or both Seq)");
      return string;
    }
    auto *element = arena_.fresh_var(level);
    uf_.add_var(element->var_id, level);
    auto *sequence = arena_.make_app("Seq", {element});
    unifier_.unify(left_type, sequence, node->Range,
                   "in operator ++ (left sequence)");
    unifier_.unify(right_type, sequence, node->Range,
                   "in operator ++ (right sequence)");
    return unifier_.resolve(sequence);
  }

  auto scheme = env->lookup(name);
  if (!scheme) {
    // Fallback: assume left_type -> left_type -> left_type
    unifier_.unify(left_type, right_type, node->Range, "in operator " + name);
    return unifier_.resolve(left_type);
  }

  // Instantiate operator type and unify with arguments
  auto *op_type = instantiate_binding(*scheme, level);
  auto *result_var = arena_.fresh_var(level);
  uf_.add_var(result_var->var_id, level);

  auto *expected =
      arena_.make_arrow(left_type, arena_.make_arrow(right_type, result_var));
  unifier_.unify(op_type, expected, node->Range, "in operator " + name);

  return unifier_.resolve(result_var);
}

// ===== Tuple =====

MonoTypePtr TypeChecker::infer_tuple(TupleExpr *node,
                                     std::shared_ptr<TypeEnv> env, int level) {
  std::vector<MonoTypePtr> elem_types;
  for (auto *v : node->values)
    elem_types.push_back(infer(v, env, level));
  auto *Result = arena_.make_tuple(elem_types);
  for (std::size_t Index = 0; Index < node->values.size(); ++Index)
    merge_callable_contracts(node, node->values[Index],
                             Result->elements[Index]);
  return Result;
}

// ===== Sequence =====

MonoTypePtr TypeChecker::infer_seq(ValuesSequenceExpr *node,
                                   std::shared_ptr<TypeEnv> env, int level) {
  if (node->values.empty())
    return arena_.make_app("Seq", {arena_.fresh_var(level)});

  auto *elem_type = infer(node->values[0], env, level);
  for (size_t i = 1; i < node->values.size(); i++) {
    auto *t = infer(node->values[i], env, level);
    unifier_.unify(elem_type, t, node->Range,
                   "in sequence literal (all elements must have same type)");
  }
  auto *Result = arena_.make_app("Seq", {unifier_.resolve(elem_type)});
  for (auto *Value : node->values)
    merge_callable_contracts(node, Value, Result->args.front());
  return Result;
}

// ===== Do Block =====

MonoTypePtr TypeChecker::infer_do(DoExpr *node, std::shared_ptr<TypeEnv> env,
                                  int level) {
  MonoTypePtr last_type = arena_.make_con(TyCon::Unit);
  for (auto *step : node->steps)
    last_type = infer(step, env, level);
  return last_type;
}

// ===== Generalization =====

void TypeChecker::collect_free_vars(MonoTypePtr type, int level,
                                    std::vector<TypeId> &vars) {
  type = unifier_.resolve(type);
  if (!type)
    return;
  if (type->tag == MonoType::Var) {
    if (uf_.level(type->var_id) > level) {
      // Check not already collected
      for (auto id : vars)
        if (id == type->var_id)
          return;
      vars.push_back(type->var_id);
    }
    return;
  }
  if (type->tag == MonoType::Arrow) {
    collect_free_vars(type->param_type, level, vars);
    collect_free_vars(type->return_type, level, vars);
  }
  if (type->tag == MonoType::App)
    for (auto *a : type->args)
      collect_free_vars(a, level, vars);
  if (type->tag == MonoType::MTuple)
    for (auto *e : type->elements)
      collect_free_vars(e, level, vars);
  if (type->tag == MonoType::MRecord) {
    for (auto &[_, ft] : type->record_fields)
      collect_free_vars(ft, level, vars);
    if (type->row_rest)
      collect_free_vars(type->row_rest, level, vars);
  }
  if (type->tag == MonoType::Sum)
    for (const auto *Alternative : type->sum_alternatives)
      collect_free_vars(Alternative, level, vars);
}

TypeScheme TypeChecker::generalize(MonoTypePtr type, int level,
                                   std::vector<Constraint> Constraints) {
  type = zonk(type);
  TypeScheme scheme(type);
  scheme.constraints.reserve(Constraints.size());
  for (auto &Constraint : Constraints) {
    std::vector<MonoTypePtr> Types;
    Types.reserve(Constraint.types.size());
    for (const auto *Type : Constraint.types)
      Types.push_back(zonk(Type));
    if (Constraint.resolved_trait)
      scheme.constraints.emplace_back(*Constraint.resolved_trait,
                                      std::move(Types));
    else
      scheme.constraints.emplace_back(Constraint.trait_name, std::move(Types));
  }
  auto Closure = collectInferenceEffectClosure(
      type, scheme.constraints, arena_.effect_solver(), arena_.effect_atoms(),
      [&](MonoTypePtr Current) { return unifier_.resolve(Current); });
  if (!Closure)
    throw std::invalid_argument(Closure.error().Message);
  scheme.effect_roots = Closure->ArrowRoots;
  for (const auto *Current : Closure->Types)
    if (Current->tag == MonoType::Var && uf_.level(Current->var_id) > level &&
        std::ranges::find(scheme.quantified_vars, Current->var_id) ==
            scheme.quantified_vars.end())
      scheme.quantified_vars.push_back(Current->var_id);
  for (const auto &Row : Closure->Effects)
    for (const auto &Tail : Row.Normal.tails) {
      const auto Kind = Tail.opaque ? model::EffectVariableKind::Opaque
                                    : model::EffectVariableKind::Flexible;
      if (std::ranges::none_of(scheme.quantified_effect_variables,
                               [&](const QuantifiedEffectVariable &Variable) {
                                 return Variable.Variable == Tail.variable;
                               }))
        scheme.quantified_effect_variables.push_back({Tail.variable, Kind});
    }
  if (!Closure->Effects.empty()) {
    std::vector<EffectRef> Effects;
    Effects.reserve(Closure->Effects.size());
    for (const auto &Row : Closure->Effects)
      Effects.push_back(Row.Effect);
    scheme.effect_graph = arena_.effect_solver().freeze(std::move(Effects));
  }
  return scheme;
}

MonoTypePtr TypeChecker::substitute(
    MonoTypePtr type, const std::unordered_map<TypeId, MonoTypePtr> &subst,
    const std::vector<std::pair<EffectRef, EffectRef>> &effect_subst,
    bool ValidateEffects) {
  type = unifier_.resolve(type);
  if (!type)
    return nullptr;
  if (type->tag == MonoType::Var) {
    auto it = subst.find(type->var_id);
    if (it != subst.end())
      return it->second;
    return type;
  }
  if (type->tag == MonoType::Con)
    return type;
  if (type->tag == MonoType::Arrow) {
    auto effect = type->arrow_effect;
    for (const auto &[source, replacement] : effect_subst)
      if (effect == source) {
        effect = replacement;
        break;
      }
    auto *Parameter =
        substitute(type->param_type, subst, effect_subst, ValidateEffects);
    auto *Result =
        substitute(type->return_type, subst, effect_subst, ValidateEffects);
    if (ValidateEffects)
      return arena_.make_arrow(Parameter, Result, effect);
    return arena_.alloc(MonoType::make_arrow(Parameter, Result, effect,
                                             &arena_.effect_solver(),
                                             &arena_.effect_atoms()));
  }
  if (type->tag == MonoType::App) {
    std::vector<MonoTypePtr> new_args;
    for (auto *a : type->args)
      new_args.push_back(substitute(a, subst, effect_subst, ValidateEffects));
    if (type->type_name == "Promise" && type->promise_result_contract &&
        !new_args.empty()) {
      auto Effects = type->promise_effects;
      for (const auto &[Source, Replacement] : effect_subst)
        if (Effects == Source) {
          Effects = Replacement;
          break;
        }
      return arena_.make_promise(new_args.front(),
                                 *type->promise_result_contract, Effects);
    }
    return type->resolved_key
               ? arena_.make_app(*type->resolved_key, std::move(new_args),
                                 type->type_name)
               : arena_.make_app(type->type_name, std::move(new_args));
  }
  if (type->tag == MonoType::MTuple) {
    std::vector<MonoTypePtr> new_elems;
    for (auto *e : type->elements)
      new_elems.push_back(substitute(e, subst, effect_subst, ValidateEffects));
    return arena_.make_tuple(new_elems);
  }
  if (type->tag == MonoType::MRecord) {
    std::vector<std::pair<std::string, MonoTypePtr>> new_fields;
    for (auto &[name, ft] : type->record_fields)
      new_fields.push_back(
          {name, substitute(ft, subst, effect_subst, ValidateEffects)});
    MonoTypePtr new_rest =
        type->row_rest
            ? substitute(type->row_rest, subst, effect_subst, ValidateEffects)
            : nullptr;
    return arena_.make_record(new_fields, new_rest);
  }
  if (type->tag == MonoType::Sum) {
    std::vector<MonoTypePtr> Alternatives;
    for (const auto *Alternative : type->sum_alternatives)
      Alternatives.push_back(
          substitute(Alternative, subst, effect_subst, ValidateEffects));
    return arena_.make_sum(std::move(Alternatives));
  }
  return type;
}

std::vector<CallableTypeContract> TypeChecker::interface_callable_contracts(
    MonoTypePtr Type, const std::vector<bool> &Borrowed,
    std::size_t VisibleParameterCount, model::CallingConvention Convention,
    SourceRange Range) {
  std::vector<MonoTypePtr> Stages;
  std::vector<model::ParameterOwnership> Ownerships;
  auto *Stage = unifier_.resolve(Type);
  std::size_t Index = 0;
  while (Index < VisibleParameterCount && Stage &&
         Stage->tag == MonoType::Arrow) {
    Stages.push_back(Stage);
    const auto *Parameter = unifier_.resolve(Stage->param_type);
    Ownerships.push_back(Parameter && Parameter->tag == MonoType::Con &&
                                 Parameter->con != TyCon::String
                             ? model::ParameterOwnership::Trivial
                         : Index < Borrowed.size() && Borrowed[Index]
                             ? model::ParameterOwnership::Borrow
                             : model::ParameterOwnership::Consume);
    Stage = unifier_.resolve(Stage->return_type);
    ++Index;
  }
  if (Index != VisibleParameterCount)
    throw std::invalid_argument(
        "declared function arity exceeds its Arrow signature");
  const auto ResultContract =
      Stage && Stage->tag == MonoType::Con && Stage->con != TyCon::String
          ? model::ResultOwnership::Trivial
          : model::ResultOwnership::Owned;
  std::vector<CallableTypeContract> Result;
  Result.reserve(Stages.size());
  for (std::size_t ContractIndex = 0; ContractIndex < Stages.size();
       ++ContractIndex)
    Result.push_back(CallableTypeContract{
        Stages[ContractIndex],
        {Ownerships.begin() + static_cast<std::ptrdiff_t>(ContractIndex),
         Ownerships.end()},
        ResultContract,
        ContractIndex == 0 ? Convention
                           : model::CallingConvention::ClosureEntry,
        Range});
  return Result;
}

ImportedFunctionDeclarationProjection
TypeChecker::imported_function_declaration(const TypeScheme &Scheme,
                                           const std::vector<bool> &Borrowed,
                                           std::size_t VisibleParameterCount,
                                           model::CallingConvention Convention,
                                           SourceRange Range) {
  auto Contracts = declaration_callable_contracts(
      std::span<MonoTypePtr const>(&Scheme.body, 1), Convention, Range);
  auto InterfaceRootContracts = interface_callable_contracts(
      Scheme.body, Borrowed, VisibleParameterCount, Convention, Range);
  for (auto &InterfaceContract : InterfaceRootContracts) {
    const auto Existing = std::ranges::find(Contracts, InterfaceContract.Arrow,
                                            &CallableTypeContract::Arrow);
    if (Existing == Contracts.end())
      Contracts.push_back(std::move(InterfaceContract));
    else
      *Existing = std::move(InterfaceContract);
  }
  const auto Root = std::ranges::find_if(Contracts, [&](const auto &Contract) {
    return Contract.Arrow == Scheme.body;
  });
  const auto *Resolved = unifier_.resolve(Scheme.body);
  const auto ResultContract = Root != Contracts.end() ? Root->ResultContract
                              : Resolved && Resolved->tag == MonoType::Con &&
                                      Resolved->con != TyCon::String
                                  ? model::ResultOwnership::Trivial
                                  : model::ResultOwnership::Owned;
  return ImportedFunctionDeclarationProjection{
      Scheme,
      Root == Contracts.end() ? std::vector<model::ParameterOwnership>{}
                              : Root->ParameterOwnerships,
      ResultContract, Convention, std::move(Contracts)};
}

std::vector<CallableTypeContract>
TypeChecker::declaration_callable_contracts(std::span<MonoTypePtr const> Roots,
                                            model::CallingConvention Convention,
                                            SourceRange Range) {
  std::vector<CallableTypeContract> Result;
  std::unordered_set<MonoTypePtr> Seen;
  const auto Visit = [&](const auto &Self, MonoTypePtr Type,
                         bool DeclarationRoot) -> void {
    Type = unifier_.resolve(Type);
    if (!Type || !Seen.insert(Type).second)
      return;
    if (Type->tag == MonoType::Arrow) {
      std::size_t Arity = 0;
      for (auto *Stage = Type; Stage && Stage->tag == MonoType::Arrow;
           Stage = unifier_.resolve(Stage->return_type))
        ++Arity;
      auto Contracts = interface_callable_contracts(
          Type, {}, Arity,
          DeclarationRoot ? Convention : model::CallingConvention::ClosureEntry,
          Range);
      for (auto &Contract : Contracts)
        if (std::ranges::none_of(Result, [&](const auto &Existing) {
              return Existing.Arrow == Contract.Arrow;
            }))
          Result.push_back(std::move(Contract));
      Self(Self, Type->param_type, false);
      Self(Self, Type->return_type, false);
      return;
    }
    if (Type->tag == MonoType::App) {
      for (auto *Argument : Type->args)
        Self(Self, Argument, false);
    } else if (Type->tag == MonoType::MTuple) {
      for (auto *Element : Type->elements)
        Self(Self, Element, false);
    } else if (Type->tag == MonoType::MRecord) {
      for (const auto &[_, Field] : Type->record_fields)
        Self(Self, Field, false);
      Self(Self, Type->row_rest, false);
    } else if (Type->tag == MonoType::Sum) {
      for (auto *Alternative : Type->sum_alternatives)
        Self(Self, Alternative, false);
    }
  };
  for (auto *Root : Roots)
    Visit(Visit, Root, true);
  return Result;
}

std::vector<CallableTypeContract> TypeChecker::remap_callable_contracts(
    MonoTypePtr Source, MonoTypePtr Destination,
    std::span<const CallableTypeContract> Contracts) {
  std::unordered_map<MonoTypePtr, MonoTypePtr> Mapping;
  const auto Visit = [&](const auto &Self, MonoTypePtr Before,
                         MonoTypePtr After) -> void {
    Before = unifier_.resolve(Before);
    After = unifier_.resolve(After);
    if (!Before || !After || Before->tag != After->tag ||
        Mapping.contains(Before))
      return;
    Mapping.emplace(Before, After);
    switch (Before->tag) {
    case MonoType::Arrow:
      Self(Self, Before->param_type, After->param_type);
      Self(Self, Before->return_type, After->return_type);
      break;
    case MonoType::App:
      for (std::size_t Index = 0;
           Index < Before->args.size() && Index < After->args.size(); ++Index)
        Self(Self, Before->args[Index], After->args[Index]);
      break;
    case MonoType::MTuple:
      for (std::size_t Index = 0;
           Index < Before->elements.size() && Index < After->elements.size();
           ++Index)
        Self(Self, Before->elements[Index], After->elements[Index]);
      break;
    case MonoType::MRecord:
      for (std::size_t Index = 0; Index < Before->record_fields.size() &&
                                  Index < After->record_fields.size();
           ++Index)
        Self(Self, Before->record_fields[Index].second,
             After->record_fields[Index].second);
      Self(Self, Before->row_rest, After->row_rest);
      break;
    case MonoType::Sum:
      for (std::size_t Index = 0; Index < Before->sum_alternatives.size() &&
                                  Index < After->sum_alternatives.size();
           ++Index)
        Self(Self, Before->sum_alternatives[Index],
             After->sum_alternatives[Index]);
      break;
    case MonoType::Var:
    case MonoType::Con:
      break;
    }
  };
  Visit(Visit, Source, Destination);

  std::vector<CallableTypeContract> Result;
  Result.reserve(Contracts.size());
  for (const auto &Contract : Contracts) {
    const auto Found = Mapping.find(unifier_.resolve(Contract.Arrow));
    if (Found == Mapping.end() || !Found->second ||
        Found->second->tag != MonoType::Arrow)
      continue;
    auto Copy = Contract;
    Copy.Arrow = Found->second;
    Result.push_back(std::move(Copy));
  }
  return Result;
}

void TypeChecker::propagate_callable_contracts(ast::AstNode *Destination,
                                               const ast::AstNode *Source,
                                               MonoTypePtr DestinationType) {
  const auto Contracts = expression_callable_contracts_.find(Source);
  const auto SourceType = type_map_.find(const_cast<ast::AstNode *>(Source));
  if (Contracts == expression_callable_contracts_.end() ||
      SourceType == type_map_.end()) {
    expression_callable_contracts_.erase(Destination);
    return;
  }
  auto Remapped = remap_callable_contracts(SourceType->second, DestinationType,
                                           Contracts->second);
  if (Remapped.empty())
    expression_callable_contracts_.erase(Destination);
  else
    expression_callable_contracts_.insert_or_assign(Destination,
                                                    std::move(Remapped));
}

void TypeChecker::merge_callable_contracts(ast::AstNode *Destination,
                                           const ast::AstNode *Source,
                                           MonoTypePtr DestinationType) {
  const auto Contracts = expression_callable_contracts_.find(Source);
  const auto SourceType = type_map_.find(const_cast<ast::AstNode *>(Source));
  if (Contracts == expression_callable_contracts_.end() ||
      SourceType == type_map_.end())
    return;
  auto Remapped = remap_callable_contracts(SourceType->second, DestinationType,
                                           Contracts->second);
  auto &DestinationContracts = expression_callable_contracts_[Destination];
  for (auto &Contract : Remapped) {
    const auto Duplicate =
        std::ranges::find_if(DestinationContracts, [&](const auto &Existing) {
          return Existing.Arrow == Contract.Arrow &&
                 Existing.ParameterOwnerships == Contract.ParameterOwnerships &&
                 Existing.ResultContract == Contract.ResultContract &&
                 Existing.Convention == Contract.Convention;
        });
    if (Duplicate == DestinationContracts.end())
      DestinationContracts.push_back(std::move(Contract));
  }
  if (DestinationContracts.empty())
    expression_callable_contracts_.erase(Destination);
}

void TypeChecker::refine_callable_contract_ownership(
    std::span<CallableTypeContract> Contracts) {
  const auto IsTrivial = [&](MonoTypePtr Type) {
    Type = unifier_.resolve(Type);
    return Type && Type->tag == MonoType::Con && Type->con != TyCon::String;
  };
  for (auto &Contract : Contracts) {
    auto *Stage = unifier_.resolve(Contract.Arrow);
    for (auto &Ownership : Contract.ParameterOwnerships) {
      if (!Stage || Stage->tag != MonoType::Arrow)
        break;
      if (IsTrivial(Stage->param_type))
        Ownership = model::ParameterOwnership::Trivial;
      Stage = unifier_.resolve(Stage->return_type);
    }
    Contract.ResultContract = IsTrivial(Stage) ? model::ResultOwnership::Trivial
                                               : model::ResultOwnership::Owned;
  }
}

std::vector<Constraint>
TypeChecker::constraints_for_function(const ast::FunctionExpr *Function) {
  std::vector<Constraint> Result;
  const auto ContainsVariable = [&](const auto &Self,
                                    MonoTypePtr Type) -> bool {
    Type = unifier_.resolve(Type);
    if (!Type)
      return false;
    if (Type->tag == MonoType::Var)
      return true;
    if (Type->tag == MonoType::Arrow)
      return Self(Self, Type->param_type) || Self(Self, Type->return_type);
    if (Type->tag == MonoType::App)
      return std::ranges::any_of(
          Type->args, [&](MonoTypePtr Value) { return Self(Self, Value); });
    if (Type->tag == MonoType::MTuple)
      return std::ranges::any_of(
          Type->elements, [&](MonoTypePtr Value) { return Self(Self, Value); });
    if (Type->tag == MonoType::MRecord) {
      if (Type->row_rest && Self(Self, Type->row_rest))
        return true;
      return std::ranges::any_of(Type->record_fields, [&](const auto &Field) {
        return Self(Self, Field.second);
      });
    }
    if (Type->tag == MonoType::Sum)
      return std::ranges::any_of(
          Type->sum_alternatives,
          [&](MonoTypePtr Value) { return Self(Self, Value); });
    return false;
  };
  for (const auto &Deferred : deferred_constraints_) {
    if (Deferred.owning_function != Function || Deferred.types.empty())
      continue;
    if (!std::ranges::any_of(Deferred.types, [&](MonoTypePtr Type) {
          return ContainsVariable(ContainsVariable, Type);
        }))
      continue;
    const auto Trait = Deferred.resolved_trait.value_or(
        resolved_trait_key(Deferred.trait_name));
    const bool Duplicate = std::ranges::any_of(Result, [&](const auto &Value) {
      return Value.resolved_trait == Trait && Value.types == Deferred.types;
    });
    if (!Duplicate)
      Result.emplace_back(Trait, Deferred.types);
  }
  return Result;
}

MonoTypePtr
TypeChecker::instantiate_binding(const TypeScheme &scheme, int level,
                                 std::vector<MonoTypePtr> *TypeArguments,
                                 std::vector<EffectRef> *EffectArguments) {
  if (scheme.quantified_vars.empty() && scheme.constraints.empty() &&
      scheme.quantified_effect_variables.empty() && !scheme.effect_graph &&
      scheme.effect_roots.empty()) {
    if (TypeArguments)
      TypeArguments->clear();
    if (EffectArguments)
      EffectArguments->clear();
    return scheme.body;
  }
  return instantiate(scheme, level, TypeArguments, EffectArguments);
}

MonoTypePtr TypeChecker::instantiate(const TypeScheme &scheme, int level,
                                     std::vector<MonoTypePtr> *TypeArguments,
                                     std::vector<EffectRef> *EffectArguments) {
  if (TypeArguments)
    TypeArguments->clear();
  if (EffectArguments)
    EffectArguments->clear();

  auto Closure = collectInferenceEffectClosure(
      scheme.body, scheme.constraints, arena_.effect_solver(),
      arena_.effect_atoms(),
      [&](MonoTypePtr Current) { return unifier_.resolve(Current); });
  if (!Closure)
    throw std::logic_error(Closure.error().Message);

  if (scheme.effect_graph) {
    if (Closure->ArrowRoots != scheme.effect_roots || Closure->Effects.empty())
      throw std::logic_error("malformed scheme effect root closure");
  } else if (!scheme.effect_roots.empty()) {
    throw std::logic_error("scheme effect roots require a frozen graph");
  } else if (std::ranges::any_of(Closure->Effects, [&](const auto &Row) {
               return !Row.Normal.empty();
             })) {
    throw std::logic_error(
        "polymorphic effectful scheme requires a frozen graph");
  }

  if (scheme.quantified_vars.empty() && scheme.constraints.empty() &&
      scheme.quantified_effect_variables.empty() && !scheme.effect_graph)
    return scheme.body;

  std::unordered_map<TypeId, MonoTypePtr> subst;
  std::unordered_set<TypeId> DeclaredTypes;
  for (auto id : scheme.quantified_vars) {
    if (!DeclaredTypes.insert(id).second)
      throw std::logic_error("duplicate quantified type variable");
    auto *fresh = arena_.fresh_var(level);
    uf_.add_var(fresh->var_id, level);
    subst[id] = fresh;
    if (TypeArguments)
      TypeArguments->push_back(fresh);
  }

  std::vector<std::pair<EffectRef, EffectRef>> effect_subst;
  std::unordered_map<EffectVarId, EffectRef> InstantiatedEffectVariables;
  std::vector<std::pair<EffectAtomId, SourceRange>> InstantiatedOrigins;
  if (scheme.effect_graph) {
    auto CatalogTransaction = arena_.effect_atoms().beginAppend();
    auto SolverTransaction = arena_.effect_solver().beginInstantiation(
        *scheme.effect_graph,
        [&](EffectAtomId Atom, const EffectReferenceMap &References)
            -> std::expected<EffectAtomId, std::string> {
          const auto &Source = arena_.effect_atoms().at(Atom);
          if (const auto *Operation =
                  std::get_if<OperationEffectAtom>(&Source)) {
            OperationEffectAtom Clone = *Operation;
            std::vector<std::pair<EffectRef, EffectRef>> ReferencePairs;
            ReferencePairs.reserve(References.size());
            for (const auto &[Original, Fresh] : References)
              ReferencePairs.emplace_back(Original, Fresh);
            for (auto *&Argument : Clone.TypeArguments)
              Argument = substitute(Argument, subst, ReferencePairs, false);
            for (auto &Argument : Clone.EffectArguments) {
              const auto Found = References.find(Argument);
              if (Found == References.end())
                return std::unexpected(
                    "operation effect argument is outside scheme closure");
              Argument = Found->second;
            }
            const auto Instantiated =
                CatalogTransaction.intern(std::move(Clone));
            if (const auto Origin = effect_origins_.find(Atom);
                Origin != effect_origins_.end())
              InstantiatedOrigins.emplace_back(Instantiated, Origin->second);
            return Instantiated;
          }
          return CatalogTransaction.intern(Source);
        });
    if (!SolverTransaction)
      throw std::logic_error(SolverTransaction.error());
    if (auto Prepared = CatalogTransaction.prepareCommit(); !Prepared)
      throw std::logic_error(Prepared.error());
    if (auto Prepared = SolverTransaction->prepareCommit(
            [&](EffectAtomId Atom) { return CatalogTransaction.valid(Atom); },
            [&](EffectAtomId Atom) {
              return CatalogTransaction.validForMask(Atom);
            });
        !Prepared)
      throw std::logic_error(Prepared.error());
    CatalogTransaction.commitPrepared();
    SolverTransaction->commitPrepared();
    for (const auto &[Atom, Origin] : InstantiatedOrigins)
      effect_origins_.try_emplace(Atom, Origin);
    const auto &Graph = SolverTransaction->result();
    effect_subst.reserve(Graph.references.size());
    for (const auto &[Original, Fresh] : Graph.references)
      effect_subst.emplace_back(Original, Fresh);
    InstantiatedEffectVariables = Graph.variables;
    for (const auto &Root : scheme.effect_roots)
      if (!Graph.references.contains(Root.Effect))
        throw std::logic_error("malformed scheme effect root closure");
  }

  std::unordered_set<EffectVarId> DeclaredEffects;
  for (const auto &Variable : scheme.quantified_effect_variables) {
    if (!DeclaredEffects.insert(Variable.Variable).second)
      throw std::logic_error("duplicate quantified effect variable");
    auto Found = InstantiatedEffectVariables.find(Variable.Variable);
    EffectRef Fresh;
    if (Found != InstantiatedEffectVariables.end()) {
      Fresh = Found->second;
    } else {
      Fresh = Variable.Kind == model::EffectVariableKind::Opaque
                  ? arena_.effect_solver().opaque()
                  : arena_.effect_solver().flexible();
    }
    if (EffectArguments)
      EffectArguments->push_back(Fresh);
  }

  // Record deferred constraints (substituted)
  for (auto &c : scheme.constraints) {
    std::vector<MonoTypePtr> arguments;
    for (auto *type : c.types)
      arguments.push_back(substitute(type, subst, effect_subst));
    if (c.resolved_trait)
      deferred_constraints_.emplace_back(
          *c.resolved_trait, std::move(arguments), SourceRange{}, "");
    else
      deferred_constraints_.emplace_back(c.trait_name, std::move(arguments),
                                         SourceRange{}, "");
  }

  return substitute(scheme.body, subst, effect_subst);
}

// ===== Case Expression =====

MonoTypePtr TypeChecker::infer_case(CaseExpr *node,
                                    std::shared_ptr<TypeEnv> env, int level) {
  auto *scrut_type = infer(node->expr, env, level);

  MonoTypePtr result_type = nullptr;
  const ast::AstNode *ContractSource = nullptr;
  bool CallableContractsAgree = true;
  bool SawClause = false;

  for (auto *clause : node->clauses) {
    auto clause_env = env->child();

    // Infer pattern and bind variables
    auto *pat_type = infer_pattern(clause->pattern, clause_env, level);
    unifier_.unify(scrut_type, pat_type, clause->Range, "in case pattern");

    // Infer body
    auto *body_type = infer(clause->body, clause_env, level);
    const auto BodyContracts =
        expression_callable_contracts_.find(clause->body);
    if (!SawClause) {
      ContractSource = BodyContracts == expression_callable_contracts_.end()
                           ? nullptr
                           : clause->body;
    } else if (!ContractSource ||
               BodyContracts == expression_callable_contracts_.end() ||
               !same_callable_contracts(
                   expression_callable_contracts_.at(ContractSource),
                   BodyContracts->second)) {
      CallableContractsAgree = false;
    }
    SawClause = true;

    if (!result_type)
      result_type = body_type;
    else
      unifier_.unify(result_type, body_type, clause->Range,
                     "in case branches (all must have same type)");
  }

  auto *Result = result_type ? unifier_.resolve(result_type)
                             : arena_.make_con(TyCon::Unit);
  if (CallableContractsAgree && ContractSource)
    propagate_callable_contracts(node, ContractSource, Result);
  return Result;
}

// ===== Pattern Inference =====

MonoTypePtr TypeChecker::infer_pattern(PatternNode *pat,
                                       std::shared_ptr<TypeEnv> env,
                                       int level) {
  if (!pat)
    return arena_.fresh_var(level);

  switch (pat->get_type()) {
  case AST_UNDERSCORE_PATTERN: {
    // Wildcard: matches anything
    auto *v = arena_.fresh_var(level);
    uf_.add_var(v->var_id, level);
    return v;
  }

  case AST_PATTERN_VALUE: {
    auto *pv = static_cast<PatternValue *>(pat);
    // Identifier binding: fresh var, bind in env
    if (auto *id = std::get_if<IdentifierExpr *>(&pv->expr)) {
      auto *v = arena_.fresh_var(level);
      uf_.add_var(v->var_id, level);
      env->bind((*id)->name->value, v);
      return v;
    }
    // Symbol literal
    if (std::get_if<SymbolExpr *>(&pv->expr))
      return arena_.make_con(TyCon::Symbol);
    // Integer literal
    if (auto *lit = std::get_if<LiteralExpr<void *> *>(&pv->expr)) {
      auto *an = static_cast<AstNode *>(*lit);
      if (an->get_type() == AST_INTEGER_EXPR)
        return arena_.make_con(TyCon::Int);
      if (an->get_type() == AST_FLOAT_EXPR)
        return arena_.make_con(TyCon::Float);
      if (an->get_type() == AST_STRING_EXPR)
        return arena_.make_con(TyCon::String);
    }
    // Fallback
    auto *v = arena_.fresh_var(level);
    uf_.add_var(v->var_id, level);
    return v;
  }

  case AST_HEAD_TAILS_PATTERN: {
    auto *htp = static_cast<HeadTailsPattern *>(pat);
    auto *elem_type = arena_.fresh_var(level);
    uf_.add_var(elem_type->var_id, level);

    // Head patterns: each must match elem_type
    for (auto *head_pat : htp->heads) {
      auto *head_type = infer_pattern(head_pat, env, level);
      unifier_.unify(elem_type, head_type, pat->Range, "in head-tail pattern");
    }

    // Tail: must be Seq(elem_type)
    if (htp->tail) {
      auto *tail_type = infer_pattern(htp->tail, env, level);
      auto *seq_type = arena_.make_app("Seq", {elem_type});
      unifier_.unify(tail_type, seq_type, pat->Range, "in tail pattern");
    }

    return arena_.make_app("Seq", {unifier_.resolve(elem_type)});
  }

  case AST_SEQ_PATTERN: {
    auto *sp = static_cast<SeqPattern *>(pat);
    auto *elem_type = arena_.fresh_var(level);
    uf_.add_var(elem_type->var_id, level);
    for (auto *sub : sp->patterns) {
      auto *sub_type = infer_pattern(sub, env, level);
      unifier_.unify(elem_type, sub_type, pat->Range, "in sequence pattern");
    }
    return arena_.make_app("Seq", {unifier_.resolve(elem_type)});
  }

  case AST_TUPLE_PATTERN: {
    auto *tp = static_cast<TuplePattern *>(pat);
    if (tp->patterns.empty())
      return arena_.make_con(TyCon::Unit);
    std::vector<MonoTypePtr> elem_types;
    for (auto *sub : tp->patterns)
      elem_types.push_back(infer_pattern(sub, env, level));
    return arena_.make_tuple(elem_types);
  }

  case AST_CONSTRUCTOR_PATTERN: {
    auto *cp = static_cast<ConstructorPattern *>(pat);
    auto ctor_it = constructor_registry_.find(cp->constructor_name);
    if (ctor_it != constructor_registry_.end()) {
      auto &info = ctor_it->second;
      // Instantiate ADT type params with fresh vars
      std::vector<MonoTypePtr> type_arg_vars;
      for (size_t i = 0; i < info.type_params.size(); i++) {
        auto *v = arena_.fresh_var(level);
        uf_.add_var(v->var_id, level);
        type_arg_vars.push_back(v);
      }
      auto field_type =
          [&](const ast::FieldType &field,
              const std::unordered_map<std::string, MonoTypePtr> &params,
              const auto &self) -> MonoTypePtr {
        if (field.is_tuple_type) {
          std::vector<MonoTypePtr> elements;
          for (const auto &element : field.tuple_types)
            elements.push_back(self(element, params, self));
          return arena_.make_tuple(elements);
        }
        if (field.is_function_type) {
          MonoTypePtr result =
              field.return_types.empty()
                  ? arena_.make_con(TyCon::Unit)
                  : self(field.return_types.front(), params, self);
          for (auto it = field.param_types.rbegin();
               it != field.param_types.rend(); ++it)
            result = arena_.make_arrow(self(*it, params, self), result);
          return result;
        }
        if (auto it = params.find(field.name); it != params.end())
          return it->second;
        if (field.name == "Int")
          return arena_.make_con(TyCon::Int);
        if (field.name == "Float")
          return arena_.make_con(TyCon::Float);
        if (field.name == "Bool")
          return arena_.make_con(TyCon::Bool);
        if (field.name == "String")
          return arena_.make_con(TyCon::String);
        if (field.name == "Symbol")
          return arena_.make_con(TyCon::Symbol);
        if (field.name == "()" || field.name == "Unit")
          return arena_.make_con(TyCon::Unit);
        std::vector<MonoTypePtr> arguments;
        for (const auto &argument : field.type_arguments)
          arguments.push_back(self(argument, params, self));
        if (const auto Key = resolved_nominal_key(field.name))
          return arena_.make_app(*Key, std::move(arguments), field.name);
        return arena_.make_app(field.name, std::move(arguments));
      };
      std::unordered_map<std::string, MonoTypePtr> params;
      for (size_t i = 0;
           i < info.type_params.size() && i < type_arg_vars.size(); ++i)
        params[info.type_params[i]] = type_arg_vars[i];

      // Constructor fields and tuple elements are distinct levels
      // of structure. `Box (Int, Int)` declares one tuple field,
      // so its pattern is `Box ((a, b))`, not `Box (a, b)`.
      // Infer malformed sub-patterns to keep their bindings usable
      // while reporting the structural error only once.
      if (cp->sub_patterns.size() != info.field_types.size()) {
        for (auto *sub_pattern : cp->sub_patterns)
          infer_pattern(sub_pattern, env, level);

        const auto actual_count = cp->sub_patterns.size();
        const auto declared_count = info.field_types.size();
        std::string message = "constructor pattern '" + cp->constructor_name +
                              "' has " + std::to_string(actual_count) +
                              (actual_count == 1 ? " field" : " fields") +
                              ", but constructor declares " +
                              std::to_string(declared_count) +
                              (declared_count == 1 ? " field" : " fields");
        if (declared_count == 1)
          message += " of type " + info.field_types[0].to_string();
        diag_.error(pat->Range, ErrorCode::E0100, message);

        if (declared_count == 1 && info.field_types[0].is_tuple_type) {
          diag_.note(pat->Range,
                     "match the tuple field with " + cp->constructor_name +
                         " ((first, second)); " + cp->constructor_name +
                         " (first, second) supplies two constructor fields");
        }
        return arena_.make_app(info.adt_key, type_arg_vars, info.adt_name);
      }

      // Bind every sub-pattern against its canonical declared field type.
      for (size_t i = 0; i < cp->sub_patterns.size(); i++) {
        auto *sub_type = infer_pattern(cp->sub_patterns[i], env, level);
        auto *expected = field_type(info.field_types[i], params, field_type);
        if (!unifier_.unify(sub_type, expected, pat->Range,
                            "in field " + std::to_string(i + 1) +
                                " of constructor pattern '" +
                                cp->constructor_name + "'")) {
          const auto &declared = info.field_types[i];
          diag_.note(pat->Range,
                     "'" + cp->constructor_name + "' declares field " +
                         std::to_string(i + 1) + " as " + declared.to_string());
        }
      }
      return arena_.make_app(info.adt_key, type_arg_vars, info.adt_name);
    }
    // Unknown constructor â€” fresh var
    auto *v = arena_.fresh_var(level);
    uf_.add_var(v->var_id, level);
    for (auto *sub : cp->sub_patterns)
      infer_pattern(sub, env, level);
    return v;
  }

  case AST_TYPED_PATTERN: {
    auto *tp = static_cast<TypedPattern *>(pat);
    // Map type name to MonoType
    MonoTypePtr bound_type;
    if (tp->type_name == "Int")
      bound_type = arena_.make_con(TyCon::Int);
    else if (tp->type_name == "Float")
      bound_type = arena_.make_con(TyCon::Float);
    else if (tp->type_name == "Bool")
      bound_type = arena_.make_con(TyCon::Bool);
    else if (tp->type_name == "String")
      bound_type = arena_.make_con(TyCon::String);
    else if (tp->type_name == "Symbol")
      bound_type = arena_.make_con(TyCon::Symbol);
    else if (tp->type_name == "ByteArray")
      bound_type = arena_.make_con(TyCon::ByteArray);
    else {
      // Unknown or ADT type â€” use a named App type
      if (const auto Key = resolved_nominal_key(tp->type_name))
        bound_type = arena_.make_app(*Key, {}, tp->type_name);
      else
        bound_type = arena_.make_app(tp->type_name, {});
    }
    env->bind(tp->binding_name, bound_type);
    // The pattern matches a sum type containing this alternative
    // Return the scrutinee type (sum) rather than the inner type
    auto *v = arena_.fresh_var(level);
    uf_.add_var(v->var_id, level);
    return v;
  }

  case AST_OR_PATTERN: {
    auto *op = static_cast<OrPattern *>(pat);
    MonoTypePtr or_type = nullptr;
    for (auto &alt : op->patterns) {
      auto *alt_type = infer_pattern(alt.get(), env, level);
      if (!or_type)
        or_type = alt_type;
      else
        unifier_.unify(or_type, alt_type, pat->Range, "in or-pattern");
    }
    return or_type ? or_type : arena_.fresh_var(level);
  }

  case AST_DICT_PATTERN: {
    auto *dp = static_cast<DictPattern *>(pat);
    auto *key_var = arena_.fresh_var(level);
    uf_.add_var(key_var->var_id, level);
    auto *val_var = arena_.fresh_var(level);
    uf_.add_var(val_var->var_id, level);
    for (auto &[key_pat, val_pat] : dp->keyValuePairs) {
      auto *kt = infer_pattern(key_pat, env, level);
      auto *vt = infer_pattern(val_pat, env, level);
      unifier_.unify(key_var, kt, pat->Range, "in dict pattern key");
      unifier_.unify(val_var, vt, pat->Range, "in dict pattern value");
    }
    return arena_.make_app(
        "Dict", {unifier_.resolve(key_var), unifier_.resolve(val_var)});
  }

  case AST_RECORD_PATTERN: {
    auto *rp = static_cast<RecordPattern *>(pat);
    for (auto &[name_expr, sub_pat] : rp->items) {
      auto *sub_type = infer_pattern(sub_pat, env, level);
      if (name_expr && sub_pat->get_type() == AST_PATTERN_VALUE) {
        auto *pv = static_cast<PatternValue *>(sub_pat);
        if (auto *id = std::get_if<IdentifierExpr *>(&pv->expr))
          env->bind((*id)->name->value, sub_type);
      }
    }
    auto *v = arena_.fresh_var(level);
    uf_.add_var(v->var_id, level);
    return v;
  }

  case AST_AS_DATA_STRUCTURE_PATTERN: {
    auto *asp = static_cast<AsDataStructurePattern *>(pat);
    auto *inner_type =
        infer_pattern(static_cast<PatternNode *>(asp->pattern), env, level);
    if (asp->identifier)
      env->bind(asp->identifier->name->value, inner_type);
    return inner_type;
  }

  default: {
    auto *v = arena_.fresh_var(level);
    uf_.add_var(v->var_id, level);
    return v;
  }
  }
}

// ===== Collection Extractor Binding =====

void TypeChecker::bind_collection_extractor(CollectionExtractorExpr *ce,
                                            std::shared_ptr<TypeEnv> env,
                                            int level) {
  if (!ce)
    return;

  if (auto *vce = dynamic_cast<ValueCollectionExtractorExpr *>(ce)) {
    // Infer collection type to get element type
    auto *elem_type = arena_.fresh_var(level);
    uf_.add_var(elem_type->var_id, level);
    if (vce->collection) {
      auto *col_type = infer(vce->collection, env, level);
      auto *resolved = unifier_.resolve(col_type);
      if (resolved && resolved->tag == MonoType::App &&
          resolved->type_name == "Iterator" && !resolved->args.empty()) {
        unifier_.unify(resolved->args.front(), elem_type, ce->Range,
                       "in iterator generator");
      } else {
        auto *expected = arena_.make_app("Seq", {elem_type});
        unifier_.unify(col_type, expected, ce->Range,
                       "in generator collection");
      }
    }
    // Bind the iteration variable
    if (auto *id = std::get_if<IdentifierExpr *>(&vce->expr))
      env->bind((*id)->name->value, elem_type);
    // Infer guard condition if present
    if (vce->condition)
      infer(vce->condition, env, level);
  } else if (auto *kvce = dynamic_cast<KeyValueCollectionExtractorExpr *>(ce)) {
    auto *key_type = arena_.fresh_var(level);
    uf_.add_var(key_type->var_id, level);
    auto *val_type = arena_.fresh_var(level);
    uf_.add_var(val_type->var_id, level);
    if (kvce->collection) {
      auto *col_type = infer(kvce->collection, env, level);
      auto *expected = arena_.make_app("Dict", {key_type, val_type});
      unifier_.unify(col_type, expected, ce->Range,
                     "in dict generator collection");
    }
    if (auto *id = std::get_if<IdentifierExpr *>(&kvce->keyExpr))
      env->bind((*id)->name->value, key_type);
    if (auto *id = std::get_if<IdentifierExpr *>(&kvce->valueExpr))
      env->bind((*id)->name->value, val_type);
    if (kvce->condition)
      infer(kvce->condition, env, level);
  }
}

// ===== Cons =====

MonoTypePtr TypeChecker::infer_cons(ConsLeftExpr *node,
                                    std::shared_ptr<TypeEnv> env, int level) {
  auto *elem_type = infer(node->left, env, level);
  auto *seq_type = infer(node->right, env, level);
  auto *expected_seq = arena_.make_app("Seq", {elem_type});
  unifier_.unify(seq_type, expected_seq, node->Range, "in cons (::) operator");
  return unifier_.resolve(expected_seq);
}

// ===== ADT Registration =====

void TypeChecker::register_adt(
    const std::string &type_name, const std::vector<std::string> &type_params,
    const std::vector<std::pair<std::string, int>> &constructors,
    const std::vector<std::vector<ast::FieldType>> &field_types,
    const std::vector<std::vector<std::string>> &field_names,
    const model::ModuleIdentity *declaration_module) {
  if (field_types.size() != constructors.size())
    throw std::invalid_argument(
        "ADT registration requires field types for every constructor");
  if (!field_names.empty() && field_names.size() != constructors.size())
    throw std::invalid_argument(
        "ADT registration field-name groups must match constructors");

  adt_type_params_[type_name] = type_params;
  const model::NominalTypeKey AdtKey{
      declaration_module ? *declaration_module : current_module_, type_name};
  nominal_keys_.insert_or_assign(type_name, AdtKey);
  for (size_t constructor_index = 0; constructor_index < constructors.size();
       ++constructor_index) {
    const auto &[ctor_name, arity] = constructors[constructor_index];
    const auto &fields = field_types[constructor_index];
    if (arity < 0 || fields.size() != static_cast<size_t>(arity))
      throw std::invalid_argument(
          "ADT constructor arity must match its canonical field types");
    static const std::vector<std::string> EmptyNames;
    const auto &names =
        field_names.empty() ? EmptyNames : field_names[constructor_index];
    if (!names.empty() && names.size() != fields.size())
      throw std::invalid_argument(
          "named ADT constructor fields must match constructor arity");
    constructor_registry_.insert_or_assign(
        ctor_name,
        ConstructorInfo{type_name, AdtKey, arity, type_params, fields, names});

    // Register constructor as a function in root env:
    // For arity 0: constructor is a value of type ADT
    // For arity N: constructor is a function a1 -> ... -> aN -> ADT(params...)
    std::vector<MonoTypePtr> param_vars;
    for (auto &tp : type_params) {
      auto *v = arena_.fresh_var(0);
      uf_.add_var(v->var_id, 0);
      param_vars.push_back(v);
    }

    MonoTypePtr result_type = arena_.make_app(AdtKey, param_vars, type_name);

    auto field_type = [&](const ast::FieldType &field,
                          const auto &self) -> MonoTypePtr {
      if (field.is_tuple_type) {
        std::vector<MonoTypePtr> elements;
        for (const auto &element : field.tuple_types)
          elements.push_back(self(element, self));
        return arena_.make_tuple(elements);
      }
      if (field.is_function_type) {
        MonoTypePtr result = field.return_types.empty()
                                 ? arena_.make_con(TyCon::Unit)
                                 : self(field.return_types.front(), self);
        for (auto it = field.param_types.rbegin();
             it != field.param_types.rend(); ++it)
          result = arena_.make_arrow(self(*it, self), result);
        return result;
      }
      for (size_t i = 0; i < type_params.size(); ++i)
        if (field.name == type_params[i])
          return param_vars[i];
      if (field.name == "Int")
        return arena_.make_con(TyCon::Int);
      if (field.name == "Float")
        return arena_.make_con(TyCon::Float);
      if (field.name == "Bool")
        return arena_.make_con(TyCon::Bool);
      if (field.name == "String")
        return arena_.make_con(TyCon::String);
      if (field.name == "Symbol")
        return arena_.make_con(TyCon::Symbol);
      if (field.name == "()" || field.name == "Unit")
        return arena_.make_con(TyCon::Unit);
      std::vector<MonoTypePtr> arguments;
      for (const auto &argument : field.type_arguments)
        arguments.push_back(self(argument, self));
      if (const auto Key = resolved_nominal_key(field.name))
        return arena_.make_app(*Key, std::move(arguments), field.name);
      return arena_.make_app(field.name, std::move(arguments));
    };

    if (arity == 0) {
      auto Scheme = generalize(result_type, -1);
      scheme_identities_.insert_or_assign(
          Scheme.body, model::FunctionDeclarationIdentity{
                           model::SymbolIdentity{AdtKey.Module, ctor_name}});
      root_env_->bind_scheme(ctor_name, std::move(Scheme));
    } else {
      // Build curried function: a -> b -> ... -> ADT(params)
      // Each constructor arg gets a fresh var (polymorphic)
      MonoTypePtr fn_type = result_type;
      for (int i = arity - 1; i >= 0; i--) {
        MonoTypePtr arg_type;
        if (i < static_cast<int>(fields.size()))
          arg_type = field_type(fields[i], field_type);
        else if (i < (int)param_vars.size())
          arg_type = param_vars[i];
        else {
          arg_type = arena_.fresh_var(0);
          uf_.add_var(arg_type->var_id, 0);
        }
        fn_type = arena_.make_arrow(arg_type, fn_type);
      }
      auto Scheme = generalize(fn_type, -1);
      scheme_identities_.insert_or_assign(
          Scheme.body, model::FunctionDeclarationIdentity{
                           model::SymbolIdentity{AdtKey.Module, ctor_name}});
      root_env_->bind_scheme(ctor_name, std::move(Scheme));
    }
  }
}

// ===== Trait Registration =====

void TypeChecker::register_trait(
    const std::string &trait_name, std::vector<std::string> type_params,
    const model::ModuleIdentity *declaration_module) {
  trait_type_params_[trait_name] = std::move(type_params);
  trait_keys_.insert_or_assign(
      trait_name, model::NominalTypeKey{declaration_module ? *declaration_module
                                                           : current_module_,
                                        trait_name});
}

void TypeChecker::register_trait_method(const std::string &trait_name,
                                        const std::string &method_name,
                                        MonoTypePtr method_type) {
  trait_methods_[trait_name].insert(method_name);
  std::vector<TypeId> qvars;
  collect_free_vars(method_type, -1, qvars);

  std::vector<Constraint> constraints;
  // Add trait constraint on the first free var (the type param)
  if (!qvars.empty()) {
    // Find the first var that appears as a param type
    auto *resolved = unifier_.resolve(method_type);
    MonoTypePtr constrained = nullptr;
    if (resolved && resolved->tag == MonoType::Arrow)
      constrained = unifier_.resolve(resolved->param_type);
    if (!constrained) {
      auto *v = arena_.fresh_var(0);
      uf_.add_var(v->var_id, 0);
      constrained = v;
      qvars.push_back(v->var_id);
    }
    constraints.emplace_back(resolved_trait_key(trait_name), constrained);
  }

  auto Scheme = generalize(method_type, -1, std::move(constraints));
  const auto Trait = resolved_trait_key(trait_name);
  scheme_identities_.insert_or_assign(
      Scheme.body, model::FunctionDeclarationIdentity{model::SymbolIdentity{
                       Trait.Module, Trait.Name + "." + method_name}});
  root_env_->bind_scheme(method_name, std::move(Scheme));
}

void TypeChecker::register_trait_method_descriptor(
    const std::string &trait_name, const std::string &method_name,
    const std::string &descriptor) {
  ImportedFnSig signature;
  signature.return_descriptor = descriptor;
  std::unordered_map<std::string, MonoTypePtr> variables;
  auto *method_type = mono_from_import_sig(signature, 0, &variables);
  register_trait_method(trait_name, method_name, method_type);
  if (const auto parameters = trait_type_params_.find(trait_name);
      parameters != trait_type_params_.end()) {
    std::vector<MonoTypePtr> arguments;
    for (const auto &parameter : parameters->second) {
      auto found = variables.find(parameter);
      if (found == variables.end()) {
        auto *variable = arena_.fresh_var(0);
        uf_.add_var(variable->var_id, 0);
        found = variables.emplace(parameter, variable).first;
      }
      arguments.push_back(found->second);
    }
    if (auto scheme = root_env_->lookup(method_name);
        scheme && !arguments.empty()) {
      root_env_->bind_scheme(
          method_name, generalize(scheme->body, -1,
                                  {Constraint(resolved_trait_key(trait_name),
                                              std::move(arguments))}));
    }
  }
}

void TypeChecker::register_builtin_function(
    const std::string &function_name, MonoTypePtr function_type,
    std::size_t visible_parameter_count,
    const model::ModuleIdentity *source_module) {
  auto Scheme = generalize(function_type, -1);
  const model::FunctionDeclarationIdentity Identity{model::SymbolIdentity{
      source_module ? *source_module : current_module_, function_name}};
  scheme_identities_.insert_or_assign(Scheme.body, Identity);
  auto Declaration = imported_function_declaration(
      Scheme, {}, visible_parameter_count, model::CallingConvention::DirectYona,
      SourceRange::unknown());
  scheme_callable_contracts_.insert_or_assign(Scheme.body,
                                              Declaration.CallableContracts);
  if (source_module)
    registered_interface_function_declarations_.insert_or_assign(
        Identity, std::move(Declaration));
  root_env_->bind_scheme(function_name, std::move(Scheme));
}

void TypeChecker::register_interface_function(
    const interface::Function &Function,
    const model::ModuleIdentity *source_module,
    const std::unordered_set<std::string> *source_nominals) {
  register_builtin_function(
      Function.Name,
      mono_from_interface_function(Function, 0, source_module, source_nominals),
      Function.ParameterTypes.size(), source_module);
  if (const auto Scheme = root_env_->lookup(Function.Name)) {
    const auto Convention = [&] {
      switch (Function.Kind) {
      case interface::FunctionKind::Synchronous:
        return model::CallingConvention::DirectYona;
      case interface::FunctionKind::ThreadPool:
      case interface::FunctionKind::Io:
        return model::CallingConvention::AsyncAdapter;
      case interface::FunctionKind::Native:
        return model::CallingConvention::NativeExtern;
      }
      return model::CallingConvention::DirectYona;
    }();
    auto Declaration = imported_function_declaration(
        *Scheme, Function.BorrowedParameters, Function.ParameterTypes.size(),
        Convention, SourceRange::unknown());
    scheme_callable_contracts_.insert_or_assign(Scheme->body,
                                                Declaration.CallableContracts);
    if (source_module)
      registered_interface_function_declarations_.insert_or_assign(
          model::FunctionDeclarationIdentity{
              model::SymbolIdentity{*source_module, Function.Name}},
          std::move(Declaration));
  }
}

void TypeChecker::register_trait_superclass(
    const std::string &trait_name, const std::string &superclass_name) {
  auto &superclasses = trait_superclasses_[trait_name];
  if (std::find(superclasses.begin(), superclasses.end(), superclass_name) ==
      superclasses.end()) {
    superclasses.push_back(superclass_name);
    std::sort(superclasses.begin(), superclasses.end());
  }
}

void TypeChecker::register_instance(
    const std::string &trait_name, std::vector<std::string> type_names,
    std::vector<std::string> type_params,
    std::vector<std::pair<std::string, std::string>> constraints,
    const model::ModuleIdentity *declaration_module) {
  const auto Module =
      declaration_module ? *declaration_module : current_module_;
  auto &instances = trait_instances_[trait_name];
  const auto duplicate =
      std::find_if(instances.begin(), instances.end(),
                   [&](const InstanceContract &instance) {
                     return instance.module == Module &&
                            instance.type_names == type_names &&
                            instance.type_params == type_params &&
                            instance.constraints == constraints;
                   });
  if (duplicate != instances.end())
    return;
  std::vector<MonoTypePtr> StructuralParameters;
  StructuralParameters.reserve(type_params.size());
  std::unordered_map<std::string, MonoTypePtr> Parameters;
  for (const auto &Name : type_params) {
    auto *Parameter = arena_.fresh_var(0);
    uf_.add_var(Parameter->var_id, 0);
    Parameters.emplace(Name, Parameter);
    StructuralParameters.push_back(Parameter);
  }
  const auto SimpleHead = [&](std::string_view Name) -> MonoTypePtr {
    if (const auto Found = Parameters.find(std::string(Name));
        Found != Parameters.end())
      return Found->second;
    if (Name == "Int")
      return arena_.make_con(TyCon::Int);
    if (Name == "Float")
      return arena_.make_con(TyCon::Float);
    if (Name == "Bool")
      return arena_.make_con(TyCon::Bool);
    if (Name == "String")
      return arena_.make_con(TyCon::String);
    if (Name == "Char")
      return arena_.make_con(TyCon::Char);
    if (Name == "Byte")
      return arena_.make_con(TyCon::Byte);
    if (Name == "Symbol")
      return arena_.make_con(TyCon::Symbol);
    if (Name == "Unit" || Name == "()")
      return arena_.make_con(TyCon::Unit);
    if (Name == "ByteArray")
      return arena_.make_con(TyCon::ByteArray);
    if (Name == "IntArray" || Name == "FloatArray")
      return arena_.make_app(std::string(Name), {});
    const auto Separator = Name.rfind('\\');
    const auto Key = Separator == std::string_view::npos
                         ? model::NominalTypeKey{Module, std::string(Name)}
                         : model::NominalTypeKey{
                               model::ModuleIdentity{Name.substr(0, Separator)},
                               std::string(Name.substr(Separator + 1))};
    return arena_.make_app(Key, {}, std::string(Name));
  };
  std::vector<MonoTypePtr> StructuralHeads;
  StructuralHeads.reserve(type_names.size());
  for (std::size_t Index = 0; Index < type_names.size(); ++Index) {
    const auto &Name = type_names[Index];
    auto *Head = SimpleHead(Name);
    if (!StructuralParameters.empty() && (Index == 0 || Name == "Tuple")) {
      if (Name == "Tuple")
        Head = arena_.make_tuple(StructuralParameters);
      else if (Name == "Seq" || Name == "Set" || Name == "Dict")
        Head = arena_.make_app(Name, StructuralParameters);
      else if (Head->tag == MonoType::App)
        Head = Head->resolved_key
                   ? arena_.make_app(*Head->resolved_key, StructuralParameters,
                                     Head->type_name)
                   : arena_.make_app(Head->type_name, StructuralParameters);
    } else if (Index == 0 && Head->tag == MonoType::App && Head->args.empty()) {
      // A derived instance is registered as `Trait Nominal` without named
      // instance parameters. Preserve the nominal declaration's arity with
      // anonymous structural wildcards so `Pair Int String` matches
      // `Pair _ _` without returning to display-name prefix matching.
      if (const auto Declared = adt_type_params_.find(Name);
          Declared != adt_type_params_.end() && !Declared->second.empty()) {
        std::vector<MonoTypePtr> Wildcards;
        Wildcards.reserve(Declared->second.size());
        for ([[maybe_unused]] const auto &Parameter : Declared->second) {
          auto *Wildcard = arena_.fresh_var(0);
          uf_.add_var(Wildcard->var_id, 0);
          Wildcards.push_back(Wildcard);
        }
        Head = Head->resolved_key
                   ? arena_.make_app(*Head->resolved_key, std::move(Wildcards),
                                     Head->type_name)
                   : arena_.make_app(Head->type_name, std::move(Wildcards));
      }
    }
    StructuralHeads.push_back(Head);
  }
  instances.push_back({Module, std::move(type_names), std::move(type_params),
                       std::move(constraints), std::move(StructuralHeads),
                       std::move(StructuralParameters)});
  std::sort(instances.begin(), instances.end(),
            [](const InstanceContract &left, const InstanceContract &right) {
              return std::tie(left.module, left.type_names) <
                     std::tie(right.module, right.type_names);
            });
}

bool TypeChecker::solve_constraints() {
  bool all_ok = true;
  std::unordered_set<std::string> reported;
  std::vector<DeferredConstraint> worklist = deferred_constraints_;
  struct CollectionEvidence {
    std::optional<TraitMethodSelectionProjection> Hash;
    std::optional<TraitMethodSelectionProjection> Equals;
  };
  std::unordered_map<const ast::AstNode *, CollectionEvidence>
      CollectionEvidenceByNode;
  const auto methodFor = [](const DeferredConstraint &Constraint) {
    std::string Method = Constraint.trait_name == "Eq"     ? "eq"
                         : Constraint.trait_name == "Hash" ? "hash"
                         : Constraint.trait_name == "Ord"  ? "compare"
                                                           : "call";
    if (const auto *Application =
            dynamic_cast<const ast::ApplyExpr *>(Constraint.origin))
      if (const auto *Named =
              dynamic_cast<const ast::NameCall *>(Application->call))
        Method = Named->name->value;
    return Method;
  };
  const auto owningScheme = [&](const DeferredConstraint &Constraint)
      -> std::optional<SchemeIdentity> {
    const auto *Function = Constraint.owning_function;
    if (!Function)
      return std::nullopt;
    if (const auto Found = projection_function_identities_.find(Function);
        Found != projection_function_identities_.end())
      return Found->second;
    if (projection_module_ && !Function->name.empty())
      return model::FunctionDeclarationIdentity{
          model::SymbolIdentity{current_module_, Function->name}};
    return model::FunctionDeclarationIdentity{model::LocalFunctionIdentity{
        current_module_,
        {Function == projection_root_
             ? 0U
             : static_cast<std::uint32_t>(Function->Range.Offset)}}};
  };
  const auto publishEvidence = [&](const DeferredConstraint &Constraint,
                                   TraitMethodSelectionProjection Selection) {
    if (!Constraint.origin)
      return;
    switch (Constraint.evidence_role) {
    case DeferredConstraint::EvidenceRole::TraitMethod:
      trait_method_uses_.insert_or_assign(Constraint.origin,
                                          std::move(Selection));
      break;
    case DeferredConstraint::EvidenceRole::CollectionHash:
      CollectionEvidenceByNode[Constraint.origin].Hash = std::move(Selection);
      break;
    case DeferredConstraint::EvidenceRole::CollectionEquals:
      CollectionEvidenceByNode[Constraint.origin].Equals = std::move(Selection);
      break;
    case DeferredConstraint::EvidenceRole::None:
      break;
    }
  };
  for (size_t work_index = 0; work_index < worklist.size(); ++work_index) {
    const auto &dc = worklist[work_index];
    std::vector<MonoTypePtr> resolved;
    std::vector<std::string> type_names;
    std::vector<std::string> type_heads;
    bool unresolved = false;
    for (auto *argument : dc.types) {
      auto *concrete = zonk(argument);
      if (concrete->tag == MonoType::Var)
        unresolved = true;
      resolved.push_back(concrete);
      type_names.push_back(pretty_print(concrete));
      type_heads.push_back(
          concrete->tag == MonoType::App ? concrete->type_name
          : concrete->tag == MonoType::MTuple && concrete->elements.empty()
              ? "Unit"
          : concrete->tag == MonoType::MTuple ? "Tuple"
                                              : pretty_print(concrete));
      if (type_names.back() == "()")
        type_heads.back() = "Unit";
    }
    if (resolved.empty())
      continue;

    if (unresolved) {
      const auto Owner = owningScheme(dc);
      if (dc.origin && Owner &&
          dc.evidence_role != DeferredConstraint::EvidenceRole::None) {
        const auto Trait =
            dc.resolved_trait.value_or(resolved_trait_key(dc.trait_name));
        const Constraint StructuralConstraint{Trait, resolved};
        publishEvidence(dc, TraitMethodSelectionProjection{
                                Trait,
                                methodFor(dc),
                                resolved,
                                {},
                                {},
                                DeferredTraitEvidenceProjection{
                                    *Owner, StructuralConstraint},
                                dc.loc});
      }
      continue;
    }

    std::string application = dc.trait_name;
    for (const auto &name : type_names)
      application += " " + name;
    const std::string obligation_key = application + ":" + dc.context;
    const bool first_report = reported.insert(obligation_key).second;

    auto it = trait_instances_.find(dc.trait_name);
    if (dc.resolved_trait &&
        *dc.resolved_trait != resolved_trait_key(dc.trait_name))
      it = trait_instances_.end();
    if (it == trait_instances_.end()) {
      std::string message = "no instances for trait '" + dc.trait_name + "'";
      if (!dc.context.empty())
        message += " (" + dc.context + ")";
      if (first_report)
        diag_.error(dc.loc, ErrorCode::E0106, message);
      all_ok = false;
      continue;
    }
    struct Match {
      const InstanceContract *instance;
      std::unordered_map<std::string, MonoTypePtr> parameters;
    };
    std::vector<Match> matches;
    for (const auto &instance : it->second) {
      const auto &heads = instance.structural_heads;
      if (heads.size() != resolved.size())
        continue;
      std::unordered_map<TypeId, MonoTypePtr> StructuralBindings;
      std::function<bool(MonoTypePtr, MonoTypePtr)> MatchType;
      MatchType = [&](MonoTypePtr Pattern, MonoTypePtr Actual) -> bool {
        Pattern = zonk(Pattern);
        Actual = zonk(Actual);
        if (!Pattern || !Actual)
          return Pattern == Actual;
        if (Pattern->tag == MonoType::Var) {
          const auto Bound = StructuralBindings.find(Pattern->var_id);
          if (Bound == StructuralBindings.end()) {
            StructuralBindings.emplace(Pattern->var_id, Actual);
            return true;
          }
          return MatchType(Bound->second, Actual);
        }
        if (Pattern->tag != Actual->tag)
          return false;
        switch (Pattern->tag) {
        case MonoType::Var:
          return false;
        case MonoType::Con:
          return Pattern->con == Actual->con;
        case MonoType::App:
          if (Pattern->resolved_key || Actual->resolved_key) {
            if (!Pattern->resolved_key || !Actual->resolved_key ||
                *Pattern->resolved_key != *Actual->resolved_key)
              return false;
          } else if (Pattern->type_name != Actual->type_name) {
            return false;
          }
          if (Pattern->args.size() != Actual->args.size())
            return false;
          for (std::size_t Index = 0; Index < Pattern->args.size(); ++Index)
            if (!MatchType(Pattern->args[Index], Actual->args[Index]))
              return false;
          return true;
        case MonoType::MTuple:
          if (Pattern->elements.size() != Actual->elements.size())
            return false;
          for (std::size_t Index = 0; Index < Pattern->elements.size(); ++Index)
            if (!MatchType(Pattern->elements[Index], Actual->elements[Index]))
              return false;
          return true;
        case MonoType::MRecord:
          if (Pattern->record_fields.size() != Actual->record_fields.size())
            return false;
          for (std::size_t Index = 0; Index < Pattern->record_fields.size();
               ++Index)
            if (Pattern->record_fields[Index].first !=
                    Actual->record_fields[Index].first ||
                !MatchType(Pattern->record_fields[Index].second,
                           Actual->record_fields[Index].second))
              return false;
          return MatchType(Pattern->row_rest, Actual->row_rest);
        case MonoType::Sum:
          if (Pattern->sum_alternatives.size() !=
              Actual->sum_alternatives.size())
            return false;
          for (std::size_t Index = 0; Index < Pattern->sum_alternatives.size();
               ++Index)
            if (!MatchType(Pattern->sum_alternatives[Index],
                           Actual->sum_alternatives[Index]))
              return false;
          return true;
        case MonoType::Arrow:
          return MatchType(Pattern->param_type, Actual->param_type) &&
                 MatchType(Pattern->return_type, Actual->return_type) &&
                 arena_.effect_solver().summarize(Pattern->arrow_effect) ==
                     arena_.effect_solver().summarize(Actual->arrow_effect);
        }
        return false;
      };
      bool compatible = true;
      for (std::size_t Index = 0; Index < heads.size(); ++Index)
        if (!MatchType(heads[Index], resolved[Index])) {
          compatible = false;
          break;
        }
      if (!compatible)
        continue;
      std::unordered_map<std::string, MonoTypePtr> parameters;
      for (std::size_t Index = 0; Index < instance.type_params.size() &&
                                  Index < instance.structural_parameters.size();
           ++Index)
        if (const auto Bound = StructuralBindings.find(
                instance.structural_parameters[Index]->var_id);
            Bound != StructuralBindings.end())
          parameters.emplace(instance.type_params[Index], Bound->second);
      matches.push_back({&instance, std::move(parameters)});
    }
    if (matches.empty()) {
      std::string message = "no instance for '" + application + "'";
      if (!dc.context.empty())
        message += " (" + dc.context + ")";
      if (first_report)
        diag_.error(dc.loc, ErrorCode::E0105, message);
      if (first_report && dc.trait_name == "Eq") {
        diag_.note(
            dc.loc,
            "add `deriving Eq` to a value ADT, define an `instance Eq " +
                type_heads.front() +
                "`, or pass an explicit comparator such as Std\\Test.equalBy");
      } else if (first_report && dc.trait_name == "Ord") {
        diag_.note(dc.loc,
                   "add `deriving Ord` (and Eq), define an `instance Ord " +
                       type_heads.front() +
                       "`, or call an explicit comparison function");
      } else if (first_report && dc.trait_name == "Send") {
        diag_.note(
            dc.loc,
            "move only values with a `Send` instance across this boundary; "
            "immutable ADTs may add `deriving Send`, native arrays may move "
            "but not "
            "be shared, and Linear resources or promises must remain on their "
            "owning task");
      } else if (first_report && dc.trait_name == "Shareable") {
        diag_.note(dc.loc,
                   "capture only immutable values with a `Shareable` instance; "
                   "immutable ADTs may add `deriving Send, Shareable`, "
                   "synchronized channel "
                   "endpoints are shareable, and mutable native arrays require "
                   "a snapshot");
      }
      all_ok = false;
      continue;
    }
    if (matches.size() > 1) {
      if (first_report)
        diag_.error(dc.loc, ErrorCode::E0400,
                    "ambiguous visible instances for '" + application +
                        "'; remove the duplicate import or instance");
      all_ok = false;
      continue;
    }

    const auto &match = matches.front();
    const auto &instance = *match.instance;
    if (dc.origin) {
      if (const auto *Application =
              dynamic_cast<const ast::ApplyExpr *>(dc.origin))
        selected_trait_instances_[Application] = {dc.trait_name,
                                                  instance.type_names};
      std::vector<MonoTypePtr> TargetTypeArguments;
      TargetTypeArguments.reserve(instance.type_params.size());
      for (const auto &Parameter : instance.type_params)
        if (const auto Bound = match.parameters.find(Parameter);
            Bound != match.parameters.end())
          TargetTypeArguments.push_back(Bound->second);
      const model::FunctionDeclarationIdentity Target = model::SymbolIdentity{
          instance.module,
          instance_method_symbol(dc.trait_name, instance.type_names,
                                 methodFor(dc))};
      publishEvidence(
          dc, TraitMethodSelectionProjection{
                  dc.resolved_trait.value_or(resolved_trait_key(dc.trait_name)),
                  methodFor(dc),
                  resolved,
                  {},
                  {},
                  ResolvedTraitTargetProjection{
                      Target, std::move(TargetTypeArguments), {}},
                  dc.loc});
    }
    if (const auto superclasses = trait_superclasses_.find(dc.trait_name);
        superclasses != trait_superclasses_.end()) {
      for (const auto &superclass : superclasses->second)
        worklist.emplace_back(superclass, resolved.front(), dc.loc,
                              "superclass of " + application);
    }
    for (const auto &[required_trait, parameter] : instance.constraints) {
      const auto bound = match.parameters.find(parameter);
      if (bound == match.parameters.end()) {
        diag_.error(dc.loc, ErrorCode::E0400,
                    "malformed instance contract for '" + application +
                        "': constraint parameter '" + parameter +
                        "' is not declared");
        all_ok = false;
        continue;
      }
      worklist.emplace_back(required_trait, bound->second, dc.loc,
                            "required by " + application);
    }
  }
  for (auto &[Node, Evidence] : CollectionEvidenceByNode)
    if (Evidence.Hash && Evidence.Equals)
      collection_key_operations_.insert_or_assign(
          Node, CollectionKeyOperationsProjection{std::move(*Evidence.Hash),
                                                  std::move(*Evidence.Equals)});
  for (auto &[Node, Occurrence] : operation_occurrences_) {
    auto *Perform = std::get_if<PerformProjection>(&Occurrence);
    if (!Perform || !Node)
      continue;
    MonoTypePtr ContinuationResult = nullptr;
    for (auto *Current = Node->parent; Current; Current = Current->parent) {
      if (dynamic_cast<const ast::HandleExpr *>(Current)) {
        if (const auto Found = type_map_.find(Current);
            Found != type_map_.end())
          ContinuationResult = unifier_.resolve(Found->second);
        break;
      }
      if (const auto *Function =
              dynamic_cast<const ast::FunctionExpr *>(Current)) {
        auto Found = type_map_.find(Current);
        MonoTypePtr Type = Found == type_map_.end() ? nullptr : Found->second;
        const auto Arity = std::max<std::size_t>(Function->patterns.size(), 1);
        for (std::size_t Index = 0; Index < Arity && Type; ++Index) {
          Type = unifier_.resolve(Type);
          if (Type->tag != MonoType::Arrow)
            break;
          Type = Type->return_type;
        }
        ContinuationResult = unifier_.resolve(Type);
        break;
      }
    }
    if (!ContinuationResult)
      ContinuationResult = unifier_.resolve(projection_root_type_);
    if (ContinuationResult)
      Perform->ContinuationResultType = ContinuationResult;

    EffectRef EnclosingEffects = arena_.effect_solver().empty();
    for (auto *Current = Node->parent; Current; Current = Current->parent) {
      const auto Found = node_effects_.find(Current);
      if (Found == node_effects_.end())
        continue;
      EnclosingEffects = Found->second;
      break;
    }
    if (const auto Atom = operation_atoms_.find(Node);
        Atom != operation_atoms_.end())
      Perform->ResumeEffects =
          arena_.effect_solver().mask(EnclosingEffects, {Atom->second});
  }
  if (all_ok && !has_errors()) {
    if (projection_module_)
      refreshModuleStructuralProjection(projection_module_,
                                        projection_module_functions_);
    else
      refreshStructuralProjection(projection_root_, projection_root_type_,
                                  projection_visible_parameter_count_);
  } else {
    projection_storage_->clear();
    projection_storage_->publish();
  }
  return all_ok && !has_errors();
}

// ===== Effect Registration =====

model::EffectOperationKey
TypeChecker::resolved_effect_key(std::string_view EffectName,
                                 std::string_view OperationName) {
  const auto Separator = EffectName.rfind('\\');
  if (Separator == std::string_view::npos) {
    return model::EffectOperationKey{
        model::NominalTypeKey{current_module_, std::string(EffectName)},
        std::string(OperationName)};
  }
  return model::EffectOperationKey{
      model::NominalTypeKey{
          model::ModuleIdentity{EffectName.substr(0, Separator)},
          std::string(EffectName.substr(Separator + 1))},
      std::string(OperationName)};
}

std::optional<model::NominalTypeKey>
TypeChecker::resolved_nominal_key(std::string_view TypeName) const {
  if (ambiguous_nominal_names_.contains(std::string(TypeName)))
    return std::nullopt;
  const auto Found = nominal_keys_.find(std::string(TypeName));
  if (Found != nominal_keys_.end())
    return Found->second;
  const auto Separator = TypeName.rfind('\\');
  if (Separator == std::string_view::npos || Separator == 0 ||
      Separator + 1 == TypeName.size())
    return std::nullopt;
  return model::NominalTypeKey{
      model::ModuleIdentity{TypeName.substr(0, Separator)},
      std::string(TypeName.substr(Separator + 1))};
}

void TypeChecker::register_imported_nominals(const std::string &ModuleFqn) {
  if (!import_src_ || ModuleFqn.empty() ||
      !registered_import_modules_.insert(ModuleFqn).second)
    return;

  model::ModuleIdentity Module{ModuleFqn};
  for (auto &Nominal : import_src_->imported_nominals(ModuleFqn)) {
    const model::NominalTypeKey Key{Module, Nominal.name};
    if (ambiguous_nominal_names_.contains(Nominal.name))
      continue;
    const auto Existing = nominal_keys_.find(Nominal.name);
    if (Existing != nominal_keys_.end() && Existing->second != Key) {
      nominal_keys_.erase(Existing);
      if (ambiguous_nominal_names_.insert(Nominal.name).second) {
        diag_.error(SourceRange::unknown(), ErrorCode::E0100,
                    "ambiguous nominal type '" + Nominal.name +
                        "' is declared by multiple visible modules");
        ++error_count_;
      }
      continue;
    }
    nominal_keys_.insert_or_assign(Nominal.name, Key);
    adt_type_params_.insert_or_assign(Nominal.name,
                                      std::move(Nominal.type_parameters));
  }
}

model::NominalTypeKey
TypeChecker::resolved_trait_key(std::string_view TraitName) const {
  const auto Found = trait_keys_.find(std::string(TraitName));
  if (Found != trait_keys_.end())
    return Found->second;
  const auto Separator = TraitName.rfind('\\');
  if (Separator != std::string_view::npos && Separator != 0 &&
      Separator + 1 != TraitName.size())
    return {model::ModuleIdentity{TraitName.substr(0, Separator)},
            std::string(TraitName.substr(Separator + 1))};
  return {current_module_, std::string(TraitName)};
}

EffectAtomId
TypeChecker::intern_operation_atom(const model::EffectOperationKey &Key,
                                   std::vector<MonoTypePtr> TypeArguments,
                                   std::vector<EffectRef> EffectArguments) {
  auto Transaction = arena_.effect_atoms().beginAppend();
  const auto Atom = Transaction.intern(OperationEffectAtom{
      Key, std::move(TypeArguments), std::move(EffectArguments)});
  if (auto Prepared = Transaction.prepareCommit(); !Prepared)
    throw std::logic_error(Prepared.error());
  Transaction.commitPrepared();
  return Atom;
}

void TypeChecker::register_effect(
    const std::string &effect_name, const std::string &type_param,
    const std::vector<std::tuple<std::string, std::vector<MonoTypePtr>,
                                 MonoTypePtr>> &operations,
    const model::ModuleIdentity *declaration_module) {
  for (auto &[op_name, param_types, return_type] : operations) {
    std::string key = effect_name + "." + op_name;
    std::vector<MonoTypePtr> SignatureParts = param_types;
    SignatureParts.push_back(return_type);
    auto Signature = generalize(arena_.make_tuple(SignatureParts), -1);
    std::size_t TypeArgumentCount = 0;
    if (!type_param.empty()) {
      TypeArgumentCount = 1;
      if (Signature.quantified_vars.empty()) {
        auto *Phantom = arena_.fresh_var(0);
        uf_.add_var(Phantom->var_id, 0);
        Signature.quantified_vars.push_back(Phantom->var_id);
      }
    }
    const auto CallableContracts = declaration_callable_contracts(
        SignatureParts, model::CallingConvention::ClosureEntry,
        SourceRange::unknown());
    effect_ops_.insert_or_assign(
        key,
        EffectOpInfo{effect_name,
                     declaration_module
                         ? model::EffectOperationKey{model::NominalTypeKey{
                                                         *declaration_module,
                                                         effect_name},
                                                     op_name}
                         : resolved_effect_key(effect_name, op_name),
                     std::move(Signature), TypeArgumentCount,
                     param_types.size(), CallableContracts});
  }
}

// ===== Perform =====

MonoTypePtr TypeChecker::infer_perform(PerformExpr *node,
                                       std::shared_ptr<TypeEnv> env,
                                       int level) {
  std::string op_key = node->effect_name + "." + node->operation_name;
  auto Registered = effect_ops_.find(op_key);
  auto Key = Registered == effect_ops_.end()
                 ? resolved_effect_key(node->effect_name, node->operation_name)
                 : Registered->second.key;

  // Look up the operation's type signature
  auto it = Registered;
  std::vector<MonoTypePtr> TypeArguments;
  std::vector<EffectRef> EffectArguments;
  std::vector<MonoTypePtr> ParameterTypes;
  MonoTypePtr ReturnType = nullptr;
  if (it != effect_ops_.end()) {
    auto &info = it->second;
    std::vector<MonoTypePtr> FreshTypes;
    auto *Signature =
        instantiate(info.signature, level, &FreshTypes, &EffectArguments);
    if (!Signature || Signature->tag != MonoType::MTuple ||
        Signature->elements.size() != info.parameter_count + 1)
      throw std::logic_error("malformed registered effect signature");
    TypeArguments.assign(FreshTypes.begin(),
                         FreshTypes.begin() + static_cast<std::ptrdiff_t>(
                                                  info.type_argument_count));
    ParameterTypes.assign(
        Signature->elements.begin(),
        Signature->elements.begin() +
            static_cast<std::ptrdiff_t>(info.parameter_count));
    // `perform State.get ()` is the 0-arg surface (Unit is not a payload).
    // Ops that actually take Unit (`Gpu.oom`) keep the argument.
    size_t expected = info.parameter_count;
    std::vector<ExprNode *> payload;
    payload.reserve(node->args.size());
    for (auto *arg : node->args) {
      if (expected == 0 && arg && arg->get_type() == AST_UNIT_EXPR) {
        infer(arg, env, level);
        continue;
      }
      payload.push_back(arg);
    }
    size_t actual = payload.size();
    if (actual != expected) {
      diag_.error(node->Range, ErrorCode::E0201,
                  "effect operation '" + op_key + "' expects " +
                      std::to_string(expected) + " argument(s), got " +
                      std::to_string(actual));
      error_count_++;
    }
    for (size_t i = 0; i < payload.size() && i < info.parameter_count; i++) {
      auto *arg_type = infer(payload[i], env, level);
      unifier_.unify(arg_type, Signature->elements[i], node->Range,
                     "in argument " + std::to_string(i + 1) + " of perform " +
                         op_key);
    }
    ReturnType = Signature->elements.back();
    for (auto *&Argument : TypeArguments)
      Argument = zonk(Argument);
  } else {
    // Yona has no separate effect declaration syntax. The first source
    // occurrence therefore establishes the canonical operation scheme; later
    // performs and handlers instantiate and unify against this one scheme.
    ParameterTypes.reserve(node->args.size());
    for (auto *arg : node->args)
      ParameterTypes.push_back(infer(arg, env, level));
    auto *Variable = arena_.fresh_var(level);
    uf_.add_var(Variable->var_id, level);
    ReturnType = Variable;
    std::vector<MonoTypePtr> SignatureParts = ParameterTypes;
    SignatureParts.push_back(ReturnType);
    auto OccurrenceScheme = generalize(arena_.make_tuple(SignatureParts), -1);
    const auto findVariable = [&](TypeId Id) -> MonoTypePtr {
      for (auto *Part : SignatureParts) {
        std::vector<MonoTypePtr> Pending{Part};
        std::unordered_set<MonoTypePtr> Seen;
        while (!Pending.empty()) {
          auto *Current = unifier_.resolve(Pending.back());
          Pending.pop_back();
          if (!Current || !Seen.insert(Current).second)
            continue;
          if (Current->tag == MonoType::Var && Current->var_id == Id)
            return Current;
          if (Current->tag == MonoType::Arrow) {
            Pending.push_back(Current->param_type);
            Pending.push_back(Current->return_type);
          } else if (Current->tag == MonoType::App) {
            Pending.insert(Pending.end(), Current->args.begin(),
                           Current->args.end());
          } else if (Current->tag == MonoType::MTuple) {
            Pending.insert(Pending.end(), Current->elements.begin(),
                           Current->elements.end());
          } else if (Current->tag == MonoType::MRecord) {
            for (const auto &[_, Field] : Current->record_fields)
              Pending.push_back(Field);
            if (Current->row_rest)
              Pending.push_back(Current->row_rest);
          } else if (Current->tag == MonoType::Sum) {
            Pending.insert(Pending.end(), Current->sum_alternatives.begin(),
                           Current->sum_alternatives.end());
          }
        }
      }
      return nullptr;
    };
    for (const auto Id : OccurrenceScheme.quantified_vars)
      if (auto *Argument = findVariable(Id))
        TypeArguments.push_back(Argument);
    std::unordered_map<TypeId, MonoTypePtr> DeclarationVariables;
    for (const auto Id : OccurrenceScheme.quantified_vars) {
      auto *Variable = arena_.fresh_var(0);
      uf_.add_var(Variable->var_id, 0);
      DeclarationVariables.emplace(Id, Variable);
    }
    auto Signature =
        generalize(substitute(OccurrenceScheme.body, DeclarationVariables), -1);
    const auto TypeArgumentCount = Signature.quantified_vars.size();
    const auto *DeclarationTuple = Signature.body;
    const auto CallableContracts = declaration_callable_contracts(
        DeclarationTuple && DeclarationTuple->tag == MonoType::MTuple
            ? std::span<MonoTypePtr const>(DeclarationTuple->elements)
            : std::span<MonoTypePtr const>{},
        model::CallingConvention::ClosureEntry, node->Range);
    Registered =
        effect_ops_
            .insert_or_assign(
                op_key, EffectOpInfo{node->effect_name, Key,
                                     std::move(Signature), TypeArgumentCount,
                                     ParameterTypes.size(), CallableContracts})
            .first;
  }

  const auto Atom = intern_operation_atom(Key, TypeArguments, EffectArguments);
  operation_atoms_.insert_or_assign(node, Atom);
  include_evaluation_effect(arena_.effect_solver().atoms({Atom}));
  if (Registered != effect_ops_.end()) {
    std::vector<model::ParameterOwnership> Ownerships;
    Ownerships.reserve(ParameterTypes.size());
    for (const auto *Parameter : ParameterTypes) {
      const auto *Resolved = unifier_.resolve(Parameter);
      Ownerships.push_back(Resolved && Resolved->tag == MonoType::Con &&
                                   Resolved->con != TyCon::String
                               ? model::ParameterOwnership::Trivial
                               : model::ParameterOwnership::Consume);
    }
    const auto *ResolvedResult = unifier_.resolve(ReturnType);
    const auto ResultContract = ResolvedResult &&
                                        ResolvedResult->tag == MonoType::Con &&
                                        ResolvedResult->con != TyCon::String
                                    ? model::ResultOwnership::Trivial
                                    : model::ResultOwnership::Owned;
    operation_occurrences_.insert_or_assign(
        node, PerformProjection{
                  ResolvedOperationApplicationProjection{
                      Key, std::move(TypeArguments), std::move(EffectArguments),
                      std::move(ParameterTypes), std::move(Ownerships),
                      ReturnType, ResultContract},
                  ReturnType, arena_.effect_solver().empty(), node->Range});
  }
  if (!latent_effect_stack_.empty()) {
    effect_origins_.try_emplace(Atom, node->Range);
    const auto result = arena_.effect_solver().addAtom(
        latent_effect_stack_.back().effect, Atom);
    if (result == EffectConstraintResult::Conflict) {
      diag_.error(node->Range, ErrorCode::E0100,
                  "incompatible effect constraints for '" + op_key + "'");
      ++error_count_;
    }
  } else {
    unhandled_effect_locations_.push_back(node->Range);
    diag_.warning(node->Range, ErrorCode::E0200,
                  "effect operation '" + op_key +
                      "' may not be handled; "
                      "ensure a 'handle...with' block provides a handler for " +
                      node->effect_name,
                  WarningFlag::UnhandledEffect);
  }
  return ReturnType;
}

// ===== Handle =====

MonoTypePtr TypeChecker::infer_handle(HandleExpr *node,
                                      std::shared_ptr<TypeEnv> env, int level) {
  // Collect which operations this handle block covers
  struct HandlerOperation {
    model::EffectOperationKey Key;
    std::vector<MonoTypePtr> TypeArguments;
    std::vector<EffectRef> EffectArguments;
    std::vector<MonoTypePtr> ParameterTypes;
    MonoTypePtr ReturnType = nullptr;
  };
  std::unordered_map<const ast::HandlerClause *, HandlerOperation>
      HandlerOperations;
  std::vector<EffectAtomId> handled_ops;
  for (auto *clause : node->clauses) {
    if (!clause->is_return_clause) {
      const auto Display = clause->effect_name + "." + clause->operation_name;
      auto Registered = effect_ops_.find(Display);
      if (Registered == effect_ops_.end()) {
        // A handler can be the first source declaration of an operation. Keep
        // its declaration binders disjoint from this occurrence, just as for
        // the first `perform`, so the batch owns each binder exactly once.
        std::vector<MonoTypePtr> OccurrenceParts;
        OccurrenceParts.reserve(clause->arg_names.size() + 1);
        for ([[maybe_unused]] const auto &Name : clause->arg_names) {
          auto *Parameter = arena_.fresh_var(level);
          uf_.add_var(Parameter->var_id, level);
          OccurrenceParts.push_back(Parameter);
        }
        auto *Return = arena_.fresh_var(level);
        uf_.add_var(Return->var_id, level);
        OccurrenceParts.push_back(Return);
        const auto OccurrenceScheme =
            generalize(arena_.make_tuple(OccurrenceParts), -1);
        std::vector<MonoTypePtr> TypeArguments;
        TypeArguments.reserve(OccurrenceScheme.quantified_vars.size());
        for (const auto Id : OccurrenceScheme.quantified_vars) {
          const auto Variable =
              std::ranges::find_if(OccurrenceParts, [&](const auto *Part) {
                const auto *Resolved = unifier_.resolve(Part);
                return Resolved && Resolved->tag == MonoType::Var &&
                       Resolved->var_id == Id;
              });
          if (Variable != OccurrenceParts.end())
            TypeArguments.push_back(*Variable);
        }
        std::unordered_map<TypeId, MonoTypePtr> DeclarationVariables;
        for (const auto Id : OccurrenceScheme.quantified_vars) {
          auto *Variable = arena_.fresh_var(0);
          uf_.add_var(Variable->var_id, 0);
          DeclarationVariables.emplace(Id, Variable);
        }
        auto Declaration = generalize(
            substitute(OccurrenceScheme.body, DeclarationVariables), -1);
        const auto *DeclarationTuple = Declaration.body;
        const auto CallableContracts = declaration_callable_contracts(
            DeclarationTuple && DeclarationTuple->tag == MonoType::MTuple
                ? std::span<MonoTypePtr const>(DeclarationTuple->elements)
                : std::span<MonoTypePtr const>{},
            model::CallingConvention::ClosureEntry, clause->Range);
        const auto Key =
            resolved_effect_key(clause->effect_name, clause->operation_name);
        Registered =
            effect_ops_
                .insert_or_assign(
                    Display,
                    EffectOpInfo{clause->effect_name, Key,
                                 std::move(Declaration),
                                 OccurrenceScheme.quantified_vars.size(),
                                 clause->arg_names.size(), CallableContracts})
                .first;
        const auto Atom = intern_operation_atom(Key, TypeArguments, {});
        operation_atoms_.insert_or_assign(clause, Atom);
        handled_ops.push_back(Atom);
        HandlerOperations.emplace(clause,
                                  HandlerOperation{Key,
                                                   std::move(TypeArguments),
                                                   {},
                                                   {OccurrenceParts.begin(),
                                                    OccurrenceParts.end() - 1},
                                                   Return});
        continue;
      }
      const auto &Info = Registered->second;
      std::vector<MonoTypePtr> FreshTypes;
      std::vector<EffectRef> EffectArguments;
      auto *Signature =
          instantiate(Info.signature, level, &FreshTypes, &EffectArguments);
      if (!Signature || Signature->tag != MonoType::MTuple ||
          Signature->elements.size() != Info.parameter_count + 1)
        throw std::logic_error("malformed registered effect signature");
      std::vector<MonoTypePtr> TypeArguments(
          FreshTypes.begin(),
          FreshTypes.begin() +
              static_cast<std::ptrdiff_t>(Info.type_argument_count));
      handled_ops.push_back(
          intern_operation_atom(Info.key, TypeArguments, EffectArguments));
      operation_atoms_.insert_or_assign(clause, handled_ops.back());
      HandlerOperations.emplace(
          clause, HandlerOperation{
                      Info.key,
                      std::move(TypeArguments),
                      std::move(EffectArguments),
                      {Signature->elements.begin(),
                       Signature->elements.begin() +
                           static_cast<std::ptrdiff_t>(Info.parameter_count)},
                      Signature->elements.back()});
    }
  }

  // Infer the protected body in an isolated derived cell, then project it
  // through a symbolic mask into the surrounding function.  The mask stays
  // symbolic so effects contributed by an unknown callback after helper
  // instantiation are still removed by this handler.
  const auto body_effect = arena_.effect_solver().derived();
  latent_effect_stack_.push_back({body_effect});
  isolated_evaluation_roots_.insert(node->body);
  auto *body_type = infer(node->body, env, level);
  isolated_evaluation_roots_.erase(node->body);
  latent_effect_stack_.pop_back();

  std::ranges::sort(handled_ops);
  handled_ops.erase(std::ranges::unique(handled_ops).begin(),
                    handled_ops.end());
  const auto masked_body_effect =
      arena_.effect_solver().mask(body_effect, handled_ops);

  // The result type of the whole handle expression.
  // If there's a return clause, it transforms the body result â€” so
  // result_type may differ from body_type. Start with a fresh var.
  auto *result_type = arena_.fresh_var(level);
  uf_.add_var(result_type->var_id, level);

  // Check for a return clause first to establish the result type
  bool has_return = false;
  for (auto *clause : node->clauses) {
    if (clause->is_return_clause) {
      has_return = true;
      auto clause_env = env->child();
      clause_env->bind(clause->return_binding, body_type);
      auto *clause_type = infer(clause->body, clause_env, level);
      unifier_.unify(result_type, clause_type, clause->Range,
                     "in return handler clause");
      break;
    }
  }
  // No return clause â†’ result is body type
  if (!has_return)
    unifier_.unify(result_type, body_type, node->body->Range, "in handle body");

  result_type = unifier_.resolve(result_type);

  // Infer operation handler clauses
  for (auto *clause : node->clauses) {
    if (clause->is_return_clause)
      continue;

    auto clause_env = env->child();
    std::string op_key = clause->effect_name + "." + clause->operation_name;

    // Bind operation argument names
    const auto Operation = HandlerOperations.find(clause);
    for (size_t i = 0; i < clause->arg_names.size(); i++) {
      MonoTypePtr arg_type;
      if (Operation != HandlerOperations.end() &&
          i < Operation->second.ParameterTypes.size())
        arg_type = Operation->second.ParameterTypes[i];
      else {
        arg_type = arena_.fresh_var(level);
        uf_.add_var(arg_type->var_id, level);
      }
      clause_env->bind(clause->arg_names[i], arg_type);
    }

    MonoTypePtr SourceResumeType = nullptr;
    std::optional<CallableTypeContract> ResumeContract;
    // Bind resume to its explicit continuation contract. Its reinstated row
    // is the protected body's row with the handled operations removed.
    if (!clause->resume_name.empty()) {
      MonoTypePtr resume_param;
      if (Operation != HandlerOperations.end())
        resume_param = Operation->second.ReturnType;
      else {
        resume_param = arena_.fresh_var(level);
        uf_.add_var(resume_param->var_id, level);
      }
      SourceResumeType =
          arena_.make_arrow(resume_param, result_type, masked_body_effect);
      const auto *ResolvedParameter = unifier_.resolve(resume_param);
      const auto *ResolvedResumeResult = unifier_.resolve(result_type);
      ResumeContract.emplace(CallableTypeContract{
          SourceResumeType,
          {ResolvedParameter && ResolvedParameter->tag == MonoType::Con &&
                   ResolvedParameter->con != TyCon::String
               ? model::ParameterOwnership::Trivial
               : model::ParameterOwnership::Consume},
          ResolvedResumeResult && ResolvedResumeResult->tag == MonoType::Con &&
                  ResolvedResumeResult->con != TyCon::String
              ? model::ResultOwnership::Trivial
              : model::ResultOwnership::Owned,
          model::CallingConvention::Continuation,
          clause->Range});
      scheme_callable_contracts_.insert_or_assign(
          SourceResumeType, std::vector<CallableTypeContract>{*ResumeContract});
      clause_env->bind_scheme(clause->resume_name,
                              TypeScheme(SourceResumeType));
    }

    auto *clause_type = infer(clause->body, clause_env, level);
    unifier_.unify(result_type, clause_type, clause->Range,
                   "in handler clause for " + op_key);
    result_type = unifier_.resolve(result_type);
    record(clause, result_type);

    if (Operation != HandlerOperations.end()) {
      std::vector<model::ParameterOwnership> Ownerships;
      Ownerships.reserve(Operation->second.ParameterTypes.size());
      for (const auto *Parameter : Operation->second.ParameterTypes) {
        const auto *Resolved = unifier_.resolve(Parameter);
        Ownerships.push_back(Resolved && Resolved->tag == MonoType::Con &&
                                     Resolved->con != TyCon::String
                                 ? model::ParameterOwnership::Trivial
                                 : model::ParameterOwnership::Consume);
      }
      const auto *ResolvedResult =
          unifier_.resolve(Operation->second.ReturnType);
      const auto ResultContract =
          ResolvedResult && ResolvedResult->tag == MonoType::Con &&
                  ResolvedResult->con != TyCon::String
              ? model::ResultOwnership::Trivial
              : model::ResultOwnership::Owned;
      if (!SourceResumeType)
        SourceResumeType = arena_.make_arrow(Operation->second.ReturnType,
                                             result_type, masked_body_effect);
      if (!function_inference_stack_.empty() && ResumeContract)
        function_callable_contracts_[function_inference_stack_.back()]
            .push_back(*ResumeContract);
      operation_occurrences_.insert_or_assign(
          clause,
          HandlerClauseProjection{
              ResolvedOperationApplicationProjection{
                  Operation->second.Key, Operation->second.TypeArguments,
                  Operation->second.EffectArguments,
                  Operation->second.ParameterTypes, std::move(Ownerships),
                  Operation->second.ReturnType, ResultContract},
              SourceResumeType, masked_body_effect, clause->Range});
    }
  }

  // Keep the mask symbolic until handler-clause inference has constrained the
  // operation's ordered type/effect arguments. Materialize it here even when
  // the handle expression has no enclosing latent-effect cell; otherwise a
  // top-level handle would retain only the handler occurrence's raw atom and
  // miss an equivalent protected-body instance. The matcher adds only full
  // type/effect-argument matches, never every application sharing the key.
  if (materialize_effect_masks(masked_body_effect, node->Range,
                               "in handled expression"))
    include_ambient_effect(masked_body_effect, node->Range,
                           "in handled expression");

  return result_type;
}

void TypeChecker::bind_import_name(std::shared_ptr<TypeEnv> env,
                                   const std::string &module_fqn,
                                   const std::string &func_name,
                                   const std::string &bind_name, int level) {
  std::optional<yona::interface::InterfaceModule> interface_module;
  std::optional<model::ModuleIdentity> SourceIdentity;
  if (!module_fqn.empty() && !func_name.empty()) {
    try {
      SourceIdentity.emplace(module_fqn);
      auto loaded = yona::interface::readModuleFromSearchPaths(ModulePaths,
                                                               *SourceIdentity);
      if (loaded && loaded->has_value())
        interface_module.emplace(std::move(**loaded));
    } catch (const std::invalid_argument &) {
      // The normal unknown-import diagnostic remains authoritative for an
      // invalid source-level module name.
    }
  }
  const yona::interface::Function *row =
      interface_module
          ? yona::interface::findFunction(*interface_module, func_name)
          : nullptr;
  std::optional<ImportedFnSig> lin;
  if (row) {
    ImportedFnSig Signature;
    Signature.param_descriptors = row->ParameterTypes;
    Signature.return_descriptor = row->ReturnType;
    Signature.effect_scheme = row->Effects.Scheme;
    lin = std::move(Signature);
  } else if (import_src_) {
    lin = import_src_->imported_function_sig(module_fqn, func_name);
  }

  std::unordered_set<std::string> SourceNominals;
  if (interface_module) {
    for (const auto &Adt : interface_module->Adts)
      SourceNominals.insert(Adt.Name);
  } else if (import_src_) {
    for (const auto &Nominal : import_src_->imported_nominals(module_fqn))
      SourceNominals.insert(Nominal.name);
  }

  if (row) {
    if (!row->Effects.IsKnown && require_effect_free_)
      has_unknown_effect_rows_ = true;
    auto *Exact = mono_from_interface_function(
        *row, level, SourceIdentity ? &*SourceIdentity : nullptr,
        &SourceNominals);
    auto Scheme = generalize(Exact, level - 1);
    const model::FunctionDeclarationIdentity Identity = model::SymbolIdentity{
        SourceIdentity.value_or(model::ModuleIdentity{module_fqn}), func_name};
    scheme_identities_.insert_or_assign(Scheme.body, Identity);
    const auto Convention = [&] {
      switch (row->Kind) {
      case interface::FunctionKind::Synchronous:
        return model::CallingConvention::DirectYona;
      case interface::FunctionKind::ThreadPool:
      case interface::FunctionKind::Io:
        return model::CallingConvention::AsyncAdapter;
      case interface::FunctionKind::Native:
        return model::CallingConvention::NativeExtern;
      }
      return model::CallingConvention::DirectYona;
    }();
    auto Declaration = imported_function_declaration(
        Scheme, row->BorrowedParameters, row->ParameterTypes.size(), Convention,
        SourceRange::unknown());
    auto Contracts = Declaration.CallableContracts;
    imported_function_declarations_.insert_or_assign(Identity,
                                                     std::move(Declaration));
    scheme_callable_contracts_.insert_or_assign(Scheme.body,
                                                std::move(Contracts));
    env->bind_scheme(bind_name, std::move(Scheme));
    return;
  }
  if (lin) {
    if (require_effect_free_)
      has_unknown_effect_rows_ = true;
    auto Scheme = generalize(
        mono_from_import_sig(
            *lin, level, nullptr, SourceIdentity ? &*SourceIdentity : nullptr,
            !SourceNominals.empty() ? &SourceNominals : nullptr),
        -1);
    const model::FunctionDeclarationIdentity Identity = model::SymbolIdentity{
        SourceIdentity.value_or(model::ModuleIdentity{module_fqn}), func_name};
    scheme_identities_.insert_or_assign(Scheme.body, Identity);
    auto Declaration = imported_function_declaration(
        Scheme, lin->borrowed_parameters, lin->param_descriptors.size(),
        lin->convention, SourceRange::unknown());
    auto Contracts = Declaration.CallableContracts;
    imported_function_declarations_.insert_or_assign(Identity,
                                                     std::move(Declaration));
    scheme_callable_contracts_.insert_or_assign(Scheme.body,
                                                std::move(Contracts));
    env->bind_scheme(bind_name, std::move(Scheme));
    return;
  }
  if (require_effect_free_)
    has_unknown_effect_rows_ = true;
  auto *v = arena_.fresh_var(level);
  uf_.add_var(v->var_id, level);
  env->bind(bind_name, v);
}

MonoTypePtr TypeChecker::mono_from_import_sig(
    const ImportedFnSig &sig, int level,
    std::unordered_map<std::string, MonoTypePtr> *output_descriptor_variables,
    const model::ModuleIdentity *source_module,
    const std::unordered_set<std::string> *source_nominals) {
  auto fresh = [this, level]() {
    auto *v = arena_.fresh_var(level);
    uf_.add_var(v->var_id, level);
    return v;
  };
  // Bare collection/function/ADT descriptors are canonical structural
  // wildcards. Scalar descriptors, including INT, are always exact; generic
  // variables must use VAR(name).
  auto from_atom = [&](const std::string &tag) -> MonoTypePtr {
    if (tag == "INT")
      return arena_.make_con(TyCon::Int);
    if (tag == "STRING")
      return arena_.make_con(TyCon::String);
    if (tag == "BOOL")
      return arena_.make_con(TyCon::Bool);
    if (tag == "FLOAT")
      return arena_.make_con(TyCon::Float);
    if (tag == "CHAR")
      return arena_.make_con(TyCon::Char);
    if (tag == "BYTE")
      return arena_.make_con(TyCon::Byte);
    if (tag == "SYMBOL")
      return arena_.make_con(TyCon::Symbol);
    if (tag == "UNIT")
      return arena_.make_con(TyCon::Unit);
    if (tag == "SEQ")
      return arena_.make_app("Seq", {fresh()});
    if (tag == "SET")
      return arena_.make_app("Set", {fresh()});
    if (tag == "DICT")
      return arena_.make_app("Dict", {fresh(), fresh()});
    if (tag == "BYTE_ARRAY")
      return arena_.make_con(TyCon::ByteArray);
    if (tag == "INT_ARRAY")
      return arena_.make_app("IntArray", {});
    if (tag == "FLOAT_ARRAY")
      return arena_.make_app("FloatArray", {});
    if (tag == "CHANNEL")
      return arena_.make_app("Channel", {fresh()});
    if (tag == "PROMISE")
      return arena_.make_promise(fresh(), model::ResultOwnership::Owned,
                                 arena_.effect_solver().empty());
    if (tag == "FUNCTION")
      return arena_.make_arrow(fresh(), fresh());
    if (tag == "ADT")
      return arena_.make_app("ADT", {fresh()});
    if (tag == "LINEAR" || tag == "TUPLE")
      return fresh();
    throw std::invalid_argument("unknown canonical interface type: " + tag);
  };
  std::unordered_map<std::string, MonoTypePtr> descriptor_variables;
  auto from_descriptor = [&](const std::string &text) -> MonoTypePtr {
    std::function<MonoTypePtr(const std::string &)> parse;
    parse = [&](const std::string &value) -> MonoTypePtr {
      auto open = value.find('(');
      if (open == std::string::npos)
        return from_atom(value);
      if (!value.ends_with(')'))
        throw std::invalid_argument("malformed canonical interface type: " +
                                    value);
      std::string name = value.substr(0, open);
      std::vector<std::string> parts;
      std::string part;
      int depth = 0;
      for (size_t i = open + 1; i + 1 < value.size(); ++i) {
        char c = value[i];
        if (c == '(')
          ++depth;
        else if (c == ')')
          --depth;
        if (c == ',' && depth == 0) {
          parts.push_back(part);
          part.clear();
        } else
          part += c;
      }
      if (!part.empty())
        parts.push_back(part);
      if (name == "VAR" && parts.size() == 1) {
        auto found = descriptor_variables.find(parts[0]);
        if (found != descriptor_variables.end())
          return found->second;
        auto *variable = fresh();
        descriptor_variables[parts[0]] = variable;
        return variable;
      }
      if (name == "ADT" && !parts.empty()) {
        std::vector<MonoTypePtr> type_arguments;
        for (size_t i = 1; i < parts.size(); ++i)
          type_arguments.push_back(parse(parts[i]));
        if (type_arguments.empty()) {
          if (const auto Declared = adt_type_params_.find(parts[0]);
              Declared != adt_type_params_.end()) {
            for (size_t i = 0; i < Declared->second.size(); ++i)
              type_arguments.push_back(fresh());
          }
        }
        if (source_module && source_nominals &&
            source_nominals->contains(parts[0]))
          return arena_.make_app(
              model::NominalTypeKey{*source_module, parts[0]},
              std::move(type_arguments), parts[0]);
        if (const auto Key = resolved_nominal_key(parts[0]))
          return arena_.make_app(*Key, std::move(type_arguments), parts[0]);
        return arena_.make_app(parts[0], std::move(type_arguments));
      }
      std::vector<MonoTypePtr> args;
      for (const auto &nested : parts)
        args.push_back(parse(nested));
      if (name == "TUPLE")
        return arena_.make_tuple(args);
      if (name == "FUNCTION" && args.size() == 2)
        return arena_.make_arrow(args[0], args[1]);
      if (name == "LINEAR" && args.size() == 1) {
        if (const auto Key = resolved_nominal_key("Linear"))
          return arena_.make_app(*Key, std::move(args), "Linear");
        return arena_.make_app("Linear", std::move(args));
      }
      if (name == "Promise" && args.size() == 1) {
        const auto *Success = unifier_.resolve(args.front());
        const auto Ownership = Success && Success->tag == MonoType::Con &&
                                       Success->con != TyCon::String
                                   ? model::ResultOwnership::Trivial
                                   : model::ResultOwnership::Owned;
        return arena_.make_promise(args.front(), Ownership,
                                   arena_.effect_solver().empty());
      }
      if (name == "Seq" || name == "SET" || name == "Set" || name == "DICT" ||
          name == "Dict")
        return arena_.make_app(name == "SET"    ? "Set"
                               : name == "DICT" ? "Dict"
                                                : name,
                               args);
      throw std::invalid_argument("unknown canonical interface type: " + value);
    };
    return parse(text);
  };
  if (sig.return_descriptor.empty())
    throw std::invalid_argument(
        "imported function requires a canonical return descriptor");
  MonoTypePtr ret = from_descriptor(sig.return_descriptor);
  MonoTypePtr fn = ret;
  for (auto Descriptor = sig.param_descriptors.rbegin();
       Descriptor != sig.param_descriptors.rend(); ++Descriptor) {
    if (Descriptor->empty())
      throw std::invalid_argument(
          "imported function requires canonical parameter descriptors");
    fn = arena_.make_arrow(from_descriptor(*Descriptor), fn);
  }
  if (output_descriptor_variables)
    *output_descriptor_variables = descriptor_variables;
  return fn;
}

MonoTypePtr TypeChecker::mono_from_interface_function(
    const interface::Function &Function, int Level,
    const model::ModuleIdentity *SourceModule,
    const std::unordered_set<std::string> *SourceNominals) {
  ImportedFnSig Signature;
  Signature.param_descriptors = Function.ParameterTypes;
  Signature.return_descriptor = Function.ReturnType;
  Signature.effect_scheme = Function.Effects.Scheme;
  auto *Type = mono_from_import_sig(Signature, Level, nullptr, SourceModule,
                                    SourceNominals);
  const auto ShiftedThunkScheme =
      Function.ParameterTypes.empty()
          ? normalize_zero_arity_thunk_scheme(Function.Effects.Scheme)
          : std::nullopt;
  const bool ReturnDescriptorIsArrow =
      unifier_.resolve(Type) && unifier_.resolve(Type)->tag == MonoType::Arrow;
  if (ShiftedThunkScheme && !ReturnDescriptorIsArrow)
    Type = arena_.make_arrow(arena_.make_con(TyCon::Unit), Type);
  // Legacy interfaces predate effect schemes. A non-empty/open known effect
  // row cannot describe a CAF value because effects attach to calls, so it is
  // sufficient evidence for the same visible Unit thunk. Unknown and
  // explicitly pure zero-arity native/CAF rows deliberately remain values.
  const bool LegacyKnownUnitThunk =
      Function.ParameterTypes.empty() && Function.Effects.IsKnown &&
      Function.Effects.Scheme.empty() &&
      (!Function.Effects.Operations.empty() || Function.Effects.IsOpen) &&
      !ReturnDescriptorIsArrow;
  if (LegacyKnownUnitThunk)
    Type = arena_.make_arrow(arena_.make_con(TyCon::Unit), Type);
  if (!Function.Effects.IsKnown)
    return Type;
  if (ShiftedThunkScheme)
    return apply_effect_scheme(Type, *ShiftedThunkScheme);
  if (!Function.Effects.Scheme.empty())
    return apply_effect_scheme(Type, Function.Effects.Scheme);

  std::vector<EffectRef> Sources;
  if (!Function.Effects.Operations.empty()) {
    std::vector<EffectAtomId> Atoms;
    Atoms.reserve(Function.Effects.Operations.size());
    for (const auto &Operation : Function.Effects.Operations) {
      const auto Dot = Operation.rfind('.');
      if (Dot == std::string::npos || Dot == 0 || Dot + 1 == Operation.size())
        throw std::invalid_argument("invalid imported effect operation name");
      const auto Atom = intern_operation_atom(
          resolved_effect_key(std::string_view(Operation).substr(0, Dot),
                              std::string_view(Operation).substr(Dot + 1)));
      legacy_untyped_effect_atoms_.insert(Atom);
      Atoms.push_back(Atom);
    }
    Sources.push_back(arena_.effect_solver().atoms(std::move(Atoms)));
  }
  if (Function.Effects.IsOpen)
    Sources.push_back(arena_.effect_solver().opaque());
  const auto ImportedEffect = arena_.effect_solver().join(std::move(Sources));
  std::function<MonoTypePtr(MonoTypePtr, bool)> AttachEffects;
  AttachEffects = [&](MonoTypePtr Current, bool FirstParameter) -> MonoTypePtr {
    Current = unifier_.resolve(Current);
    if (!Current || Current->tag != MonoType::Arrow)
      return Current;
    MonoTypePtr Parameter = Current->param_type;
    if (Function.Effects.IsHigherOrder && FirstParameter) {
      auto *ResolvedParameter = unifier_.resolve(Parameter);
      if (ResolvedParameter && ResolvedParameter->tag == MonoType::Arrow)
        Parameter = AttachEffects(ResolvedParameter, false);
    }
    return arena_.make_arrow(
        Parameter, AttachEffects(Current->return_type, false), ImportedEffect);
  };
  return AttachEffects(Type, true);
}

MonoTypePtr TypeChecker::from_ast_type(const yona::compiler::types::Type &t,
                                       int level) {
  std::unordered_map<std::string, MonoTypePtr> variables;
  return from_ast_type_impl(t, level, variables);
}

MonoTypePtr TypeChecker::from_ast_type_impl(
    const yona::compiler::types::Type &t, int level,
    std::unordered_map<std::string, MonoTypePtr> &variables) {
  using yona::compiler::types::BuiltinType;
  using yona::compiler::types::FunctionType;
  using yona::compiler::types::NamedType;
  using yona::compiler::types::ProductType;
  using yona::compiler::types::PromiseType;
  using yona::compiler::types::RefinedType;
  using yona::compiler::types::SingleItemCollectionType;
  using yona::compiler::types::SumType;

  if (std::holds_alternative<std::nullptr_t>(t)) {
    auto *v = arena_.fresh_var(level);
    uf_.add_var(v->var_id, level);
    return v;
  }
  if (std::holds_alternative<BuiltinType>(t)) {
    switch (std::get<BuiltinType>(t)) {
    case BuiltinType::Bool:
      return arena_.make_con(TyCon::Bool);
    case BuiltinType::String:
      return arena_.make_con(TyCon::String);
    case BuiltinType::Symbol:
      return arena_.make_con(TyCon::Symbol);
    case BuiltinType::Unit:
      return arena_.make_con(TyCon::Unit);
    case BuiltinType::Float32:
    case BuiltinType::Float64:
    case BuiltinType::Float128:
      return arena_.make_con(TyCon::Float);
    case BuiltinType::Seq:
      return arena_.make_app("Seq", {[&]() {
                               auto *v = arena_.fresh_var(level);
                               uf_.add_var(v->var_id, level);
                               return v;
                             }()});
    case BuiltinType::Set:
      return arena_.make_app("Set", {[&]() {
                               auto *v = arena_.fresh_var(level);
                               uf_.add_var(v->var_id, level);
                               return v;
                             }()});
    default:
      return arena_.make_con(TyCon::Int);
    }
  }
  if (std::holds_alternative<std::shared_ptr<FunctionType>>(t)) {
    auto &ft = std::get<std::shared_ptr<FunctionType>>(t);
    return arena_.make_arrow(
        from_ast_type_impl(ft->argumentType, level, variables),
        from_ast_type_impl(ft->returnType, level, variables));
  }
  if (std::holds_alternative<std::shared_ptr<SumType>>(t)) {
    const auto &Sum = std::get<std::shared_ptr<SumType>>(t);
    if (!Sum)
      throw std::invalid_argument("source sum type must not be null");
    std::vector<MonoTypePtr> Alternatives;
    Alternatives.reserve(Sum->types.size());
    for (const auto &Alternative : Sum->types)
      Alternatives.push_back(from_ast_type_impl(Alternative, level, variables));
    return arena_.make_sum(std::move(Alternatives));
  }
  if (std::holds_alternative<std::shared_ptr<ProductType>>(t)) {
    auto &pt = std::get<std::shared_ptr<ProductType>>(t);
    std::vector<MonoTypePtr> elems;
    elems.reserve(pt->types.size());
    for (auto &e : pt->types)
      elems.push_back(from_ast_type_impl(e, level, variables));
    return arena_.make_tuple(elems);
  }
  if (std::holds_alternative<std::shared_ptr<NamedType>>(t)) {
    auto &nt = std::get<std::shared_ptr<NamedType>>(t);
    if (!nt->name.empty() &&
        std::islower(static_cast<unsigned char>(nt->name.front())) &&
        std::holds_alternative<std::nullptr_t>(nt->type)) {
      if (const auto found = variables.find(nt->name); found != variables.end())
        return found->second;
      auto *variable = arena_.fresh_var(level);
      uf_.add_var(variable->var_id, level);
      variables[nt->name] = variable;
      return variable;
    }
    if (std::holds_alternative<std::nullptr_t>(nt->type)) {
      if (nt->name == "ByteArray")
        return arena_.make_con(TyCon::ByteArray);
      if (nt->name == "IntArray" || nt->name == "FloatArray")
        return arena_.make_app(nt->name, {});
    }
    if (!std::holds_alternative<std::nullptr_t>(nt->type)) {
      // Surface syntax writes multi-parameter applications as
      // `Result (a, e)`.  The product here is argument punctuation,
      // not a single tuple argument: normalize it to the same n-ary
      // App representation used by constructor schemes and `.yonai`.
      const auto params = adt_type_params_.find(nt->name);
      if (const auto *product =
              std::get_if<std::shared_ptr<ProductType>>(&nt->type);
          product && params != adt_type_params_.end() &&
          params->second.size() > 1 &&
          (*product)->types.size() == params->second.size()) {
        std::vector<MonoTypePtr> arguments;
        arguments.reserve((*product)->types.size());
        for (const auto &argument : (*product)->types)
          arguments.push_back(from_ast_type_impl(argument, level, variables));
        if (const auto Key = resolved_nominal_key(nt->name))
          return arena_.make_app(*Key, std::move(arguments), nt->name);
        return arena_.make_app(nt->name, std::move(arguments));
      }
      std::vector<MonoTypePtr> Arguments{
          from_ast_type_impl(nt->type, level, variables)};
      if (nt->name == "Promise") {
        const auto *Success = unifier_.resolve(Arguments.front());
        const auto Ownership = Success && Success->tag == MonoType::Con &&
                                       Success->con != TyCon::String
                                   ? model::ResultOwnership::Trivial
                                   : model::ResultOwnership::Owned;
        return arena_.make_promise(Arguments.front(), Ownership,
                                   arena_.effect_solver().empty());
      }
      if (const auto Key = resolved_nominal_key(nt->name))
        return arena_.make_app(*Key, std::move(Arguments), nt->name);
      return arena_.make_app(nt->name, std::move(Arguments));
    }
    if (const auto Key = resolved_nominal_key(nt->name))
      return arena_.make_app(*Key, {}, nt->name);
    return arena_.make_app(nt->name, {});
  }
  if (std::holds_alternative<std::shared_ptr<PromiseType>>(t)) {
    auto &pr = std::get<std::shared_ptr<PromiseType>>(t);
    auto *Success = from_ast_type_impl(pr->valueType, level, variables);
    const auto *Resolved = unifier_.resolve(Success);
    const auto Ownership = Resolved && Resolved->tag == MonoType::Con &&
                                   Resolved->con != TyCon::String
                               ? model::ResultOwnership::Trivial
                               : model::ResultOwnership::Owned;
    return arena_.make_promise(Success, Ownership,
                               arena_.effect_solver().empty());
  }
  if (std::holds_alternative<std::shared_ptr<RefinedType>>(t)) {
    auto &rt = std::get<std::shared_ptr<RefinedType>>(t);
    return from_ast_type_impl(rt->base_type, level, variables);
  }
  if (std::holds_alternative<std::shared_ptr<SingleItemCollectionType>>(t)) {
    auto &col = std::get<std::shared_ptr<SingleItemCollectionType>>(t);
    auto *elem = from_ast_type_impl(col->valueType, level, variables);
    const char *name =
        (col->kind == SingleItemCollectionType::Seq) ? "Seq" : "Set";
    return arena_.make_app(name, {elem});
  }
  auto *v = arena_.fresh_var(level);
  uf_.add_var(v->var_id, level);
  return v;
}

} // namespace yona::compiler::typechecker
