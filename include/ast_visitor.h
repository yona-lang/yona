#pragma once

#include "yona_export.h"
#include <stdexcept>

// Disable MSVC warning C4251 about STL types in exported class interfaces
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

namespace yona::ast {

// Forward declarations
class AddExpr;
class AdtConstructor;
class AdtDeclNode;
class AliasCall;
class ApplyExpr;
class AsDataStructurePattern;
class BinaryNotOpExpr;
class BitwiseAndExpr;
class BitwiseOrExpr;
class BitwiseXorExpr;
class BodyWithGuards;
class BodyWithoutGuards;
class ByteExpr;
class CaseClause;
class CaseExpr;
class CatchExpr;
class CatchPatternExpr;
class CharacterExpr;
class ConsLeftExpr;
class ConsRightExpr;
class DictExpr;
class DictGeneratorExpr;
class DictGeneratorReducer;
class DictPattern;
class DivideExpr;
class DoExpr;
class EqExpr;
class FalseLiteralExpr;
class FieldAccessExpr;
class FieldUpdateExpr;
class FloatExpr;
class FqnAlias;
class FqnExpr;
class FunctionAlias;
class FunctionDeclaration;
class FunctionExpr;
class FunctionsImport;
class GtExpr;
class GteExpr;
class HeadTailsHeadPattern;
class HeadTailsPattern;
class IfExpr;
class ImportClauseExpr;
class ImportExpr;
class ExternDeclExpr;
class InExpr;
class IntegerExpr;
class JoinExpr;
class KeyValueCollectionExtractorExpr;
class LambdaAlias;
class LeftShiftExpr;
class LetExpr;
class LogicalAndExpr;
class LogicalNotOpExpr;
class LogicalOrExpr;
class LtExpr;
class LteExpr;
class ModuloExpr;
class ModuleCall;
class ExprCall;
class ModuleDecl;
class ModuleImport;
class MultiplyExpr;
class NameCall;
class NameExpr;
class NeqExpr;
class PackageNameExpr;
class PipeLeftExpr;
class PipeRightExpr;
class PatternAlias;
class PatternExpr;
class PatternValue;
class PatternWithGuards;
class PatternWithoutGuards;
class PowerExpr;
class RaiseExpr;
class RemoveExpr;
class RangeSequenceExpr;
class RecordInstanceExpr;
class RecordNode;
class RecordPattern;
class OrPattern;
class RightShiftExpr;
class SeqGeneratorExpr;
class SeqPattern;
class SetExpr;
class SetGeneratorExpr;
class StringExpr;
class SubtractExpr;
class SymbolExpr;
class TailsHeadPattern;
class TrueLiteralExpr;
class TryCatchExpr;
class TupleExpr;
class TuplePattern;
class TypeDeclaration;
class TypeDefinition;
class TypeNode;
class TypeInstance;
class UnderscoreNode;
class UnitExpr;
class ValueAlias;
class ValueCollectionExtractorExpr;
class ValuesSequenceExpr;
class WithExpr;
class ZerofillRightShiftExpr;
class MainNode;
class IdentifierExpr;
class BuiltinTypeNode;
class UserDefinedTypeNode;
class ExprNode;
class AstNode;
class ScopedNode;
class PatternNode;
class ValueExpr;
class SequenceExpr;
class FunctionBody;
class AliasExpr;
class OpExpr;
class BinaryOpExpr;
class TypeNameNode;
class CallExpr;
class GeneratorExpr;
class CollectionExtractorExpr;
class ConstructorPattern;
class TypedPattern;
class RecordLiteralExpr;
class TraitDeclNode;
class InstanceDeclNode;
class EffectDeclNode;
class PerformExpr;
class HandleExpr;
class HandlerClause;

// Templated visitor base class that allows each visitor to specify its own return type
template<typename ResultType>
class AstVisitor {
protected:
  // Helper method to dispatch based on runtime type when we have a base pointer
  // This avoids infinite recursion when accept() is called on base class pointers
  ResultType dispatchVisit(AstNode *node) const;

public:
  virtual ~AstVisitor() = default;
  virtual ResultType visit(AddExpr *node) const = 0;
  virtual ResultType visit(AdtConstructor *node) const = 0;
  virtual ResultType visit(AdtDeclNode *node) const = 0;
  virtual ResultType visit(AliasCall *node) const = 0;
  virtual ResultType visit(ApplyExpr *node) const = 0;
  virtual ResultType visit(AsDataStructurePattern *node) const = 0;
  virtual ResultType visit(BinaryNotOpExpr *node) const = 0;
  virtual ResultType visit(BitwiseAndExpr *node) const = 0;
  virtual ResultType visit(BitwiseOrExpr *node) const = 0;
  virtual ResultType visit(BitwiseXorExpr *node) const = 0;
  virtual ResultType visit(BodyWithGuards *node) const = 0;
  virtual ResultType visit(BodyWithoutGuards *node) const = 0;
  virtual ResultType visit(ByteExpr *node) const = 0;
  virtual ResultType visit(CaseClause *node) const = 0;
  virtual ResultType visit(CaseExpr *node) const = 0;
  virtual ResultType visit(CatchExpr *node) const = 0;
  virtual ResultType visit(CatchPatternExpr *node) const = 0;
  virtual ResultType visit(CharacterExpr *node) const = 0;
  virtual ResultType visit(ConstructorPattern *node) const = 0;
  virtual ResultType visit(ConsLeftExpr *node) const = 0;
  virtual ResultType visit(ConsRightExpr *node) const = 0;
  virtual ResultType visit(DictExpr *node) const = 0;
  virtual ResultType visit(DictGeneratorExpr *node) const = 0;
  virtual ResultType visit(DictGeneratorReducer *node) const = 0;
  virtual ResultType visit(DictPattern *node) const = 0;
  virtual ResultType visit(DivideExpr *node) const = 0;
  virtual ResultType visit(DoExpr *node) const = 0;
  virtual ResultType visit(EqExpr *node) const = 0;
  virtual ResultType visit(FalseLiteralExpr *node) const = 0;
  virtual ResultType visit(FieldAccessExpr *node) const = 0;
  virtual ResultType visit(FieldUpdateExpr *node) const = 0;
  virtual ResultType visit(FloatExpr *node) const = 0;
  virtual ResultType visit(FqnAlias *node) const = 0;
  virtual ResultType visit(FqnExpr *node) const = 0;
  virtual ResultType visit(FunctionAlias *node) const = 0;
  virtual ResultType visit(FunctionDeclaration *node) const = 0;
  virtual ResultType visit(FunctionExpr *node) const = 0;
  virtual ResultType visit(FunctionsImport *node) const = 0;
  virtual ResultType visit(GtExpr *node) const = 0;
  virtual ResultType visit(GteExpr *node) const = 0;
  virtual ResultType visit(HeadTailsHeadPattern *node) const = 0;
  virtual ResultType visit(HeadTailsPattern *node) const = 0;
  virtual ResultType visit(IfExpr *node) const = 0;
  virtual ResultType visit(ImportClauseExpr *node) const = 0;
  virtual ResultType visit(ImportExpr *node) const = 0;
  virtual ResultType visit(ExternDeclExpr *node) const = 0;
  virtual ResultType visit(InExpr *node) const = 0;
  virtual ResultType visit(IntegerExpr *node) const = 0;
  virtual ResultType visit(JoinExpr *node) const = 0;
  virtual ResultType visit(KeyValueCollectionExtractorExpr *node) const = 0;
  virtual ResultType visit(LambdaAlias *node) const = 0;
  virtual ResultType visit(LeftShiftExpr *node) const = 0;
  virtual ResultType visit(LetExpr *node) const = 0;
  virtual ResultType visit(LogicalAndExpr *node) const = 0;
  virtual ResultType visit(LogicalNotOpExpr *node) const = 0;
  virtual ResultType visit(LogicalOrExpr *node) const = 0;
  virtual ResultType visit(LtExpr *node) const = 0;
  virtual ResultType visit(LteExpr *node) const = 0;
  virtual ResultType visit(ModuloExpr *node) const = 0;
  virtual ResultType visit(ModuleCall *node) const = 0;
  virtual ResultType visit(ExprCall *node) const = 0;
  virtual ResultType visit(ModuleDecl *node) const = 0;
  virtual ResultType visit(ModuleImport *node) const = 0;
  virtual ResultType visit(MultiplyExpr *node) const = 0;
  virtual ResultType visit(NameCall *node) const = 0;
  virtual ResultType visit(NameExpr *node) const = 0;
  virtual ResultType visit(NeqExpr *node) const = 0;
  virtual ResultType visit(PackageNameExpr *node) const = 0;
  virtual ResultType visit(PipeLeftExpr *node) const = 0;
  virtual ResultType visit(PipeRightExpr *node) const = 0;
  virtual ResultType visit(PatternAlias *node) const = 0;
  virtual ResultType visit(PatternExpr *node) const = 0;
  virtual ResultType visit(PatternValue *node) const = 0;
  virtual ResultType visit(PatternWithGuards *node) const = 0;
  virtual ResultType visit(PatternWithoutGuards *node) const = 0;
  virtual ResultType visit(PowerExpr *node) const = 0;
  virtual ResultType visit(RaiseExpr *node) const = 0;
  virtual ResultType visit(RemoveExpr *node) const = 0;
  virtual ResultType visit(RangeSequenceExpr *node) const = 0;
  virtual ResultType visit(RecordInstanceExpr *node) const = 0;
  virtual ResultType visit(RecordNode *node) const = 0;
  virtual ResultType visit(RecordPattern *node) const = 0;
  virtual ResultType visit(OrPattern *node) const = 0;
  virtual ResultType visit(RightShiftExpr *node) const = 0;
  virtual ResultType visit(SeqGeneratorExpr *node) const = 0;
  virtual ResultType visit(SeqPattern *node) const = 0;
  virtual ResultType visit(SetExpr *node) const = 0;
  virtual ResultType visit(SetGeneratorExpr *node) const = 0;
  virtual ResultType visit(StringExpr *node) const = 0;
  virtual ResultType visit(SubtractExpr *node) const = 0;
  virtual ResultType visit(SymbolExpr *node) const = 0;
  virtual ResultType visit(TailsHeadPattern *node) const = 0;
  virtual ResultType visit(TrueLiteralExpr *node) const = 0;
  virtual ResultType visit(TryCatchExpr *node) const = 0;
  virtual ResultType visit(TupleExpr *node) const = 0;
  virtual ResultType visit(TuplePattern *node) const = 0;
  virtual ResultType visit(TypeDeclaration *node) const = 0;
  virtual ResultType visit(TypeDefinition *node) const = 0;
  virtual ResultType visit(TypeNode *node) const = 0;
  virtual ResultType visit(TypeInstance *node) const = 0;
  virtual ResultType visit(UnderscoreNode *node) const = 0;
  virtual ResultType visit(UnitExpr *node) const = 0;
  virtual ResultType visit(ValueAlias *node) const = 0;
  virtual ResultType visit(ValueCollectionExtractorExpr *node) const = 0;
  virtual ResultType visit(ValuesSequenceExpr *node) const = 0;
  virtual ResultType visit(WithExpr *node) const = 0;
  virtual ResultType visit(ZerofillRightShiftExpr *node) const = 0;
  virtual ResultType visit(MainNode *node) const = 0;
  virtual ResultType visit(IdentifierExpr *node) const = 0;
  virtual ResultType visit(BuiltinTypeNode *node) const = 0;
  virtual ResultType visit(UserDefinedTypeNode *node) const = 0;
  virtual ResultType visit(TraitDeclNode *node) const = 0;
  virtual ResultType visit(InstanceDeclNode *node) const = 0;
  virtual ResultType visit(EffectDeclNode *node) const = 0;
  virtual ResultType visit(PerformExpr *node) const = 0;
  virtual ResultType visit(HandleExpr *node) const = 0;
  virtual ResultType visit(HandlerClause *node) const = 0;
  virtual ResultType visit(TypedPattern *node) const = 0;
  virtual ResultType visit(RecordLiteralExpr *node) const = 0;
  virtual ResultType visit(ScopedNode *node) const = 0;

  // Default implementations for intermediate classes that dispatch to concrete types
  virtual ResultType visit(AstNode *node) const;
  virtual ResultType visit(ExprNode *node) const;
  virtual ResultType visit(PatternNode *node) const;
  virtual ResultType visit(ValueExpr *node) const;
  virtual ResultType visit(SequenceExpr *node) const;
  virtual ResultType visit(FunctionBody *node) const;
  virtual ResultType visit(AliasExpr *node) const;
  virtual ResultType visit(OpExpr *node) const;
  virtual ResultType visit(BinaryOpExpr *node) const;
  virtual ResultType visit(TypeNameNode *node) const;
  virtual ResultType visit(CallExpr *node) const;
  virtual ResultType visit(GeneratorExpr *node) const;
  virtual ResultType visit(CollectionExtractorExpr *node) const;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif

} // namespace yona::ast
