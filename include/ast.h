#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>
#include <memory>
#include <any>

#include "yona_export.h"
#include "common.h"
#include "types.h"
#include "ast_visitor.h"

#include <ostream>

// Disable MSVC warning C4251 about STL types in exported class interfaces
// This is safe for our use case as both the DLL and clients use the same STL
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

namespace yona::ast {
class FunctionDeclaration;
class AstNode;
class AsDataStructurePattern;
class RecordPattern;
class SeqPattern;
class HeadTailsPattern;
class TailsHeadPattern;
class HeadTailsHeadPattern;
class DictPattern;
class TuplePattern;
class PatternValue;
// AstVisitor is now defined in ast_visitor.h as a template
class TypeDefinition;
class ModuleDecl;
class FunctionExpr;
class AdtConstructor;
class AdtDeclNode;
class ConstructorPattern;
class TraitDeclNode;
class InstanceDeclNode;

using namespace std;
using namespace compiler::types;

enum AstNodeType {
  AST_NODE,
  AST_EXPR_NODE,
  AST_PATTERN_NODE,
  AST_UNDERSCORE_NODE,
  AST_VALUE_EXPR,
  AST_SCOPED_NODE,
  AST_LITERAL_EXPR,
  AST_OP_EXPR,
  AST_BINARY_OP_EXPR,
  AST_ALIAS_EXPR,
  AST_CALL_EXPR,
  AST_IMPORT_CLAUSE_EXPR,
  AST_GENERATOR_EXPR,
  AST_COLLECTION_EXTRACTOR_EXPR,
  AST_SEQUENCE_EXPR,
  AST_FUNCTION_BODY,
  AST_NAME_EXPR,
  AST_IDENTIFIER_EXPR,
  AST_RECORD_NODE,
  AST_TRUE_LITERAL_EXPR,
  AST_FALSE_LITERAL_EXPR,
  AST_FLOAT_EXPR,
  AST_INTEGER_EXPR,
  AST_BYTE_EXPR,
  AST_STRING_EXPR,
  AST_CHARACTER_EXPR,
  AST_UNIT_EXPR,
  AST_TUPLE_EXPR,
  AST_DICT_EXPR,
  AST_VALUES_SEQUENCE_EXPR,
  AST_RANGE_SEQUENCE_EXPR,
  AST_SET_EXPR,
  AST_SYMBOL_EXPR,
  AST_PACKAGE_NAME_EXPR,
  AST_FQN_EXPR,
  AST_FUNCTION_EXPR,
  AST_FUNCTION_DECLARATION,
  AST_MODULE_DECL,
  AST_RECORD_INSTANCE_EXPR,
  AST_BODY_WITH_GUARDS,
  AST_BODY_WITHOUT_GUARDS,
  AST_LOGICAL_NOT_OP_EXPR,
  AST_BINARY_NOT_OP_EXPR,
  AST_POWER_EXPR,
  AST_MULTIPLY_EXPR,
  AST_DIVIDE_EXPR,
  AST_MODULO_EXPR,
  AST_ADD_EXPR,
  AST_SUBTRACT_EXPR,
  AST_LEFT_SHIFT_EXPR,
  AST_RIGHT_SHIFT_EXPR,
  AST_ZEROFILL_RIGHT_SHIFT_EXPR,
  AST_GTE_EXPR,
  AST_LTE_EXPR,
  AST_GT_EXPR,
  AST_LT_EXPR,
  AST_EQ_EXPR,
  AST_NEQ_EXPR,
  AST_CONS_LEFT_EXPR,
  AST_CONS_RIGHT_EXPR,
  AST_JOIN_EXPR,
  AST_BITWISE_AND_EXPR,
  AST_BITWISE_XOR_EXPR,
  AST_BITWISE_OR_EXPR,
  AST_LOGICAL_AND_EXPR,
  AST_LOGICAL_OR_EXPR,
  AST_IN_EXPR,
  AST_PIPE_LEFT_EXPR,
  AST_PIPE_RIGHT_EXPR,
  AST_LET_EXPR,
  AST_IF_EXPR,
  AST_APPLY_EXPR,
  AST_DO_EXPR,
  AST_IMPORT_EXPR,
  AST_RAISE_EXPR,
  AST_WITH_EXPR,
  AST_FIELD_ACCESS_EXPR,
  AST_FIELD_UPDATE_EXPR,
  AST_LAMBDA_ALIAS,
  AST_VALUE_ALIAS,
  AST_PATTERN_ALIAS,
  AST_FQN_ALIAS,
  AST_FUNCTION_ALIAS,
  AST_ALIAS_CALL,
  AST_NAME_CALL,
  AST_MAIN,
  AST_MODULE_CALL,
  AST_EXPR_CALL,
  AST_MODULE_IMPORT,
  AST_FUNCTIONS_IMPORT,
  AST_EXTERN_DECL,
  AST_SEQ_GENERATOR_EXPR,
  AST_SET_GENERATOR_EXPR,
  AST_DICT_GENERATOR_REDUCER,
  AST_DICT_GENERATOR_EXPR,
  AST_UNDERSCORE_PATTERN,
  AST_VALUE_COLLECTION_EXTRACTOR_EXPR,
  AST_KEY_VALUE_COLLECTION_EXTRACTOR_EXPR,
  AST_PATTERN_WITH_GUARDS,
  AST_PATTERN_WITHOUT_GUARDS,
  AST_PATTERN_EXPR,
  AST_CATCH_PATTERN_EXPR,
  AST_CATCH_EXPR,
  AST_TRY_CATCH_EXPR,
  AST_PATTERN_VALUE,
  AST_AS_DATA_STRUCTURE_PATTERN,
  AST_TUPLE_PATTERN,
  AST_TYPE_DECLARATION,
  AST_TYPE_DEFINITION,
  AST_TYPE_INSTANCE,
  AST_TYPE_NAME_NODE,
  AST_PRIMITIVE_TYPE_NODE,
  AST_USER_DEFINED_TYPE_NODE,
  AST_TYPE,
  AST_SEQ_PATTERN,
  AST_HEAD_TAILS_PATTERN,
  AST_TAILS_HEAD_PATTERN,
  AST_HEAD_TAILS_HEAD_PATTERN,
  AST_DICT_PATTERN,
  AST_RECORD_PATTERN,
  AST_OR_PATTERN,
  AST_CASE_EXPR,
  AST_CASE_CLAUSE,
  AST_ADT_DECL,
  AST_ADT_CONSTRUCTOR,
  AST_CONSTRUCTOR_PATTERN,
  AST_TRAIT_DECL,
  AST_INSTANCE_DECL,
  AST_EFFECT_DECL,
  AST_PERFORM_EXPR,
  AST_HANDLE_EXPR,
  AST_HANDLER_CLAUSE,
  AST_TYPED_PATTERN,
  AST_RECORD_LITERAL_EXPR
};

struct expr_wrapper {
private:
  AstNode *node;

public:
  explicit expr_wrapper(AstNode *node) : node(node) {}

  template <class T>
    requires derived_from<T, AstNode>
  T *get_node() {
    return static_cast<T *>(node);
  }

  template <class T>
    requires derived_from<T, AstNode>
  T *get_node_dynamic() {
    return dynamic_cast<T *>(node);
  }
};

class YONA_API AstNode {
private:
  virtual void print(std::ostream &os) const = 0;

public:
  SourceInfo source_context;
  AstNode *parent = nullptr;
  explicit AstNode(SourceContext token) : source_context(token) {};
  virtual ~AstNode() = default;
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<AstNode*>(this));
  }
  [[nodiscard]] virtual AstNodeType get_type() const { return AST_NODE; };
  friend YONA_API std::ostream &operator<<(std::ostream &os, const AstNode &obj);

  template <class T>
    requires derived_from<T, AstNode>
  [[nodiscard]] T *parent_of_type();

  template <class T>
    requires derived_from<T, AstNode>
  T *with_parent(AstNode *parent) {
    this->parent = parent;
    return static_cast<T *>(this);
  }
};

template <class T>
  requires derived_from<T, AstNode>
vector<T *> nodes_with_parent(vector<T *> nodes, AstNode *parent) {
  for (auto node : nodes) {
    node->parent = parent;
  }
  return nodes;
}

template <class T1, class T2>
  requires derived_from<T1, AstNode> && derived_from<T2, AstNode>
vector<pair<T1 *, T2 *>> nodes_with_parent(vector<pair<T1 *, T2 *>> nodes, AstNode *parent) {
  for (auto node : nodes) {
    node.first->parent = parent;
    if (node.second) {
      node.second->parent = parent;
    }
  }
  return nodes;
}

template <class... Types>
  requires(derived_from<Types, AstNode> && ...)
variant<Types *...> node_with_parent(variant<Types *...> node, AstNode *parent) {
  visit([parent](auto arg) { arg->parent = parent; }, node);
  return node;
}

template <class T>
  requires derived_from<T, AstNode>
optional<T *> node_with_parent(optional<T *> node, AstNode *parent) {
  if (node.has_value()) {
    node.value()->parent = parent;
  }
  return node;
}

template <class... Types>
  requires(derived_from<Types, AstNode> && ...)
vector<variant<Types *...>> nodes_with_parent(vector<variant<Types *...>> nodes, AstNode *parent) {
  for (auto node : nodes) {
    node_with_parent(node, parent);
  }
  return nodes;
}

class YONA_API ExprNode : public AstNode {
public:
  explicit ExprNode(SourceContext token) : AstNode(token) {}
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_EXPR_NODE; };
};

class YONA_API PatternNode : public AstNode {
public:
  explicit PatternNode(SourceContext token) : AstNode(token) {}
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_PATTERN_NODE; };
};

class YONA_API UnderscoreNode final : public PatternNode {
private:
  void print(std::ostream &os) const override;

public:
  explicit UnderscoreNode(SourceContext token) : PatternNode(token) {}
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_UNDERSCORE_NODE; };
};

class YONA_API ValueExpr : public ExprNode {
public:
  explicit ValueExpr(SourceContext token) : ExprNode(token) {}
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_VALUE_EXPR; };
};

class YONA_API ScopedNode : public ExprNode {
public:
  explicit ScopedNode(SourceContext token) : ExprNode(token) {}
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_SCOPED_NODE; };
};

template <typename T> class YONA_API LiteralExpr : public ValueExpr {
public:
  const T value;

  explicit LiteralExpr(SourceContext token, T value);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_LITERAL_EXPR; };
};

class YONA_API OpExpr : public ExprNode {
public:
  explicit OpExpr(SourceContext token) : ExprNode(token) {}
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_OP_EXPR; };
};

class YONA_API BinaryOpExpr : public OpExpr {
public:
  ExprNode *left;
  ExprNode *right;

  explicit BinaryOpExpr(SourceContext token, ExprNode *left, ExprNode *right);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_BINARY_OP_EXPR; };
  ~BinaryOpExpr() override;
};

class AliasExpr : public ExprNode {
public:
  explicit AliasExpr(SourceContext token) : ExprNode(token) {}
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_ALIAS_EXPR; };
};

class CallExpr : public ExprNode {
public:
  explicit CallExpr(SourceContext token) : ExprNode(token) {}
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_CALL_EXPR; };
};

class ImportClauseExpr : public ExprNode {
public:
  explicit ImportClauseExpr(SourceContext token) : ExprNode(token) {}
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_IMPORT_CLAUSE_EXPR; };
};

class YONA_API GeneratorExpr : public ExprNode {
public:
  explicit GeneratorExpr(SourceContext token) : ExprNode(token) {}
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_GENERATOR_EXPR; };
};

class YONA_API CollectionExtractorExpr : public ExprNode {
public:
  explicit CollectionExtractorExpr(SourceContext token) : ExprNode(token) {}
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_COLLECTION_EXTRACTOR_EXPR; };
};

class YONA_API SequenceExpr : public ExprNode {
public:
  explicit SequenceExpr(SourceContext token) : ExprNode(token) {}
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_SEQUENCE_EXPR; };
};

class FunctionBody : public AstNode {
public:
  explicit FunctionBody(SourceContext token) : AstNode(token) {}
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_FUNCTION_BODY; };
};

class YONA_API NameExpr final : public ExprNode {
private:
  void print(std::ostream &os) const override;

public:
  const string value;

  explicit NameExpr(SourceContext token, string value);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_NAME_EXPR; };
};

class YONA_API IdentifierExpr final : public ValueExpr {
private:
  void print(std::ostream &os) const override;

public:
  NameExpr *name;

  explicit IdentifierExpr(SourceContext token, NameExpr *name);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_IDENTIFIER_EXPR; };
  ~IdentifierExpr() override;
};

class RecordNode final : public ExprNode {
private:
  void print(std::ostream &os) const override;

public:
  NameExpr *recordType;
  vector<pair<IdentifierExpr *, TypeDefinition *>> identifiers;

  explicit RecordNode(SourceContext token, NameExpr *recordType, vector<pair<IdentifierExpr *, TypeDefinition *>> identifiers);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_RECORD_NODE; };
  ~RecordNode() override;
};

class YONA_API TrueLiteralExpr final : public LiteralExpr<bool> {
private:
  void print(std::ostream &os) const override;

public:
  explicit TrueLiteralExpr(SourceContext token);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_TRUE_LITERAL_EXPR; };
};

class FalseLiteralExpr final : public LiteralExpr<bool> {
private:
  void print(std::ostream &os) const override;

public:
  explicit FalseLiteralExpr(SourceContext token);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_FALSE_LITERAL_EXPR; };
};

class FloatExpr final : public LiteralExpr<double> {
private:
  void print(std::ostream &os) const override;

public:
  explicit FloatExpr(SourceContext token, double value);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_FLOAT_EXPR; };
};

class YONA_API IntegerExpr final : public LiteralExpr<int64_t> {
private:
  void print(std::ostream &os) const override;

public:
  explicit IntegerExpr(SourceContext token, int64_t value);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_INTEGER_EXPR; };
};

class ByteExpr final : public LiteralExpr<unsigned char> {
private:
  void print(std::ostream &os) const override;

public:
  explicit ByteExpr(SourceContext token, unsigned char value);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_BYTE_EXPR; };
};

class YONA_API StringExpr final : public LiteralExpr<string> {
private:
  void print(std::ostream &os) const override;

public:
  explicit StringExpr(SourceContext token, string value);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_STRING_EXPR; };
};

class CharacterExpr final : public LiteralExpr<wchar_t> {
private:
  void print(std::ostream &os) const override;

public:
  explicit CharacterExpr(SourceContext token, wchar_t value);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_CHARACTER_EXPR; };
};

class UnitExpr final : public LiteralExpr<nullptr_t> {
private:
  void print(std::ostream &os) const override;

public:
  explicit UnitExpr(SourceContext token);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_UNIT_EXPR; };
};

class YONA_API TupleExpr final : public ValueExpr {
private:
  void print(std::ostream &os) const override;

public:
  vector<ExprNode *> values;

  explicit TupleExpr(SourceContext token, vector<ExprNode *> values);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_TUPLE_EXPR; };
  ~TupleExpr() override;
};

class DictExpr final : public ValueExpr {
private:
  void print(std::ostream &os) const override;

public:
  vector<pair<ExprNode *, ExprNode *>> values;

  explicit DictExpr(SourceContext token, vector<pair<ExprNode *, ExprNode *>> values);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_DICT_EXPR; };
  ~DictExpr() override;
};

class YONA_API ValuesSequenceExpr final : public SequenceExpr {
private:
  void print(std::ostream &os) const override;

public:
  vector<ExprNode *> values;

  explicit ValuesSequenceExpr(SourceContext token, vector<ExprNode *> values);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_VALUES_SEQUENCE_EXPR; };
  ~ValuesSequenceExpr() override;
};

class RangeSequenceExpr final : public SequenceExpr {
private:
  void print(std::ostream &os) const override;

public:
  ExprNode *start;
  ExprNode *end;
  ExprNode *step;

  explicit RangeSequenceExpr(SourceContext token, ExprNode *start, ExprNode *end, ExprNode *step);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_RANGE_SEQUENCE_EXPR; };
  ~RangeSequenceExpr() override;
};

class YONA_API SetExpr final : public ValueExpr {
private:
  void print(std::ostream &os) const override;

public:
  vector<ExprNode *> values;

  explicit SetExpr(SourceContext token, vector<ExprNode *> values);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_SET_EXPR; };
  ~SetExpr() override;
};

class YONA_API SymbolExpr final : public ValueExpr {
private:
  void print(std::ostream &os) const override;

public:
  string value;

  explicit SymbolExpr(SourceContext token, string value);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_SYMBOL_EXPR; };
};

class PackageNameExpr final : public ValueExpr {
private:
  void print(std::ostream &os) const override;

public:
  vector<NameExpr *> parts;

  explicit PackageNameExpr(SourceContext token, vector<NameExpr *> parts);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_PACKAGE_NAME_EXPR; };
  ~PackageNameExpr() override;
  [[nodiscard]] string to_string() const;
};

class FqnExpr final : public ValueExpr {
private:
  void print(std::ostream &os) const override;

public:
  optional<PackageNameExpr *> packageName;
  NameExpr *moduleName;

  explicit FqnExpr(SourceContext token, optional<PackageNameExpr *> packageName, NameExpr *moduleName);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_FQN_EXPR; };
  ~FqnExpr() override;
  [[nodiscard]] string to_string() const;
};

class FunctionExpr final : public ScopedNode {
private:
  void print(std::ostream &os) const override;

public:
  const string name;
  vector<PatternNode *> patterns; // args
  /// Parallel to `patterns`: true when the parameter was written as `@borrow ...`.
  vector<bool> param_borrow;
  vector<FunctionBody *> bodies;  // 1 if without guards, or more with guards
  std::optional<compiler::types::Type> type_signature; // optional type annotation
  string source_text; // original source for cross-module monomorphization

  explicit FunctionExpr(SourceContext token, string name, vector<PatternNode *> patterns, vector<FunctionBody *> bodies,
                        std::optional<compiler::types::Type> type_sig = std::nullopt);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_FUNCTION_EXPR; };
  ~FunctionExpr() override;
};

// ModuleDecl is a top-level declaration, NOT an expression.
// A re-export declaration: names re-exported from another module
struct ReExport {
    vector<string> names;    // function/constructor names to re-export
    string source_module;    // FQN of source module (e.g., "Std\\List")
};

// Modules compile to object files with C-ABI exports via compile_module(),
// not through the expression codegen pipeline.
class ModuleDecl final : public AstNode {
private:
  void print(std::ostream &os) const override;

public:
  FqnExpr *fqn;
  vector<string> exports;            // direct exports (functions defined in this module)
  vector<string> exported_types;     // type names exported via "export type Name"
  vector<string> exported_traits;    // trait names exported via "export trait Name"
  vector<ReExport> re_exports;       // re-exported symbols from other modules
  vector<FunctionExpr *> functions;
  vector<FunctionDeclaration *> functionDeclarations;
  vector<ExternDeclExpr *> extern_declarations;  // module-level extern bindings
  vector<AdtDeclNode *> adt_declarations;
  vector<TraitDeclNode *> trait_declarations;
  vector<InstanceDeclNode *> instance_declarations;

  explicit ModuleDecl(SourceContext token, FqnExpr *fqn, const vector<string> &exports,
                      const vector<string> &exported_types,
                      const vector<string> &exported_traits,
                      const vector<ReExport> &re_exports,
                      const vector<FunctionExpr *> &functions, const vector<FunctionDeclaration *> &function_declarations,
                      const vector<AdtDeclNode *> &adt_declarations = {},
                      const vector<TraitDeclNode *> &trait_declarations = {},
                      const vector<InstanceDeclNode *> &instance_declarations = {},
                      const vector<ExternDeclExpr *> &extern_declarations = {});
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_MODULE_DECL; };
  ~ModuleDecl() override;
};

class RecordInstanceExpr final : public ValueExpr {
private:
  void print(std::ostream &os) const override;

public:
  NameExpr *recordType;
  vector<pair<NameExpr *, ExprNode *>> items;

  explicit RecordInstanceExpr(SourceContext token, NameExpr *recordType, vector<pair<NameExpr *, ExprNode *>> items);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_RECORD_INSTANCE_EXPR; };
  ~RecordInstanceExpr() override;
};

/// Anonymous record literal: `{ name = "Alice", age = 30 }`
/// Compiled as a tuple with a compile-time field map (name → index).
class RecordLiteralExpr final : public ExprNode {
private:
    void print(std::ostream &os) const override;
public:
    vector<pair<string, ExprNode*>> fields; ///< field name → value, sorted by name

    explicit RecordLiteralExpr(SourceContext token, vector<pair<string, ExprNode*>> fields);
    template<typename ResultType>
    ResultType accept(const AstVisitor<ResultType> &visitor) const {
        return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
    }
    [[nodiscard]] AstNodeType get_type() const override { return AST_RECORD_LITERAL_EXPR; };
    ~RecordLiteralExpr() override;
};

class BodyWithGuards final : public FunctionBody {
private:
  void print(std::ostream &os) const override;

public:
  ExprNode *guard;
  ExprNode *expr;

  explicit BodyWithGuards(SourceContext token, ExprNode *guard, ExprNode *expr);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_BODY_WITH_GUARDS; };
  ~BodyWithGuards() override;
};

class BodyWithoutGuards final : public FunctionBody {
private:
  void print(std::ostream &os) const override;

public:
  ExprNode *expr;

  explicit BodyWithoutGuards(SourceContext token, ExprNode *expr);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_BODY_WITHOUT_GUARDS; };
  ~BodyWithoutGuards() override;
};

class LogicalNotOpExpr final : public OpExpr {
private:
  void print(std::ostream &os) const override;

public:
  ExprNode *expr;

  explicit LogicalNotOpExpr(SourceContext token, ExprNode *expr);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_LOGICAL_NOT_OP_EXPR; };
  ~LogicalNotOpExpr() override;
};

class BinaryNotOpExpr final : public OpExpr {
private:
  void print(std::ostream &os) const override;

public:
  ExprNode *expr;

  explicit BinaryNotOpExpr(SourceContext token, ExprNode *expr);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_BINARY_NOT_OP_EXPR; };
  ~BinaryNotOpExpr() override;
};

class PowerExpr final : public BinaryOpExpr {
private:
  void print(std::ostream &os) const override;

public:
  explicit PowerExpr(SourceContext token, ExprNode *left, ExprNode *right);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_POWER_EXPR; };
};

class YONA_API MultiplyExpr final : public BinaryOpExpr {
private:
  void print(std::ostream &os) const override;

public:
  explicit MultiplyExpr(SourceContext token, ExprNode *left, ExprNode *right);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_MULTIPLY_EXPR; };
};

class DivideExpr final : public BinaryOpExpr {
private:
  void print(std::ostream &os) const override;

public:
  explicit DivideExpr(SourceContext token, ExprNode *left, ExprNode *right);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_DIVIDE_EXPR; };
};

class ModuloExpr final : public BinaryOpExpr {
private:
  void print(std::ostream &os) const override;

public:
  explicit ModuloExpr(SourceContext token, ExprNode *left, ExprNode *right);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_MODULO_EXPR; };
};

class YONA_API AddExpr final : public BinaryOpExpr {
private:
  void print(std::ostream &os) const override;

public:
  explicit AddExpr(SourceContext token, ExprNode *left, ExprNode *right);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_ADD_EXPR; };
};

class SubtractExpr final : public BinaryOpExpr {
private:
  void print(std::ostream &os) const override;

public:
  explicit SubtractExpr(SourceContext token, ExprNode *left, ExprNode *right);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_SUBTRACT_EXPR; };
};

class LeftShiftExpr final : public BinaryOpExpr {
private:
  void print(std::ostream &os) const override;

public:
  explicit LeftShiftExpr(SourceContext token, ExprNode *left, ExprNode *right);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_LEFT_SHIFT_EXPR; };
};

class RightShiftExpr final : public BinaryOpExpr {
private:
  void print(std::ostream &os) const override;

public:
  explicit RightShiftExpr(SourceContext token, ExprNode *left, ExprNode *right);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_RIGHT_SHIFT_EXPR; };
};

class ZerofillRightShiftExpr final : public BinaryOpExpr {
private:
  void print(std::ostream &os) const override;

public:
  explicit ZerofillRightShiftExpr(SourceContext token, ExprNode *left, ExprNode *right);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_ZEROFILL_RIGHT_SHIFT_EXPR; };
};

class GteExpr final : public BinaryOpExpr {
private:
  void print(std::ostream &os) const override;

public:
  explicit GteExpr(SourceContext token, ExprNode *left, ExprNode *right);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_GTE_EXPR; };
};

class LteExpr final : public BinaryOpExpr {
private:
  void print(std::ostream &os) const override;

public:
  explicit LteExpr(SourceContext token, ExprNode *left, ExprNode *right);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_LTE_EXPR; };
};

class GtExpr final : public BinaryOpExpr {
private:
  void print(std::ostream &os) const override;

public:
  explicit GtExpr(SourceContext token, ExprNode *left, ExprNode *right);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_GT_EXPR; };
};

class LtExpr final : public BinaryOpExpr {
private:
  void print(std::ostream &os) const override;

public:
  explicit LtExpr(SourceContext token, ExprNode *left, ExprNode *right);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_LT_EXPR; };
};

class YONA_API EqExpr final : public BinaryOpExpr {
private:
  void print(std::ostream &os) const override;

public:
  explicit EqExpr(SourceContext token, ExprNode *left, ExprNode *right);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_EQ_EXPR; };
};

class NeqExpr final : public BinaryOpExpr {
private:
  void print(std::ostream &os) const override;

public:
  explicit NeqExpr(SourceContext token, ExprNode *left, ExprNode *right);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_NEQ_EXPR; };
};

class ConsLeftExpr final : public BinaryOpExpr {
private:
  void print(std::ostream &os) const override;

public:
  explicit ConsLeftExpr(SourceContext token, ExprNode *left, ExprNode *right);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_CONS_LEFT_EXPR; };
};

class ConsRightExpr final : public BinaryOpExpr {
private:
  void print(std::ostream &os) const override;

public:
  explicit ConsRightExpr(SourceContext token, ExprNode *left, ExprNode *right);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_CONS_RIGHT_EXPR; };
};

class JoinExpr final : public BinaryOpExpr {
private:
  void print(std::ostream &os) const override;

public:
  explicit JoinExpr(SourceContext token, ExprNode *left, ExprNode *right);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_JOIN_EXPR; };
};

class BitwiseAndExpr final : public BinaryOpExpr {
private:
  void print(std::ostream &os) const override;

public:
  explicit BitwiseAndExpr(SourceContext token, ExprNode *left, ExprNode *right);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_BITWISE_AND_EXPR; };
};

class BitwiseXorExpr final : public BinaryOpExpr {
private:
  void print(std::ostream &os) const override;

public:
  explicit BitwiseXorExpr(SourceContext token, ExprNode *left, ExprNode *right);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_BITWISE_XOR_EXPR; };
};

class BitwiseOrExpr final : public BinaryOpExpr {
private:
  void print(std::ostream &os) const override;

public:
  explicit BitwiseOrExpr(SourceContext token, ExprNode *left, ExprNode *right);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_BITWISE_OR_EXPR; };
};

class LogicalAndExpr final : public BinaryOpExpr {
private:
  void print(std::ostream &os) const override;

public:
  explicit LogicalAndExpr(SourceContext token, ExprNode *left, ExprNode *right);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_LOGICAL_AND_EXPR; };
};

class LogicalOrExpr final : public BinaryOpExpr {
private:
  void print(std::ostream &os) const override;

public:
  explicit LogicalOrExpr(SourceContext token, ExprNode *left, ExprNode *right);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_LOGICAL_OR_EXPR; };
};

class InExpr final : public BinaryOpExpr {
private:
  void print(std::ostream &os) const override;

public:
  explicit InExpr(SourceContext token, ExprNode *left, ExprNode *right);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_IN_EXPR; };
};

class PipeLeftExpr final : public BinaryOpExpr {
private:
  void print(std::ostream &os) const override;

public:
  explicit PipeLeftExpr(SourceContext token, ExprNode *left, ExprNode *right);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_PIPE_LEFT_EXPR; };
};

class PipeRightExpr final : public BinaryOpExpr {
private:
  void print(std::ostream &os) const override;

public:
  explicit PipeRightExpr(SourceContext token, ExprNode *left, ExprNode *right);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_PIPE_RIGHT_EXPR; };
};

class YONA_API LetExpr final : public ScopedNode {
private:
  void print(std::ostream &os) const override;

public:
  vector<AliasExpr *> aliases;
  ExprNode *expr;

  explicit LetExpr(SourceContext token, vector<AliasExpr *> aliases, ExprNode *expr);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_LET_EXPR; };
  ~LetExpr() override;
};

class YONA_API MainNode final : public ScopedNode {
private:
  void print(std::ostream &os) const override;

public:
  AstNode *node;

  explicit MainNode(SourceContext token, AstNode *node);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_MAIN; };
  ~MainNode() override;
};

class YONA_API IfExpr final : public ExprNode {
private:
  void print(std::ostream &os) const override;

public:
  ExprNode *condition;
  ExprNode *thenExpr;
  ExprNode *elseExpr;

  explicit IfExpr(SourceContext token, ExprNode *condition, ExprNode *thenExpr, ExprNode *elseExpr);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_IF_EXPR; };
  ~IfExpr() override;
};

class ApplyExpr final : public ExprNode {
private:
  void print(std::ostream &os) const override;

public:
  CallExpr *call;
  vector<variant<ExprNode *, ValueExpr *>> args;
  // For named arguments: maps parameter name to argument
  optional<vector<pair<string, variant<ExprNode *, ValueExpr *>>>> named_args;

  explicit ApplyExpr(SourceContext token, CallExpr *call, vector<variant<ExprNode *, ValueExpr *>> args);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_APPLY_EXPR; };
  ~ApplyExpr() override;
};

class DoExpr final : public ExprNode {
private:
  void print(std::ostream &os) const override;

public:
  vector<ExprNode *> steps;

  explicit DoExpr(SourceContext token, vector<ExprNode *> steps);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_DO_EXPR; };
  ~DoExpr() override;
};

class ImportExpr final : public ScopedNode {
private:
  void print(std::ostream &os) const override;

public:
  vector<ImportClauseExpr *> clauses;
  ExprNode *expr;

  explicit ImportExpr(SourceContext token, vector<ImportClauseExpr *> clauses, ExprNode *expr);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_IMPORT_EXPR; };
  ~ImportExpr() override;
};

/// How `extern` / `.yonai` FN metadata lowers promise-carrying calls (see design-gpu-async.md).
enum class ExternPromiseKind : uint8_t {
  Sync,        ///< plain `extern` — synchronous C ABI
  ThreadPool,  ///< `extern async` — pool runs C fn; caller gets pool promise
  IoUring,     ///< `extern io` — C returns i64 id; await via yona_rt_io_await
  NativePtr,   ///< `extern native` — C returns yona_promise_t*; await via yona_rt_async_await
};

// extern [async|io|native] name : Type -> Type in body
class YONA_API ExternDeclExpr final : public ExprNode {
private:
  void print(std::ostream &os) const override;

public:
  string name;              // local Yona name (also the C symbol if c_symbol is empty)
  string c_symbol;          // optional explicit C ABI symbol from `= "..."` form
  compiler::types::Type declared_type;  // type annotation
  ExprNode *body;           // expression using the extern
  ExternPromiseKind extern_promise = ExternPromiseKind::Sync;

  explicit ExternDeclExpr(SourceContext token, string name,
                           compiler::types::Type type, ExprNode *body,
                           ExternPromiseKind ext_promise = ExternPromiseKind::Sync);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_EXTERN_DECL; };
  ~ExternDeclExpr() override;
};

class YONA_API RaiseExpr final : public ExprNode {
private:
  void print(std::ostream &os) const override;

public:
  ExprNode *value;  // the exception value (typically an ADT constructor call)

  explicit RaiseExpr(SourceContext token, ExprNode *value);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_RAISE_EXPR; };
  ~RaiseExpr() override;
};

class WithExpr final : public ScopedNode {
private:
  void print(std::ostream &os) const override;

public:
  bool daemon;
  ExprNode *contextExpr;
  NameExpr *name;
  ExprNode *bodyExpr;

  explicit WithExpr(SourceContext token, bool daemon, ExprNode *contextExpr, NameExpr *name, ExprNode *bodyExpr);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_WITH_EXPR; };
  ~WithExpr() override;
};

class FieldAccessExpr final : public ExprNode {
private:
  void print(std::ostream &os) const override;

public:
  IdentifierExpr *identifier;
  NameExpr *name;

  explicit FieldAccessExpr(SourceContext token, IdentifierExpr *identifier, NameExpr *name);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_FIELD_ACCESS_EXPR; };
  ~FieldAccessExpr() override;
};

class FieldUpdateExpr final : public ExprNode {
private:
  void print(std::ostream &os) const override;

public:
  IdentifierExpr *identifier;
  vector<pair<NameExpr *, ExprNode *>> updates;

  explicit FieldUpdateExpr(SourceContext token, IdentifierExpr *identifier, vector<pair<NameExpr *, ExprNode *>> updates);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_FIELD_UPDATE_EXPR; };
  ~FieldUpdateExpr() override;
};

class LambdaAlias final : public AliasExpr {
private:
  void print(std::ostream &os) const override;

public:
  NameExpr *name;
  FunctionExpr *lambda;

  explicit LambdaAlias(SourceContext token, NameExpr *name, FunctionExpr *lambda);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_LAMBDA_ALIAS; };
  ~LambdaAlias() override;
};

class ValueAlias final : public AliasExpr {
private:
  void print(std::ostream &os) const override;

public:
  IdentifierExpr *identifier;
  ExprNode *expr;

  explicit ValueAlias(SourceContext token, IdentifierExpr *identifier, ExprNode *expr);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_VALUE_ALIAS; };
  ~ValueAlias() override;
};

class YONA_API PatternAlias final : public AliasExpr {
private:
  void print(std::ostream &os) const override;

public:
  PatternNode *pattern;
  ExprNode *expr;

  explicit PatternAlias(SourceContext token, PatternNode *pattern, ExprNode *expr);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_PATTERN_ALIAS; };
  ~PatternAlias() override;
};

class FqnAlias final : public AliasExpr {
private:
  void print(std::ostream &os) const override;

public:
  NameExpr *name;
  FqnExpr *fqn;

  explicit FqnAlias(SourceContext token, NameExpr *name, FqnExpr *fqn);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_FQN_ALIAS; };
  ~FqnAlias() override;
};

class FunctionAlias final : public AliasExpr {
private:
  void print(std::ostream &os) const override;

public:
  NameExpr *name;
  NameExpr *alias;

  explicit FunctionAlias(SourceContext token, NameExpr *name, NameExpr *alias);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_FUNCTION_ALIAS; };
  ~FunctionAlias() override;
};

class AliasCall final : public CallExpr {
private:
  void print(std::ostream &os) const override;

public:
  NameExpr *alias;
  NameExpr *funName;

  explicit AliasCall(SourceContext token, NameExpr *alias, NameExpr *funName);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_ALIAS_CALL; };
  ~AliasCall() override;
};

class NameCall final : public CallExpr {
private:
  void print(std::ostream &os) const override;

public:
  NameExpr *name;

  explicit NameCall(SourceContext token, NameExpr *name);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_NAME_CALL; };
  ~NameCall() override;

  // Debug method
  [[nodiscard]] const NameExpr* get_name() const { return name; }
};

class ModuleCall final : public CallExpr {
private:
  void print(std::ostream &os) const override;

public:
  variant<FqnExpr *, ExprNode *> fqn;
  NameExpr *funName;

  explicit ModuleCall(SourceContext token, const variant<FqnExpr *, ExprNode *> &fqn, NameExpr *funName);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_MODULE_CALL; };
  ~ModuleCall() override;
};

class ExprCall final : public CallExpr {
private:
  void print(std::ostream &os) const override;

public:
  ExprNode *expr;

  explicit ExprCall(SourceContext token, ExprNode *expr);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_EXPR_CALL; };
  ~ExprCall() override;
};

class ModuleImport final : public ImportClauseExpr {
private:
  void print(std::ostream &os) const override;

public:
  FqnExpr *fqn;
  NameExpr *name;

  explicit ModuleImport(SourceContext token, FqnExpr *fqn, NameExpr *name);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_MODULE_IMPORT; };
  ~ModuleImport() override;
};

class FunctionsImport final : public ImportClauseExpr {
private:
  void print(std::ostream &os) const override;

public:
  vector<FunctionAlias *> aliases;
  FqnExpr *fromFqn;

  explicit FunctionsImport(SourceContext token, vector<FunctionAlias *> aliases, FqnExpr *fromFqn);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_FUNCTIONS_IMPORT; };
  ~FunctionsImport() override;
};

class YONA_API SeqGeneratorExpr final : public GeneratorExpr {
private:
  void print(std::ostream &os) const override;

public:
  ExprNode *reducerExpr;
  CollectionExtractorExpr *collectionExtractor;
  ExprNode *stepExpression;
  bool is_parallel = false; ///< true for parallel comprehensions [| ... |]

  explicit SeqGeneratorExpr(SourceContext token, ExprNode *reducerExpr, CollectionExtractorExpr *collectionExtractor, ExprNode *stepExpression);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_SEQ_GENERATOR_EXPR; };
  ~SeqGeneratorExpr() override;
};

class YONA_API SetGeneratorExpr final : public GeneratorExpr {
private:
  void print(std::ostream &os) const override;

public:
  ExprNode *reducerExpr;
  CollectionExtractorExpr *collectionExtractor;
  ExprNode *stepExpression;

  explicit SetGeneratorExpr(SourceContext token, ExprNode *reducerExpr, CollectionExtractorExpr *collectionExtractor, ExprNode *stepExpression);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_SET_GENERATOR_EXPR; };
  ~SetGeneratorExpr() override;
};

class DictGeneratorReducer final : public ExprNode {
private:
  void print(std::ostream &os) const override;

public:
  ExprNode *key;
  ExprNode *value;

  explicit DictGeneratorReducer(SourceContext token, ExprNode *key, ExprNode *value);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_DICT_GENERATOR_REDUCER; };
  ~DictGeneratorReducer() override;
};

class DictGeneratorExpr final : public GeneratorExpr {
private:
  void print(std::ostream &os) const override;

public:
  DictGeneratorReducer *reducerExpr;
  CollectionExtractorExpr *collectionExtractor;
  ExprNode *stepExpression;

  explicit DictGeneratorExpr(SourceContext token, DictGeneratorReducer *reducerExpr, CollectionExtractorExpr *collectionExtractor,
                             ExprNode *stepExpression);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_DICT_GENERATOR_EXPR; };
  ~DictGeneratorExpr() override;
};

class UnderscorePattern final : public PatternNode {
private:
  void print(std::ostream &os) const override;

public:
  explicit UnderscorePattern(SourceContext token) : PatternNode(token) {}
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_UNDERSCORE_PATTERN; };
};

using IdentifierOrUnderscore = variant<IdentifierExpr *, UnderscoreNode *>;

class YONA_API ValueCollectionExtractorExpr final : public CollectionExtractorExpr {
private:
  void print(std::ostream &os) const override;

public:
  IdentifierOrUnderscore expr;
  ExprNode *collection;     // source collection to iterate over
  ExprNode *condition;      // optional guard expression (nullptr if no guard)

  explicit ValueCollectionExtractorExpr(SourceContext token, IdentifierOrUnderscore identifier_or_underscore,
                                        ExprNode *collection = nullptr, ExprNode *condition = nullptr);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_VALUE_COLLECTION_EXTRACTOR_EXPR; };
  ~ValueCollectionExtractorExpr() override;
};

class KeyValueCollectionExtractorExpr final : public CollectionExtractorExpr {
private:
  void print(std::ostream &os) const override;

public:
  IdentifierOrUnderscore keyExpr;
  IdentifierOrUnderscore valueExpr;
  ExprNode *collection;     // source collection to iterate over
  ExprNode *condition;      // optional guard expression (nullptr if no guard)

  explicit KeyValueCollectionExtractorExpr(SourceContext token, IdentifierOrUnderscore keyExpr, IdentifierOrUnderscore valueExpr,
                                           ExprNode *collection = nullptr, ExprNode *condition = nullptr);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_KEY_VALUE_COLLECTION_EXTRACTOR_EXPR; };
  ~KeyValueCollectionExtractorExpr() override;
};

using PatternWithoutSequence = PatternNode; // variant<UnderscorePattern, PatternValue, TuplePattern,
                                            // DictPattern>;
using SequencePattern = PatternNode;        // variant<SeqPattern, HeadTailsPattern,
                                            // TailsHeadPattern, HeadTailsHeadPattern>;
using DataStructurePattern = PatternNode;   // variant<TuplePattern, SequencePattern, DictPattern,
                                            // RecordPattern>;
using Pattern = PatternNode;                // variant<UnderscorePattern, PatternValue,
                                            // DataStructurePattern, AsDataStructurePattern>;
using TailPattern = PatternNode;            // variant<IdentifierExpr, SequenceExpr,
                                            // UnderscoreNode, StringExpr>;

class PatternWithGuards final : public PatternNode {
private:
  void print(std::ostream &os) const override;

public:
  ExprNode *guard;
  ExprNode *expr;

  explicit PatternWithGuards(SourceContext token, ExprNode *guard, ExprNode *expr);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_PATTERN_WITH_GUARDS; };
  ~PatternWithGuards() override;
};

class YONA_API PatternWithoutGuards final : public PatternNode {
private:
  void print(std::ostream &os) const override;

public:
  ExprNode *expr;

  explicit PatternWithoutGuards(SourceContext token, ExprNode *expr);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_PATTERN_WITHOUT_GUARDS; };
  ~PatternWithoutGuards() override;
};

// New class that properly associates a pattern with its body expression for case statements
class YONA_API CaseClause : public ExprNode {
private:
  void print(std::ostream &os) const override;

public:
  Pattern *pattern;
  ExprNode *body;
  ExprNode *guard = nullptr;  // optional guard expression (pattern | guard -> body)

  explicit CaseClause(SourceContext token, Pattern *pattern, ExprNode *body, ExprNode *guard = nullptr);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_CASE_CLAUSE; };
  ~CaseClause() override;
};

class PatternExpr : public ExprNode {
private:
  void print(std::ostream &os) const override;

public:
  variant<Pattern *, PatternWithoutGuards *, vector<PatternWithGuards *>> patternExpr;

  explicit PatternExpr(SourceContext token, const variant<Pattern *, PatternWithoutGuards *, vector<PatternWithGuards *>> &patternExpr);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_PATTERN_EXPR; };
  ~PatternExpr() override;
};

class YONA_API CatchPatternExpr final : public ExprNode {
private:
  void print(std::ostream &os) const override;

public:
  Pattern *matchPattern;
  variant<PatternWithoutGuards *, vector<PatternWithGuards *>> pattern;

  explicit CatchPatternExpr(SourceContext token, Pattern *matchPattern, const variant<PatternWithoutGuards *, vector<PatternWithGuards *>> &pattern);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_CATCH_PATTERN_EXPR; };
  ~CatchPatternExpr() override;
};

class YONA_API CatchExpr final : public ExprNode {
private:
  void print(std::ostream &os) const override;

public:
  vector<CatchPatternExpr *> patterns;

  explicit CatchExpr(SourceContext token, vector<CatchPatternExpr *> patterns);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_CATCH_EXPR; };
  ~CatchExpr() override;
};

class YONA_API TryCatchExpr final : public ExprNode {
private:
  void print(std::ostream &os) const override;

public:
  ExprNode *tryExpr;
  CatchExpr *catchExpr;

  explicit TryCatchExpr(SourceContext token, ExprNode *tryExpr, CatchExpr *catchExpr);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_TRY_CATCH_EXPR; };
  ~TryCatchExpr() override;
};

class YONA_API PatternValue final : public PatternNode {
private:
  void print(std::ostream &os) const override;

public:
  variant<LiteralExpr<nullptr_t> *, LiteralExpr<void *> *, SymbolExpr *, IdentifierExpr *> expr;

  explicit PatternValue(SourceContext token, const variant<LiteralExpr<nullptr_t> *, LiteralExpr<void *> *, SymbolExpr *, IdentifierExpr *> &expr);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_PATTERN_VALUE; };
  ~PatternValue() override;
};

class AsDataStructurePattern final : public PatternNode {
private:
  void print(std::ostream &os) const override;

public:
  IdentifierExpr *identifier;
  DataStructurePattern *pattern;

  explicit AsDataStructurePattern(SourceContext token, IdentifierExpr *identifier, DataStructurePattern *pattern);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_AS_DATA_STRUCTURE_PATTERN; };
  ~AsDataStructurePattern() override;
};

class YONA_API TuplePattern final : public PatternNode {
private:
  void print(std::ostream &os) const override;

public:
  vector<Pattern *> patterns;

  explicit TuplePattern(SourceContext token, vector<Pattern *> patterns);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_TUPLE_PATTERN; };
  ~TuplePattern() override;
};

class SeqPattern final : public PatternNode {
private:
  void print(std::ostream &os) const override;

public:
  vector<Pattern *> patterns;

  explicit SeqPattern(SourceContext token, vector<Pattern *> patterns);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_SEQ_PATTERN; };
  ~SeqPattern() override;
};

class HeadTailsPattern final : public PatternNode {
private:
  void print(std::ostream &os) const override;

public:
  vector<PatternWithoutSequence *> heads;
  TailPattern *tail;

  explicit HeadTailsPattern(SourceContext token, vector<PatternWithoutSequence *> heads, TailPattern *tail);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_HEAD_TAILS_PATTERN; };
  ~HeadTailsPattern() override;
};

class TailsHeadPattern final : public PatternNode {
private:
  void print(std::ostream &os) const override;

public:
  TailPattern *tail;
  vector<PatternWithoutSequence *> heads;

  explicit TailsHeadPattern(SourceContext token, TailPattern *tail, vector<PatternWithoutSequence *> heads);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_TAILS_HEAD_PATTERN; };
  ~TailsHeadPattern() override;
};

class HeadTailsHeadPattern final : public PatternNode {
private:
  void print(std::ostream &os) const override;

public:
  vector<PatternWithoutSequence *> left;
  TailPattern *tail;
  vector<PatternWithoutSequence *> right;

  explicit HeadTailsHeadPattern(SourceContext token, vector<PatternWithoutSequence *> left, TailPattern *tail,
                                vector<PatternWithoutSequence *> right);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_HEAD_TAILS_HEAD_PATTERN; };
  ~HeadTailsHeadPattern() override;
};

class DictPattern final : public PatternNode {
private:
  void print(std::ostream &os) const override;

public:
  vector<pair<PatternValue *, Pattern *>> keyValuePairs;

  explicit DictPattern(SourceContext token, vector<pair<PatternValue *, Pattern *>> keyValuePairs);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_DICT_PATTERN; };
  ~DictPattern() override;
};

class RecordPattern final : public PatternNode {
private:
  void print(std::ostream &os) const override;

public:
  const string recordType;
  vector<pair<NameExpr *, Pattern *>> items;

  explicit RecordPattern(SourceContext token, string recordType, vector<pair<NameExpr *, Pattern *>> items);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_RECORD_PATTERN; };
  ~RecordPattern() override;
};

class OrPattern final : public PatternNode {
private:
  void print(std::ostream &os) const override;

public:
  vector<unique_ptr<PatternNode>> patterns;

  explicit OrPattern(SourceContext token, vector<unique_ptr<PatternNode>> patterns);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_OR_PATTERN; };
  ~OrPattern() override;
};

// A type reference in an ADT field position.
// Either a simple named type ("Int", "a") or a function type (Int -> Int).
struct FieldType {
    string name;                         // "Int", "a", "Fn", etc.
    bool is_function_type = false;
    vector<FieldType> param_types;       // function parameter types
    vector<FieldType> return_types;      // 0 or 1 element (vector avoids heap alloc)

    static FieldType simple(string n) { FieldType ft; ft.name = std::move(n); return ft; }
    string to_string() const;
};

class AdtConstructor final : public AstNode {
private:
    void print(std::ostream &os) const override;
public:
    string name;
    vector<FieldType> field_type_names;  // type of each field
    vector<string> field_names;          // named fields (empty for positional constructors)

    explicit AdtConstructor(SourceContext token, string name, vector<FieldType> field_types,
                            vector<string> field_names = {});
    template<typename ResultType>
    ResultType accept(const AstVisitor<ResultType> &visitor) const {
        return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
    }
    [[nodiscard]] AstNodeType get_type() const override { return AST_ADT_CONSTRUCTOR; };
    ~AdtConstructor() override;
};

class AdtDeclNode final : public AstNode {
private:
    void print(std::ostream &os) const override;
public:
    string name;
    vector<string> type_params;
    vector<AdtConstructor*> variants;
    vector<string> derive_traits;   ///< Traits to auto-derive: ["Show", "Eq", ...]

    explicit AdtDeclNode(SourceContext token, string name, vector<string> type_params, vector<AdtConstructor*> variants,
                         vector<string> derive_traits = {});
    template<typename ResultType>
    ResultType accept(const AstVisitor<ResultType> &visitor) const {
        return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
    }
    [[nodiscard]] AstNodeType get_type() const override { return AST_ADT_DECL; };
    ~AdtDeclNode() override;
};

class ConstructorPattern final : public PatternNode {
private:
    void print(std::ostream &os) const override;
public:
    string constructor_name;
    vector<PatternNode*> sub_patterns;

    explicit ConstructorPattern(SourceContext token, string name, vector<PatternNode*> sub_patterns);
    template<typename ResultType>
    ResultType accept(const AstVisitor<ResultType> &visitor) const {
        return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
    }
    [[nodiscard]] AstNodeType get_type() const override { return AST_CONSTRUCTOR_PATTERN; };
    ~ConstructorPattern() override;
};

/// Typed pattern: `(name : Type)` for sum type matching
/// Matches a sum-typed value when its runtime tag equals the given type.
class TypedPattern final : public PatternNode {
private:
    void print(std::ostream &os) const override;
public:
    string binding_name;   ///< Variable to bind the unwrapped value
    string type_name;      ///< Type to match: "Int", "String", "Float", etc.

    explicit TypedPattern(SourceContext token, string name, string type_name);
    template<typename ResultType>
    ResultType accept(const AstVisitor<ResultType> &visitor) const {
        return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
    }
    [[nodiscard]] AstNodeType get_type() const override { return AST_TYPED_PATTERN; };
};

class YONA_API CaseExpr final : public ExprNode {
private:
  void print(std::ostream &os) const override;

public:
  ExprNode *expr;
  vector<CaseClause *> clauses;

  explicit CaseExpr(SourceContext token, ExprNode *expr, vector<CaseClause *> clauses);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_CASE_EXPR; };
  ~CaseExpr() override;
};

class TypeNameNode : public AstNode {
public:
  explicit TypeNameNode(SourceContext token) : AstNode(token) {}
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_TYPE_NAME_NODE; };
};

class BuiltinTypeNode final : public TypeNameNode {
private:
  void print(std::ostream &os) const override;

public:
  BuiltinType type;

  explicit BuiltinTypeNode(SourceContext token, const BuiltinType type) : TypeNameNode(token), type(type) {}
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_PRIMITIVE_TYPE_NODE; };
};

class UserDefinedTypeNode final : public TypeNameNode {
private:
  void print(std::ostream &os) const override;

public:
  NameExpr *name;

  explicit UserDefinedTypeNode(SourceContext token, NameExpr *name);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_USER_DEFINED_TYPE_NODE; };
  ~UserDefinedTypeNode() override;
};

class TypeDeclaration final : public AstNode {
private:
  void print(std::ostream &os) const override;

public:
  TypeNameNode *name;
  vector<NameExpr *> typeVars;

  explicit TypeDeclaration(SourceContext token, TypeNameNode *name, vector<NameExpr *> type_vars);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_TYPE_DECLARATION; };
  ~TypeDeclaration() override;
};

class TypeDefinition final : public AstNode {
private:
  void print(std::ostream &os) const override;

public:
  TypeNameNode *name;
  vector<TypeNameNode *> typeNames;

  explicit TypeDefinition(SourceContext token, TypeNameNode *name, vector<TypeNameNode *> type_names);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_TYPE_DEFINITION; };
  ~TypeDefinition() override;
};

class TypeNode final : public AstNode {
private:
  void print(std::ostream &os) const override;

public:
  TypeDeclaration *declaration;
  vector<TypeDeclaration *> definitions;

  explicit TypeNode(SourceContext token, TypeDeclaration *declaration, vector<TypeDeclaration *> definitions);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_TYPE; };
  ~TypeNode() override;
};

class TypeInstance final : public AstNode {
private:
  void print(std::ostream &os) const override;

public:
  TypeNameNode *name;
  vector<ExprNode *> exprs;

  explicit TypeInstance(SourceContext token, TypeNameNode *name, vector<ExprNode *> exprs);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_TYPE_INSTANCE; };
  ~TypeInstance() override;
};

class FunctionDeclaration final : public AstNode {
private:
  void print(std::ostream &os) const override;

public:
  NameExpr *functionName;
  vector<TypeDefinition *> typeDefinitions;

  explicit FunctionDeclaration(SourceContext token, NameExpr *function_name, vector<TypeDefinition *> type_definitions);
  template<typename ResultType>
  ResultType accept(const AstVisitor<ResultType> &visitor) const {
    return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
  }
  [[nodiscard]] AstNodeType get_type() const override { return AST_FUNCTION_DECLARATION; };
  ~FunctionDeclaration() override;
};

// Method signature in a trait declaration
struct TraitMethodSig {
    string name;
    compiler::types::Type type_signature;
    FunctionExpr* default_impl = nullptr;  // Phase 3: optional default implementation
};

class TraitDeclNode final : public AstNode {
private:
    void print(std::ostream &os) const override;
public:
    string name;                  // "Show" or "Iterable"
    string type_param;            // first type param "a" (backward compat)
    vector<string> type_params;   // all type params: {"a", "b"} for multi-param traits
    vector<TraitMethodSig> methods;
    vector<pair<string, string>> superclasses;

    explicit TraitDeclNode(SourceContext token, string name, string type_param, vector<TraitMethodSig> methods,
                           vector<pair<string, string>> superclasses = {});
    template<typename ResultType>
    ResultType accept(const AstVisitor<ResultType> &visitor) const {
        return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
    }
    [[nodiscard]] AstNodeType get_type() const override { return AST_TRAIT_DECL; };
    ~TraitDeclNode() override;
};

class InstanceDeclNode final : public AstNode {
private:
    void print(std::ostream &os) const override;
public:
    string trait_name;     // "Show" or "Iterable"
    string type_name;      // first type arg "Int" (backward compat)
    vector<string> type_names;  // all type args: {"Seq", "Int"} for multi-param
    vector<FunctionExpr*> methods;
    vector<pair<string, string>> constraints;
    vector<string> type_params;

    explicit InstanceDeclNode(SourceContext token, string trait_name, string type_name, vector<FunctionExpr*> methods,
                              vector<pair<string, string>> constraints = {}, vector<string> type_params = {});
    template<typename ResultType>
    ResultType accept(const AstVisitor<ResultType> &visitor) const {
        return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
    }
    [[nodiscard]] AstNodeType get_type() const override { return AST_INSTANCE_DECL; };
    ~InstanceDeclNode() override;
};

// ===== Algebraic Effects =====

/// An effect operation signature (name + type).
struct EffectOpSig {
    string name;
    compiler::types::Type type_signature;
};

/// Effect declaration: `effect State s; get : () -> s; put : s -> (); end`
class EffectDeclNode final : public AstNode {
private:
    void print(std::ostream &os) const override;
public:
    string name;            ///< Effect name, e.g. "State"
    string type_param;      ///< Type parameter, e.g. "s"
    vector<EffectOpSig> operations;

    explicit EffectDeclNode(SourceContext token, string name, string type_param,
                            vector<EffectOpSig> operations);
    template<typename ResultType>
    ResultType accept(const AstVisitor<ResultType> &visitor) const {
        return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
    }
    [[nodiscard]] AstNodeType get_type() const override { return AST_EFFECT_DECL; };
    ~EffectDeclNode() override;
};

/// Perform expression: `perform State.get ()`
class PerformExpr final : public ExprNode {
private:
    void print(std::ostream &os) const override;
public:
    string effect_name;     ///< "State"
    string operation_name;  ///< "get"
    vector<ExprNode*> args; ///< Operation arguments (excluding resume)

    explicit PerformExpr(SourceContext token, string effect_name, string operation_name,
                         vector<ExprNode*> args);
    template<typename ResultType>
    ResultType accept(const AstVisitor<ResultType> &visitor) const {
        return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
    }
    [[nodiscard]] AstNodeType get_type() const override { return AST_PERFORM_EXPR; };
    ~PerformExpr() override;
};

/// One handler clause in a handle...with block.
/// Either an operation handler: `State.get () resume -> resume 42`
/// Or a return handler: `return val -> val`
class HandlerClause final : public AstNode {
private:
    void print(std::ostream &os) const override;
public:
    string effect_name;     ///< "State" (empty for return clause)
    string operation_name;  ///< "get" (empty for return clause)
    vector<string> arg_names;  ///< Bound names for op args
    string resume_name;     ///< "resume" (empty for return clause)
    bool is_return_clause;  ///< true if this is `return val -> ...`
    string return_binding;  ///< "val" (for return clause)
    ExprNode* body;         ///< Handler body expression

    /// Operation handler constructor
    explicit HandlerClause(SourceContext token, string effect_name, string op_name,
                           vector<string> arg_names, string resume_name, ExprNode* body);
    /// Return handler constructor
    explicit HandlerClause(SourceContext token, string return_binding, ExprNode* body);
    template<typename ResultType>
    ResultType accept(const AstVisitor<ResultType> &visitor) const {
        return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
    }
    [[nodiscard]] AstNodeType get_type() const override { return AST_HANDLER_CLAUSE; };
    ~HandlerClause() override;
};

/// Handle expression: `handle <body> with <clauses> end`
class HandleExpr final : public ExprNode {
private:
    void print(std::ostream &os) const override;
public:
    ExprNode* body;
    vector<HandlerClause*> clauses;

    explicit HandleExpr(SourceContext token, ExprNode* body, vector<HandlerClause*> clauses);
    template<typename ResultType>
    ResultType accept(const AstVisitor<ResultType> &visitor) const {
        return visitor.visit(const_cast<typename std::remove_const<typename std::remove_pointer<decltype(this)>::type>::type*>(this));
    }
    [[nodiscard]] AstNodeType get_type() const override { return AST_HANDLE_EXPR; };
    ~HandleExpr() override;
};

// AstVisitor class is now defined in ast_visitor.h as a template

#ifdef _MSC_VER
#pragma warning(pop)
#endif

} // namespace yona::ast
