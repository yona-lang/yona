#include "yona/Semantics/SemanticModel.h"

#include "yona/Model/InferType.h"
#include "yona/Semantics/PatternAnalysis.h"
#include "yona/Semantics/TypeChecker.h"
#include "yona/Support/Diagnostic.h"
#include "yona/Syntax/Ast.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <variant>

namespace yona::semantics {
namespace {

std::string declaredTypeText(const compiler::types::Type &Type) {
  using compiler::types::Bool;
  using compiler::types::BuiltinTypeStrings;
  using compiler::types::BuiltinType;
  using compiler::types::Byte;
  using compiler::types::DictCollectionType;
  using compiler::types::Float32;
  using compiler::types::Float64;
  using compiler::types::Float128;
  using compiler::types::FunctionType;
  using compiler::types::NamedType;
  using compiler::types::ProductType;
  using compiler::types::SignedInt16;
  using compiler::types::SignedInt32;
  using compiler::types::SignedInt64;
  using compiler::types::SignedInt128;
  using compiler::types::SingleItemCollectionType;
  using compiler::types::String;
  using compiler::types::Symbol;
  using compiler::types::Unit;
  using compiler::types::UnsignedInt16;
  using compiler::types::UnsignedInt32;
  using compiler::types::UnsignedInt64;
  using compiler::types::UnsignedInt128;
  if (const auto *Builtin = std::get_if<BuiltinType>(&Type)) {
    switch (*Builtin) {
    case Bool:
      return "Bool";
    case String:
      return "String";
    case Symbol:
      return "Symbol";
    case Unit:
      return "Unit";
    case Float32:
    case Float64:
    case Float128:
      return "Float";
    case Byte:
      return "Byte";
    case SignedInt16:
    case SignedInt32:
    case SignedInt64:
    case SignedInt128:
    case UnsignedInt16:
    case UnsignedInt32:
    case UnsignedInt64:
    case UnsignedInt128:
      return "Int";
    default:
      return BuiltinTypeStrings[*Builtin];
    }
  }
  if (const auto *Function = std::get_if<std::shared_ptr<FunctionType>>(&Type))
    return declaredTypeText((*Function)->argumentType) + " -> " +
           declaredTypeText((*Function)->returnType);
  if (const auto *Named = std::get_if<std::shared_ptr<NamedType>>(&Type))
    return *Named && !(*Named)->name.empty() ? (*Named)->name : "Unknown";
  if (const auto *Collection =
          std::get_if<std::shared_ptr<SingleItemCollectionType>>(&Type))
    return std::string((*Collection)->kind == SingleItemCollectionType::Seq
                           ? "Seq "
                           : "Set ") +
           declaredTypeText((*Collection)->valueType);
  if (const auto *Dict =
          std::get_if<std::shared_ptr<DictCollectionType>>(&Type))
    return "Dict " + declaredTypeText((*Dict)->keyType) + " " +
           declaredTypeText((*Dict)->valueType);
  if (const auto *Product = std::get_if<std::shared_ptr<ProductType>>(&Type)) {
    std::string Result = "(";
    for (std::size_t Index = 0; Index < (*Product)->types.size(); ++Index) {
      if (Index != 0)
        Result += ", ";
      Result += declaredTypeText((*Product)->types[Index]);
    }
    return Result + ")";
  }
  return "Unknown";
}

OwnershipKind ownershipFromType(std::string_view Type) {
  if (Type == "Linear" || Type.starts_with("Linear ") ||
      Type.find(" Linear") != std::string_view::npos)
    return OwnershipKind::Linear;
  return Type.empty() ? OwnershipKind::Unknown : OwnershipKind::Unrestricted;
}

} // namespace

std::string_view symbolKindName(SymbolKind Kind) noexcept {
  switch (Kind) {
  case SymbolKind::Variable:
    return "variable";
  case SymbolKind::Function:
    return "function";
  case SymbolKind::Namespace:
    return "namespace";
  case SymbolKind::Type:
    return "type";
  case SymbolKind::Interface:
    return "interface";
  case SymbolKind::Method:
    return "method";
  case SymbolKind::Instance:
    return "instance";
  }
  return "variable";
}

std::string_view ownershipKindName(OwnershipKind Kind) noexcept {
  switch (Kind) {
  case OwnershipKind::Unknown:
    return "unknown";
  case OwnershipKind::Unrestricted:
    return "unrestricted";
  case OwnershipKind::Linear:
    return "linear";
  }
  return "unknown";
}

struct SemanticModel::Impl final {
  std::shared_ptr<const SourceManager> Sources;
  SourceId Source;
  std::string_view Text;
  compiler::typechecker::TypeChecker *TypeChecker = nullptr;
  compiler::DiagnosticEngine *Diagnostics = nullptr;
  std::vector<SemanticOccurrence> Occurrences;
  std::vector<SemanticDiagnostic> DiagnosticRecords;
  std::unordered_map<const ast::AstNode *, NodeSemantics> NodeFacts;
  SourceRange RootRange = SourceRange::unknown();
  std::optional<NodeSemantics> RootFacts;
  std::vector<std::unordered_map<std::string, BindingId>> BindingScopes;
  std::unordered_map<std::string, BindingId> GlobalBindings;
  std::uint64_t NextBindingValue = 1;

  Impl(std::shared_ptr<SourceManager> Sources, SourceId Source,
       ast::AstNode *Root, compiler::typechecker::TypeChecker *TypeChecker,
       compiler::DiagnosticEngine *Diagnostics)
      : Sources(std::move(Sources)), Source(Source), TypeChecker(TypeChecker),
        Diagnostics(Diagnostics) {
    if (!this->Sources)
      throw std::invalid_argument("SemanticModel requires a SourceManager");
    Text = this->Sources->text(Source);
    BindingScopes.emplace_back();
    walk(Root);
    if (Root) {
      RootRange = Root->Range;
      if (const auto Iterator = NodeFacts.find(Root);
          Iterator != NodeFacts.end())
        RootFacts = Iterator->second;
    }
    propagateOrigins();
    captureDiagnostics();
    this->TypeChecker = nullptr;
    this->Diagnostics = nullptr;
  }

  [[nodiscard]] BindingId nextBinding() noexcept {
    return BindingId(NextBindingValue++);
  }

  [[nodiscard]] BindingId globalBinding(std::string Key) {
    const auto [Iterator, Inserted] =
        GlobalBindings.try_emplace(std::move(Key), BindingId());
    if (Inserted)
      Iterator->second = nextBinding();
    return Iterator->second;
  }

  void pushScope() { BindingScopes.emplace_back(); }

  void popScope() {
    if (BindingScopes.size() > 1)
      BindingScopes.pop_back();
  }

  [[nodiscard]] BindingId lookupBinding(const std::string &Name) const {
    for (auto Scope = BindingScopes.rbegin(); Scope != BindingScopes.rend();
         ++Scope) {
      if (const auto Found = Scope->find(Name); Found != Scope->end())
        return Found->second;
    }
    return {};
  }

  [[nodiscard]] NodeSemantics makeFacts(const ast::AstNode *Node) const {
    NodeSemantics Facts;
    if (!Node || !TypeChecker)
      return Facts;
    if (auto *Type = TypeChecker->type_of(const_cast<ast::AstNode *>(Node))) {
      const auto Zoned = TypeChecker->zonk(Type);
      Facts.InferredType = compiler::typechecker::pretty_print(Zoned);
      const auto EffectInfo = TypeChecker->effect_row_info(Zoned);
      Facts.Effects = "{";
      for (std::size_t Index = 0; Index < EffectInfo.ops.size(); ++Index) {
        if (Index != 0)
          Facts.Effects += ",";
        Facts.Effects += EffectInfo.ops[Index];
      }
      if (EffectInfo.open_rest) {
        if (!EffectInfo.ops.empty())
          Facts.Effects += " | ";
        else
          Facts.Effects += "|";
        Facts.Effects += "r";
      }
      Facts.Effects += "}";
      Facts.Ownership = ownershipFromType(Facts.InferredType);
    }
    return Facts;
  }

  [[nodiscard]] const NodeSemantics &recordFacts(const ast::AstNode *Node) {
    const auto [Iterator, Inserted] = NodeFacts.try_emplace(Node);
    if (Inserted)
      Iterator->second = makeFacts(Node);
    return Iterator->second;
  }

  [[nodiscard]] SourceRange rangeAt(std::size_t Offset,
                                    std::size_t Length) const {
    std::size_t Line = 1;
    std::size_t Column = 1;
    for (std::size_t Index = 0; Index < Offset && Index < Text.size();
         ++Index) {
      if (Text[Index] == '\n') {
        ++Line;
        Column = 1;
      } else {
        ++Column;
      }
    }
    return SourceRange{Source, Line, Column, Offset, Length};
  }

  void addOccurrence(std::string Name, SourceRange Range, bool IsDefinition,
                     SymbolKind Kind, ast::AstNode *Node,
                     std::string Detail = {}, std::string Container = {},
                     std::string TypeOverride = {}) {
    if (Name.empty())
      return;
    SemanticOccurrence Occurrence;
    Occurrence.Name = std::move(Name);
    Occurrence.Range = Range;
    Occurrence.IsDefinition = IsDefinition;
    Occurrence.Kind = Kind;
    Occurrence.Detail = std::move(Detail);
    Occurrence.Container = std::move(Container);
    const std::string FallbackKey = std::string(symbolKindName(Kind)) + ":" +
                                    Occurrence.Container + ":" +
                                    Occurrence.Name;
    if (IsDefinition) {
      if (Kind == SymbolKind::Method || BindingScopes.size() == 1)
        Occurrence.Binding = globalBinding(FallbackKey);
      else
        Occurrence.Binding = nextBinding();
      BindingScopes.back()[Occurrence.Name] = Occurrence.Binding;
    } else {
      Occurrence.Binding = lookupBinding(Occurrence.Name);
      if (!Occurrence.Binding.isValid())
        Occurrence.Binding = globalBinding(FallbackKey);
    }
    if (Node)
      Occurrence.Facts = recordFacts(Node);
    if (!TypeOverride.empty()) {
      Occurrence.Facts.InferredType = std::move(TypeOverride);
      Occurrence.Facts.Ownership =
          ownershipFromType(Occurrence.Facts.InferredType);
    }
    Occurrences.push_back(std::move(Occurrence));
  }

  void addOccurrenceAt(std::string Name, std::size_t Offset, bool IsDefinition,
                       SymbolKind Kind, std::string Detail,
                       std::string Container, std::string Type) {
    const std::size_t Length = Name.size();
    addOccurrence(std::move(Name), rangeAt(Offset, Length), IsDefinition, Kind,
                  nullptr, std::move(Detail), std::move(Container),
                  std::move(Type));
  }

  void markOrigin(const std::string &Module, const std::string &ExportName) {
    if (Occurrences.empty() || Module.empty())
      return;
    Occurrences.back().OriginModule = Module;
    Occurrences.back().OriginName = ExportName;
  }

  [[nodiscard]] std::string resolveModuleAlias(const std::string &Name) const {
    const BindingId Binding = lookupBinding(Name);
    for (const auto &Occurrence : Occurrences) {
      if (Occurrence.IsDefinition && Occurrence.Name == Name &&
          !Occurrence.OriginModule.empty() &&
          (!Binding.isValid() || Occurrence.Binding == Binding))
        return Occurrence.OriginModule;
    }
    return Name;
  }

  void propagateOrigins() {
    std::unordered_map<std::uint64_t, std::pair<std::string, std::string>>
        Origins;
    for (const auto &Occurrence : Occurrences) {
      if (Occurrence.IsDefinition && !Occurrence.OriginModule.empty())
        Origins[Occurrence.Binding.value()] = {Occurrence.OriginModule,
                                               Occurrence.OriginName};
    }
    for (auto &Occurrence : Occurrences) {
      if (Occurrence.IsDefinition || !Occurrence.OriginModule.empty())
        continue;
      if (const auto Iterator = Origins.find(Occurrence.Binding.value());
          Iterator != Origins.end()) {
        Occurrence.OriginModule = Iterator->second.first;
        Occurrence.OriginName = Iterator->second.second;
      }
    }
  }

  void captureDiagnostics() {
    if (!Diagnostics)
      return;
    for (const auto &Record : Diagnostics->records()) {
      SemanticDiagnostic Diagnostic;
      Diagnostic.Range = Record.Range;
      Diagnostic.Severity = Record.level == compiler::DiagLevel::Error     ? 1
                            : Record.level == compiler::DiagLevel::Warning ? 2
                                                                           : 3;
      if (Record.code)
        Diagnostic.Code = compiler::error_code_str(*Record.code);
      Diagnostic.Message = Record.message;
      DiagnosticRecords.push_back(std::move(Diagnostic));
    }
  }

  void walkFunction(ast::FunctionExpr *Function, SymbolKind Kind,
                    std::string Detail = {}, std::string Container = {}) {
    if (!Function)
      return;
    if (!Function->name.empty() &&
        (Kind == SymbolKind::Method || BindingScopes.size() == 1 ||
         !lookupBinding(Function->name).isValid()))
      addOccurrence(Function->name, Function->Range, true, Kind, Function,
                    std::move(Detail), std::move(Container));
    pushScope();
    for (auto *Pattern : Function->patterns)
      walk(Pattern, true);
    for (auto *Body : Function->bodies) {
      if (auto *Guarded = dynamic_cast<ast::BodyWithGuards *>(Body)) {
        walk(Guarded->guard);
        walk(Guarded->expr);
      } else if (auto *Unguarded =
                     dynamic_cast<ast::BodyWithoutGuards *>(Body)) {
        walk(Unguarded->expr);
      }
    }
    popScope();
  }

  void walk(ast::AstNode *Node, bool InPattern = false);
};

void SemanticModel::Impl::walk(ast::AstNode *Node, bool InPattern) {
  if (!Node)
    return;
  (void)recordFacts(Node);
  switch (Node->get_type()) {
  case ast::AST_IDENTIFIER_EXPR: {
    auto *Identifier = static_cast<ast::IdentifierExpr *>(Node);
    if (Identifier->name)
      addOccurrence(Identifier->name->value, Identifier->Range, InPattern,
                    SymbolKind::Variable, Node);
    return;
  }
  case ast::AST_NAME_EXPR: {
    auto *Name = static_cast<ast::NameExpr *>(Node);
    addOccurrence(Name->value, Name->Range, InPattern, SymbolKind::Variable,
                  Node);
    return;
  }
  case ast::AST_PATTERN_VALUE: {
    auto *Pattern = static_cast<ast::PatternValue *>(Node);
    if (auto *Identifier = std::get_if<ast::IdentifierExpr *>(&Pattern->expr)) {
      if (*Identifier && (*Identifier)->name)
        addOccurrence((*Identifier)->name->value, (*Identifier)->Range, true,
                      SymbolKind::Variable, *Identifier);
    }
    return;
  }
  case ast::AST_AS_DATA_STRUCTURE_PATTERN: {
    auto *Pattern = static_cast<ast::AsDataStructurePattern *>(Node);
    if (Pattern->identifier && Pattern->identifier->name)
      addOccurrence(Pattern->identifier->name->value,
                    Pattern->identifier->Range, true, SymbolKind::Variable,
                    Pattern->identifier);
    walk(Pattern->pattern, true);
    return;
  }
  case ast::AST_TUPLE_PATTERN: {
    auto *Pattern = static_cast<ast::TuplePattern *>(Node);
    for (auto *Item : Pattern->patterns)
      walk(Item, true);
    return;
  }
  case ast::AST_SEQ_PATTERN: {
    auto *Pattern = static_cast<ast::SeqPattern *>(Node);
    for (auto *Item : Pattern->patterns)
      walk(Item, true);
    return;
  }
  case ast::AST_HEAD_TAILS_PATTERN: {
    auto *Pattern = static_cast<ast::HeadTailsPattern *>(Node);
    for (auto *Head : Pattern->heads)
      walk(Head, true);
    walk(Pattern->tail, true);
    return;
  }
  case ast::AST_TAILS_HEAD_PATTERN: {
    auto *Pattern = static_cast<ast::TailsHeadPattern *>(Node);
    walk(Pattern->tail, true);
    for (auto *Head : Pattern->heads)
      walk(Head, true);
    return;
  }
  case ast::AST_HEAD_TAILS_HEAD_PATTERN: {
    auto *Pattern = static_cast<ast::HeadTailsHeadPattern *>(Node);
    for (auto *Head : Pattern->left)
      walk(Head, true);
    walk(Pattern->tail, true);
    for (auto *Head : Pattern->right)
      walk(Head, true);
    return;
  }
  case ast::AST_DICT_PATTERN: {
    auto *Pattern = static_cast<ast::DictPattern *>(Node);
    for (auto &Item : Pattern->keyValuePairs) {
      walk(Item.first, true);
      walk(Item.second, true);
    }
    return;
  }
  case ast::AST_RECORD_PATTERN: {
    auto *Pattern = static_cast<ast::RecordPattern *>(Node);
    for (auto &Item : Pattern->items) {
      walk(Item.first, false);
      walk(Item.second, true);
    }
    return;
  }
  case ast::AST_OR_PATTERN: {
    auto *Pattern = static_cast<ast::OrPattern *>(Node);
    for (auto &Item : Pattern->patterns)
      walk(Item.get(), true);
    return;
  }
  case ast::AST_CONSTRUCTOR_PATTERN: {
    auto *Pattern = static_cast<ast::ConstructorPattern *>(Node);
    addOccurrence(Pattern->constructor_name, Pattern->Range, false,
                  SymbolKind::Function, Node);
    for (auto *Item : Pattern->sub_patterns)
      walk(Item, true);
    return;
  }
  case ast::AST_TYPED_PATTERN: {
    auto *Pattern = static_cast<ast::TypedPattern *>(Node);
    addOccurrence(Pattern->binding_name, Pattern->Range, true,
                  SymbolKind::Variable, Node);
    return;
  }
  case ast::AST_FUNCTION_EXPR: {
    auto *Function = static_cast<ast::FunctionExpr *>(Node);
    walkFunction(Function, SymbolKind::Function);
    return;
  }
  case ast::AST_MODULE_DECL: {
    auto *Module = static_cast<ast::ModuleDecl *>(Node);
    if (Module->fqn && Module->fqn->moduleName)
      addOccurrence(Module->fqn->moduleName->value, Module->fqn->Range, true,
                    SymbolKind::Namespace, Module);
    for (auto *Function : Module->functions)
      walk(Function);
    for (auto *Declaration : Module->adt_declarations)
      walk(Declaration);
    for (auto *Trait : Module->trait_declarations)
      walk(Trait);
    for (auto *Instance : Module->instance_declarations)
      walk(Instance);
    for (auto *External : Module->extern_declarations)
      walk(External);
    return;
  }
  case ast::AST_ADT_DECL: {
    auto *Declaration = static_cast<ast::AdtDeclNode *>(Node);
    addOccurrence(Declaration->name, Declaration->Range, true, SymbolKind::Type,
                  Node);
    for (auto *Variant : Declaration->variants) {
      if (Variant)
        addOccurrence(Variant->name, Variant->Range, true, SymbolKind::Function,
                      Variant);
    }
    return;
  }
  case ast::AST_TRAIT_DECL: {
    auto *Trait = static_cast<ast::TraitDeclNode *>(Node);
    std::string Head = "trait " + Trait->name;
    for (const auto &Parameter : Trait->type_params)
      Head += " " + Parameter;
    if (!Trait->superclasses.empty()) {
      Head += " where ";
      for (std::size_t Index = 0; Index < Trait->superclasses.size(); ++Index) {
        if (Index != 0)
          Head += ", ";
        Head += Trait->superclasses[Index].first + " " +
                Trait->superclasses[Index].second;
      }
    }
    std::size_t TraitOffset = Text.find(Trait->name, Trait->Range.Offset);
    if (TraitOffset == std::string_view::npos)
      TraitOffset = Trait->Range.Offset;
    addOccurrenceAt(Trait->name, TraitOffset, true, SymbolKind::Interface, Head,
                    {}, Head);
    std::size_t Cursor = TraitOffset + Trait->name.size();
    for (const auto &Method : Trait->methods) {
      const std::size_t Found = Text.find(Method.name, Cursor);
      if (Found == std::string_view::npos)
        continue;
      addOccurrenceAt(Method.name, Found, true, SymbolKind::Method, Head,
                      Trait->name, declaredTypeText(Method.type_signature));
      Cursor = Found + Method.name.size();
    }
    return;
  }
  case ast::AST_INSTANCE_DECL: {
    auto *Instance = static_cast<ast::InstanceDeclNode *>(Node);
    std::string Head = "instance ";
    for (std::size_t Index = 0; Index < Instance->constraints.size(); ++Index) {
      if (Index != 0)
        Head += ", ";
      Head += Instance->constraints[Index].first + " " +
              Instance->constraints[Index].second;
    }
    if (!Instance->constraints.empty())
      Head += " => ";
    Head += Instance->trait_name;
    for (const auto &TypeName : Instance->type_names)
      Head += " " + TypeName;
    const std::string Name = Head.substr(std::string("instance ").size());
    addOccurrence(Instance->trait_name, Instance->Range, false,
                  SymbolKind::Interface, Node);
    addOccurrence(Name, Instance->Range, true, SymbolKind::Instance, Node, Head,
                  Instance->trait_name, Head);
    for (auto *Function : Instance->methods)
      walkFunction(Function, SymbolKind::Method, Head, Head);
    return;
  }
  case ast::AST_EXTERN_DECL: {
    auto *External = static_cast<ast::ExternDeclExpr *>(Node);
    addOccurrence(External->name, External->Range, true, SymbolKind::Function,
                  Node);
    walk(External->body);
    return;
  }
  case ast::AST_LET_EXPR: {
    auto *Let = static_cast<ast::LetExpr *>(Node);
    pushScope();
    for (auto *Alias : Let->aliases) {
      if (auto *Lambda = dynamic_cast<ast::LambdaAlias *>(Alias)) {
        if (Lambda->name)
          addOccurrence(Lambda->name->value, Lambda->name->Range, true,
                        SymbolKind::Function, Lambda->lambda);
      }
    }
    for (auto *Alias : Let->aliases) {
      if (auto *Value = dynamic_cast<ast::ValueAlias *>(Alias)) {
        walk(Value->expr);
        if (Value->identifier && Value->identifier->name)
          addOccurrence(Value->identifier->name->value,
                        Value->identifier->Range, true, SymbolKind::Variable,
                        Value->identifier);
      } else if (auto *Lambda = dynamic_cast<ast::LambdaAlias *>(Alias)) {
        walk(Lambda->lambda);
      } else if (auto *Pattern = dynamic_cast<ast::PatternAlias *>(Alias)) {
        walk(Pattern->expr);
        walk(Pattern->pattern, true);
      }
    }
    walk(Let->expr);
    popScope();
    return;
  }
  case ast::AST_APPLY_EXPR: {
    auto *Apply = static_cast<ast::ApplyExpr *>(Node);
    walk(Apply->call);
    for (auto &Argument : Apply->args) {
      if (auto *Expression = std::get_if<ast::ExprNode *>(&Argument))
        walk(*Expression);
      else if (auto *Value = std::get_if<ast::ValueExpr *>(&Argument))
        walk(*Value);
    }
    return;
  }
  case ast::AST_IF_EXPR: {
    auto *If = static_cast<ast::IfExpr *>(Node);
    walk(If->condition);
    walk(If->thenExpr);
    walk(If->elseExpr);
    return;
  }
  case ast::AST_CASE_EXPR: {
    auto *Case = static_cast<ast::CaseExpr *>(Node);
    const compiler::pattern_analysis::ConstructorCatalog Constructors{
        [](std::string_view)
            -> std::optional<compiler::pattern_analysis::ConstructorInfo> {
          return std::nullopt;
        },
        [](std::string_view) { return std::vector<std::string>{}; }};
    const auto Analysis =
        compiler::pattern_analysis::analyze_case(*Case, Constructors);
    if (Diagnostics) {
      for (const std::size_t Index : Analysis.unreachable_clauses) {
        if (Index < Case->clauses.size() && Case->clauses[Index])
          Diagnostics->warning(
              Case->clauses[Index]->Range,
              "unreachable pattern: earlier unguarded arms already cover "
              "every value it can match",
              compiler::WarningFlag::OverlappingPatterns);
      }
    }
    walk(Case->expr);
    for (auto *Clause : Case->clauses) {
      if (!Clause)
        continue;
      pushScope();
      walk(Clause->pattern, true);
      walk(Clause->guard);
      walk(Clause->body);
      popScope();
    }
    return;
  }
  case ast::AST_DO_EXPR: {
    auto *Do = static_cast<ast::DoExpr *>(Node);
    for (auto *Step : Do->steps)
      walk(Step);
    return;
  }
  case ast::AST_WITH_EXPR: {
    auto *With = static_cast<ast::WithExpr *>(Node);
    walk(With->contextExpr);
    pushScope();
    if (With->name)
      addOccurrence(With->name->value, With->name->Range, true,
                    SymbolKind::Variable, With->name);
    walk(With->bodyExpr);
    popScope();
    return;
  }
  case ast::AST_TRY_CATCH_EXPR: {
    auto *Try = static_cast<ast::TryCatchExpr *>(Node);
    walk(Try->tryExpr);
    walk(Try->catchExpr);
    return;
  }
  case ast::AST_CATCH_EXPR: {
    auto *Catch = static_cast<ast::CatchExpr *>(Node);
    for (auto *Pattern : Catch->patterns)
      walk(Pattern);
    return;
  }
  case ast::AST_CATCH_PATTERN_EXPR: {
    auto *Pattern = static_cast<ast::CatchPatternExpr *>(Node);
    pushScope();
    walk(Pattern->matchPattern, true);
    if (auto *Unguarded =
            std::get_if<ast::PatternWithoutGuards *>(&Pattern->pattern)) {
      if (*Unguarded)
        walk((*Unguarded)->expr);
    } else if (auto *Guarded =
                   std::get_if<std::vector<ast::PatternWithGuards *>>(
                       &Pattern->pattern)) {
      for (auto *Item : *Guarded) {
        if (!Item)
          continue;
        walk(Item->guard);
        walk(Item->expr);
      }
    }
    popScope();
    return;
  }
  case ast::AST_IMPORT_EXPR: {
    auto *Import = static_cast<ast::ImportExpr *>(Node);
    pushScope();
    for (auto *Clause : Import->clauses)
      walk(Clause);
    walk(Import->expr);
    popScope();
    return;
  }
  case ast::AST_FUNCTIONS_IMPORT: {
    auto *Import = static_cast<ast::FunctionsImport *>(Node);
    const std::string Module =
        Import->fromFqn ? Import->fromFqn->to_string() : "";
    if (Import->fromFqn)
      walk(Import->fromFqn);
    for (auto *Alias : Import->aliases) {
      if (!Alias)
        continue;
      const std::string Exported =
          Alias->name ? Alias->name->value : std::string();
      if (Alias->alias) {
        addOccurrence(Alias->alias->value, Alias->alias->Range, true,
                      SymbolKind::Function, Alias);
        markOrigin(Module, Exported.empty() ? Alias->alias->value : Exported);
      } else if (Alias->name) {
        addOccurrence(Alias->name->value, Alias->name->Range, true,
                      SymbolKind::Function, Alias);
        markOrigin(Module, Alias->name->value);
      }
    }
    return;
  }
  case ast::AST_MODULE_IMPORT: {
    auto *Import = static_cast<ast::ModuleImport *>(Node);
    const std::string Module = Import->fqn ? Import->fqn->to_string() : "";
    if (Import->fqn)
      walk(Import->fqn);
    if (Import->name) {
      addOccurrence(Import->name->value, Import->name->Range, true,
                    SymbolKind::Namespace, Import);
      markOrigin(Module, "");
    }
    return;
  }
  case ast::AST_FQN_EXPR: {
    auto *Fqn = static_cast<ast::FqnExpr *>(Node);
    const std::string Name = Fqn->to_string();
    addOccurrence(Name, Fqn->Range, false, SymbolKind::Namespace, Fqn);
    markOrigin(Name, "");
    return;
  }
  case ast::AST_TUPLE_EXPR: {
    auto *Tuple = static_cast<ast::TupleExpr *>(Node);
    for (auto *Value : Tuple->values)
      walk(Value);
    return;
  }
  case ast::AST_VALUES_SEQUENCE_EXPR: {
    auto *Sequence = static_cast<ast::ValuesSequenceExpr *>(Node);
    for (auto *Value : Sequence->values)
      walk(Value);
    return;
  }
  case ast::AST_RANGE_SEQUENCE_EXPR: {
    auto *Range = static_cast<ast::RangeSequenceExpr *>(Node);
    walk(Range->start);
    walk(Range->end);
    walk(Range->step);
    return;
  }
  case ast::AST_SET_EXPR: {
    auto *Set = static_cast<ast::SetExpr *>(Node);
    for (auto *Value : Set->values)
      walk(Value);
    return;
  }
  case ast::AST_DICT_EXPR: {
    auto *Dictionary = static_cast<ast::DictExpr *>(Node);
    for (auto &Item : Dictionary->values) {
      walk(Item.first);
      walk(Item.second);
    }
    return;
  }
  case ast::AST_RECORD_INSTANCE_EXPR: {
    auto *Record = static_cast<ast::RecordInstanceExpr *>(Node);
    walk(Record->recordType);
    for (auto &Item : Record->items) {
      walk(Item.first);
      walk(Item.second);
    }
    return;
  }
  case ast::AST_RECORD_LITERAL_EXPR: {
    auto *Record = static_cast<ast::RecordLiteralExpr *>(Node);
    for (auto &Field : Record->fields)
      walk(Field.second);
    return;
  }
  case ast::AST_SEQ_GENERATOR_EXPR: {
    auto *Generator = static_cast<ast::SeqGeneratorExpr *>(Node);
    auto *Extractor = dynamic_cast<ast::ValueCollectionExtractorExpr *>(
        Generator->collectionExtractor);
    if (!Extractor) {
      walk(Generator->collectionExtractor);
      walk(Generator->reducerExpr);
      walk(Generator->stepExpression);
      return;
    }
    walk(Extractor->collection);
    pushScope();
    if (auto *Identifier =
            std::get_if<ast::IdentifierExpr *>(&Extractor->expr)) {
      if (*Identifier && (*Identifier)->name)
        addOccurrence((*Identifier)->name->value, (*Identifier)->Range, true,
                      SymbolKind::Variable, *Identifier);
    }
    walk(Extractor->condition);
    walk(Generator->reducerExpr);
    walk(Generator->stepExpression);
    popScope();
    return;
  }
  case ast::AST_SET_GENERATOR_EXPR: {
    auto *Generator = static_cast<ast::SetGeneratorExpr *>(Node);
    auto *Extractor = dynamic_cast<ast::ValueCollectionExtractorExpr *>(
        Generator->collectionExtractor);
    if (!Extractor) {
      walk(Generator->collectionExtractor);
      walk(Generator->reducerExpr);
      walk(Generator->stepExpression);
      return;
    }
    walk(Extractor->collection);
    pushScope();
    if (auto *Identifier =
            std::get_if<ast::IdentifierExpr *>(&Extractor->expr)) {
      if (*Identifier && (*Identifier)->name)
        addOccurrence((*Identifier)->name->value, (*Identifier)->Range, true,
                      SymbolKind::Variable, *Identifier);
    }
    walk(Extractor->condition);
    walk(Generator->reducerExpr);
    walk(Generator->stepExpression);
    popScope();
    return;
  }
  case ast::AST_DICT_GENERATOR_EXPR: {
    auto *Generator = static_cast<ast::DictGeneratorExpr *>(Node);
    auto *Extractor = dynamic_cast<ast::KeyValueCollectionExtractorExpr *>(
        Generator->collectionExtractor);
    if (!Extractor) {
      walk(Generator->collectionExtractor);
      walk(Generator->reducerExpr);
      walk(Generator->stepExpression);
      return;
    }
    walk(Extractor->collection);
    pushScope();
    const auto Bind = [&](ast::IdentifierOrUnderscore &Value) {
      if (auto *Identifier = std::get_if<ast::IdentifierExpr *>(&Value)) {
        if (*Identifier && (*Identifier)->name)
          addOccurrence((*Identifier)->name->value, (*Identifier)->Range, true,
                        SymbolKind::Variable, *Identifier);
      }
    };
    Bind(Extractor->keyExpr);
    Bind(Extractor->valueExpr);
    walk(Extractor->condition);
    walk(Generator->reducerExpr);
    walk(Generator->stepExpression);
    popScope();
    return;
  }
  case ast::AST_VALUE_COLLECTION_EXTRACTOR_EXPR: {
    auto *Extractor = static_cast<ast::ValueCollectionExtractorExpr *>(Node);
    walk(Extractor->collection);
    pushScope();
    if (auto *Identifier =
            std::get_if<ast::IdentifierExpr *>(&Extractor->expr)) {
      if (*Identifier && (*Identifier)->name)
        addOccurrence((*Identifier)->name->value, (*Identifier)->Range, true,
                      SymbolKind::Variable, *Identifier);
    }
    walk(Extractor->condition);
    popScope();
    return;
  }
  case ast::AST_KEY_VALUE_COLLECTION_EXTRACTOR_EXPR: {
    auto *Extractor = static_cast<ast::KeyValueCollectionExtractorExpr *>(Node);
    walk(Extractor->collection);
    pushScope();
    const auto Bind = [&](ast::IdentifierOrUnderscore &Value) {
      if (auto *Identifier = std::get_if<ast::IdentifierExpr *>(&Value)) {
        if (*Identifier && (*Identifier)->name)
          addOccurrence((*Identifier)->name->value, (*Identifier)->Range, true,
                        SymbolKind::Variable, *Identifier);
      }
    };
    Bind(Extractor->keyExpr);
    Bind(Extractor->valueExpr);
    walk(Extractor->condition);
    popScope();
    return;
  }
  case ast::AST_PERFORM_EXPR: {
    auto *Perform = static_cast<ast::PerformExpr *>(Node);
    addOccurrence(Perform->operation_name, Perform->Range, false,
                  SymbolKind::Function, Node);
    for (auto *Argument : Perform->args)
      walk(Argument);
    return;
  }
  case ast::AST_HANDLE_EXPR: {
    auto *Handle = static_cast<ast::HandleExpr *>(Node);
    walk(Handle->body);
    for (auto *Clause : Handle->clauses) {
      if (!Clause)
        continue;
      pushScope();
      for (const auto &Name : Clause->arg_names)
        addOccurrence(Name, Clause->Range, true, SymbolKind::Variable, Clause);
      if (!Clause->resume_name.empty())
        addOccurrence(Clause->resume_name, Clause->Range, true,
                      SymbolKind::Function, Clause);
      if (!Clause->return_binding.empty())
        addOccurrence(Clause->return_binding, Clause->Range, true,
                      SymbolKind::Variable, Clause);
      walk(Clause->body);
      popScope();
    }
    return;
  }
  case ast::AST_RAISE_EXPR:
    walk(static_cast<ast::RaiseExpr *>(Node)->value);
    return;
  case ast::AST_LOGICAL_NOT_OP_EXPR:
    walk(static_cast<ast::LogicalNotOpExpr *>(Node)->expr);
    return;
  case ast::AST_BINARY_NOT_OP_EXPR:
    walk(static_cast<ast::BinaryNotOpExpr *>(Node)->expr);
    return;
  case ast::AST_FIELD_ACCESS_EXPR: {
    auto *Field = static_cast<ast::FieldAccessExpr *>(Node);
    walk(Field->identifier);
    walk(Field->name);
    return;
  }
  case ast::AST_FIELD_UPDATE_EXPR: {
    auto *Field = static_cast<ast::FieldUpdateExpr *>(Node);
    walk(Field->identifier);
    for (auto &Update : Field->updates) {
      walk(Update.first);
      walk(Update.second);
    }
    return;
  }
  case ast::AST_MODULE_CALL: {
    auto *Call = static_cast<ast::ModuleCall *>(Node);
    std::string Module;
    if (auto *Fqn = std::get_if<ast::FqnExpr *>(&Call->fqn)) {
      if (*Fqn) {
        Module = (*Fqn)->to_string();
        walk(*Fqn);
      }
    } else if (auto *Expression = std::get_if<ast::ExprNode *>(&Call->fqn)) {
      if (*Expression) {
        if (auto *Identifier = dynamic_cast<ast::IdentifierExpr *>(*Expression))
          if (Identifier->name)
            Module = resolveModuleAlias(Identifier->name->value);
        walk(*Expression);
      }
    }
    if (Call->funName) {
      addOccurrence(Call->funName->value, Call->funName->Range, false,
                    SymbolKind::Function, Node);
      markOrigin(Module, Call->funName->value);
    }
    return;
  }
  case ast::AST_NAME_CALL: {
    auto *Call = static_cast<ast::NameCall *>(Node);
    if (Call->name)
      addOccurrence(Call->name->value, Call->name->Range, false,
                    SymbolKind::Function, Node);
    return;
  }
  case ast::AST_EXPR_CALL:
    walk(static_cast<ast::ExprCall *>(Node)->expr);
    return;
  case ast::AST_BINARY_OP_EXPR:
  case ast::AST_ADD_EXPR:
  case ast::AST_SUBTRACT_EXPR:
  case ast::AST_MULTIPLY_EXPR:
  case ast::AST_DIVIDE_EXPR:
  case ast::AST_MODULO_EXPR:
  case ast::AST_POWER_EXPR:
  case ast::AST_EQ_EXPR:
  case ast::AST_NEQ_EXPR:
  case ast::AST_LT_EXPR:
  case ast::AST_LTE_EXPR:
  case ast::AST_GT_EXPR:
  case ast::AST_GTE_EXPR:
  case ast::AST_LOGICAL_AND_EXPR:
  case ast::AST_LOGICAL_OR_EXPR:
  case ast::AST_PIPE_RIGHT_EXPR:
  case ast::AST_PIPE_LEFT_EXPR:
  case ast::AST_IN_EXPR:
  case ast::AST_CONS_LEFT_EXPR:
  case ast::AST_CONS_RIGHT_EXPR:
  case ast::AST_JOIN_EXPR:
  case ast::AST_REMOVE_EXPR:
  case ast::AST_LEFT_SHIFT_EXPR:
  case ast::AST_RIGHT_SHIFT_EXPR:
  case ast::AST_ZEROFILL_RIGHT_SHIFT_EXPR:
  case ast::AST_BITWISE_AND_EXPR:
  case ast::AST_BITWISE_OR_EXPR:
  case ast::AST_BITWISE_XOR_EXPR: {
    auto *Binary = static_cast<ast::BinaryOpExpr *>(Node);
    walk(Binary->left);
    walk(Binary->right);
    return;
  }
  default:
    return;
  }
}

SemanticModel::SemanticModel(std::shared_ptr<SourceManager> Sources,
                             SourceId Source, ast::AstNode *Root,
                             compiler::typechecker::TypeChecker *TypeChecker,
                             compiler::DiagnosticEngine *Diagnostics)
    : Implementation(std::make_unique<Impl>(std::move(Sources), Source, Root,
                                            TypeChecker, Diagnostics)) {}

SemanticModel::~SemanticModel() = default;
SemanticModel::SemanticModel(SemanticModel &&) noexcept = default;
SemanticModel &SemanticModel::operator=(SemanticModel &&) noexcept = default;

const SourceManager &SemanticModel::sourceManager() const noexcept {
  return *Implementation->Sources;
}

SourceId SemanticModel::sourceId() const noexcept {
  return Implementation->Source;
}

std::string_view SemanticModel::sourceText() const noexcept {
  return Implementation->Text;
}

SourceRange SemanticModel::rootRange() const noexcept {
  return Implementation->RootRange;
}

const NodeSemantics *SemanticModel::rootFacts() const noexcept {
  return Implementation->RootFacts ? &*Implementation->RootFacts : nullptr;
}

std::span<const SemanticOccurrence>
SemanticModel::occurrences() const noexcept {
  return Implementation->Occurrences;
}

std::span<const SemanticDiagnostic>
SemanticModel::diagnostics() const noexcept {
  return Implementation->DiagnosticRecords;
}

const SemanticOccurrence *
SemanticModel::occurrenceAt(std::size_t ByteOffset) const noexcept {
  const SemanticOccurrence *Best = nullptr;
  std::size_t BestLength = std::numeric_limits<std::size_t>::max();
  for (const auto &Occurrence : Implementation->Occurrences) {
    const std::size_t End = Occurrence.Range.Offset + Occurrence.Range.Length;
    if (ByteOffset < Occurrence.Range.Offset || ByteOffset >= End)
      continue;
    if (Occurrence.Range.Length < BestLength) {
      BestLength = Occurrence.Range.Length;
      Best = &Occurrence;
    }
  }
  return Best;
}

const SemanticOccurrence *
SemanticModel::definition(BindingId Binding) const noexcept {
  if (!Binding.isValid())
    return nullptr;
  for (const auto &Occurrence : Implementation->Occurrences)
    if (Occurrence.IsDefinition && Occurrence.Binding == Binding)
      return &Occurrence;
  return nullptr;
}

std::vector<const SemanticOccurrence *>
SemanticModel::references(BindingId Binding, bool IncludeDefinition) const {
  std::vector<const SemanticOccurrence *> Result;
  if (!Binding.isValid())
    return Result;
  for (const auto &Occurrence : Implementation->Occurrences) {
    if (Occurrence.Binding == Binding &&
        (IncludeDefinition || !Occurrence.IsDefinition))
      Result.push_back(&Occurrence);
  }
  return Result;
}

const NodeSemantics *
SemanticModel::factsFor(const ast::AstNode *Node) const noexcept {
  const auto Iterator = Implementation->NodeFacts.find(Node);
  return Iterator == Implementation->NodeFacts.end() ? nullptr
                                                     : &Iterator->second;
}

} // namespace yona::semantics
