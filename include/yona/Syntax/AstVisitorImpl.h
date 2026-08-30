#ifndef YONA_SYNTAX_ASTVISITORIMPL_H
#define YONA_SYNTAX_ASTVISITORIMPL_H

#include "yona/Syntax/Ast.h"
#include "yona/Syntax/AstVisitor.h"

// This file contains the implementation of dispatchVisit that requires
// complete type information. Include it in source files that instantiate the
// visitor, not in public headers. Dispatch borrows the node and never extends
// its lifetime; an unrecognized or null node raises std::runtime_error.

namespace yona::ast {

template <typename ResultType>
ResultType AstVisitor<ResultType>::dispatchVisit(AstNode *node) const {
  // First try concrete expression types
  if (auto *n = dynamic_cast<AddExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<SubtractExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<MultiplyExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<DivideExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<ModuloExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<PowerExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<EqExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<NeqExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<LtExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<LteExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<GtExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<GteExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<LogicalAndExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<LogicalOrExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<LogicalNotOpExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<BitwiseAndExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<BitwiseOrExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<BitwiseXorExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<BinaryNotOpExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<LeftShiftExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<RightShiftExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<ZerofillRightShiftExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<IntegerExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<FloatExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<StringExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<CharacterExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<ByteExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<TrueLiteralExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<FalseLiteralExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<UnitExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<SymbolExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<LetExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<IfExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<CaseExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<CaseClause *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<DoExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<WithExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<RaiseExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<TryCatchExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<CatchExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<CatchPatternExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<FunctionDeclaration *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<FunctionExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<ApplyExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<NameCall *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<ModuleCall *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<ExprCall *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<TupleExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<SetExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<SeqGeneratorExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<SetGeneratorExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<DictExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<DictGeneratorExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<DictGeneratorReducer *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<ConsLeftExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<ConsRightExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<RangeSequenceExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<ValuesSequenceExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<InExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<JoinExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<RemoveExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<PatternValue *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<PatternWithoutGuards *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<PatternWithGuards *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<TuplePattern *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<SeqPattern *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<DictPattern *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<RecordPattern *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<OrPattern *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<AsDataStructurePattern *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<HeadTailsPattern *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<TailsHeadPattern *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<HeadTailsHeadPattern *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<UnderscoreNode *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<NameExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<FqnExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<FieldAccessExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<FieldUpdateExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<RecordNode *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<RecordInstanceExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<ModuleDecl *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<ExternDeclExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<ImportExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<ImportClauseExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<ModuleImport *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<FunctionsImport *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<TypeDeclaration *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<TypeDefinition *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<TypeInstance *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<AdtConstructor *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<AdtDeclNode *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<ConstructorPattern *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<TraitDeclNode *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<InstanceDeclNode *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<EffectDeclNode *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<PerformExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<HandleExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<HandlerClause *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<TypedPattern *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<RecordLiteralExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<MainNode *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<FunctionAlias *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<PatternAlias *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<ValueAlias *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<LambdaAlias *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<FqnAlias *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<AliasCall *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<ValueCollectionExtractorExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<KeyValueCollectionExtractorExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<PipeLeftExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<PipeRightExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<BodyWithoutGuards *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<BodyWithGuards *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<IdentifierExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<BuiltinTypeNode *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<UserDefinedTypeNode *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<FunctionBody *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<TypeNameNode *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<TypeNode *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<PatternExpr *>(node))
    return visit(n);
  if (auto *n = dynamic_cast<PackageNameExpr *>(node))
    return visit(n);

  // If we get here, it's an unknown node type
  throw std::runtime_error("AstVisitor::dispatchVisit: Unknown node type");
}

// Default implementations for intermediate classes that dispatch to concrete
// types
template <typename ResultType>
ResultType AstVisitor<ResultType>::visit(AstNode *node) const {
  // Base case - dispatch to concrete type
  return dispatchVisit(node);
}

template <typename ResultType>
ResultType AstVisitor<ResultType>::visit(ExprNode *node) const {
  // ExprNode is intermediate - dispatch to concrete type
  return dispatchVisit(node);
}

template <typename ResultType>
ResultType AstVisitor<ResultType>::visit(PatternNode *node) const {
  // PatternNode is intermediate - dispatch to concrete type
  return dispatchVisit(node);
}

template <typename ResultType>
ResultType AstVisitor<ResultType>::visit(ValueExpr *node) const {
  // ValueExpr is intermediate - dispatch to concrete type
  return dispatchVisit(node);
}

template <typename ResultType>
ResultType AstVisitor<ResultType>::visit(SequenceExpr *node) const {
  // SequenceExpr is intermediate - dispatch to concrete type
  return dispatchVisit(node);
}

template <typename ResultType>
ResultType AstVisitor<ResultType>::visit(FunctionBody *node) const {
  // FunctionBody is intermediate - dispatch to concrete type
  return dispatchVisit(node);
}

template <typename ResultType>
ResultType AstVisitor<ResultType>::visit(AliasExpr *node) const {
  // AliasExpr is intermediate - dispatch to concrete type
  return dispatchVisit(node);
}

template <typename ResultType>
ResultType AstVisitor<ResultType>::visit(OpExpr *node) const {
  // OpExpr is intermediate - dispatch to concrete type
  return dispatchVisit(node);
}

template <typename ResultType>
ResultType AstVisitor<ResultType>::visit(BinaryOpExpr *node) const {
  // BinaryOpExpr is intermediate - dispatch to concrete type
  return dispatchVisit(node);
}

template <typename ResultType>
ResultType AstVisitor<ResultType>::visit(TypeNameNode *node) const {
  // TypeNameNode is intermediate - dispatch to concrete type
  return dispatchVisit(node);
}

template <typename ResultType>
ResultType AstVisitor<ResultType>::visit(CallExpr *node) const {
  // CallExpr is intermediate - dispatch to concrete type
  return dispatchVisit(node);
}

template <typename ResultType>
ResultType AstVisitor<ResultType>::visit(GeneratorExpr *node) const {
  // GeneratorExpr is intermediate - dispatch to concrete type
  return dispatchVisit(node);
}

template <typename ResultType>
ResultType AstVisitor<ResultType>::visit(CollectionExtractorExpr *node) const {
  // CollectionExtractorExpr is intermediate - dispatch to concrete type
  return dispatchVisit(node);
}

// Explicit template instantiation for Type visitor
template class AstVisitor<Type>;

} // namespace yona::ast

#endif /* YONA_SYNTAX_ASTVISITORIMPL_H */
