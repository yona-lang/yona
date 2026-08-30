//
// Created by akovari on 4.12.24.
//

#ifndef YONA_MODEL_TYPES_H
#define YONA_MODEL_TYPES_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace yona::compiler::types {
using std::get;
using std::get_if;
using std::holds_alternative;
using std::make_shared;
using std::max;
using std::nullptr_t;
using std::shared_ptr;
using std::string;
using std::unordered_map;
using std::unordered_set;
using std::variant;
using std::vector;

struct SingleItemCollectionType;
struct DictCollectionType;
struct DictCollectionType;
struct NamedType;
struct FunctionType;
struct SumType;
struct ProductType;
struct UnionType;
struct RecordType;
struct PromiseType;
struct RefinedType;

enum BuiltinType {
  Bool,
  Byte,
  SignedInt16,
  SignedInt32,
  SignedInt64,
  SignedInt128,
  UnsignedInt16,
  UnsignedInt32,
  UnsignedInt64,
  UnsignedInt128,
  Float32,
  Float64,
  Float128,
  Char,
  String,
  Symbol,
  Dict,
  Set,
  Seq,
  Var,
  Unit
};

static const std::string BuiltinTypeStrings[] = {
    "Bool",          "Byte",
    "SignedInt16",   "SignedInt32",
    "SignedInt64",   "SignedInt128",
    "UnsignedInt16", "UnsignedInt32",
    "UnsignedInt64", "UnsignedInt128",
    "Float32",       "Float64",
    "Float128",      "Char",
    "String",        "Symbol",
    "Dict",          "Set",
    "Seq",           "Var",
    "Unit"};

/// Shared semantic type graph.
///
/// Pointer alternatives share ownership of their nodes, but nodes are publicly
/// mutable, so copies observe one another's changes. All shared_ptr
/// alternatives must be non-null when used. Concurrent read-only traversal is
/// safe; callers must synchronize node mutation. nullptr_t is the explicit
/// unsupported-type sentinel returned by operations such as
/// derive_bin_op_result_type().
using Type =
    variant<BuiltinType, shared_ptr<SingleItemCollectionType>,
            shared_ptr<DictCollectionType>, shared_ptr<FunctionType>,
            shared_ptr<NamedType>, shared_ptr<SumType>, shared_ptr<ProductType>,
            shared_ptr<RecordType>, shared_ptr<PromiseType>,
            shared_ptr<RefinedType>, nullptr_t>;

struct SingleItemCollectionType final {
  enum CollectionKind { Set, Seq } kind;
  Type valueType;
};

struct DictCollectionType final {
  Type keyType;
  Type valueType;
};

struct FunctionType final {
  Type returnType;
  Type argumentType;
};

struct SumType final {
  unordered_set<Type> types;
};

struct ProductType final {
  vector<Type> types;
};

struct NamedType final {
  string name;
  Type type;
};

struct RecordType final {
  string name;
  unordered_map<string, Type> field_types;
};

// Promise<T> — a computation that will produce a value of type T.
// Transparent to the user: the type checker auto-inserts await coercions
// when Promise<T> is used where T is expected.
struct PromiseType final {
  Type valueType; // The type of the resolved value
};

/// A refinement predicate: `n > 0`, `length xs > 0`, `p && q`, etc.
struct RefinePredicate {
  enum Op {
    Gt,
    Lt,
    Ge,
    Le,
    Eq,
    Ne, // comparisons: var op literal
    And,
    Or,
    Not,      // boolean combinators
    LengthGt, // length var > literal (collection non-emptiness)
  } op;

  string var_name;     // the variable being constrained
  int64_t literal = 0; // comparison target
  vector<shared_ptr<RefinePredicate>> children; // for And/Or/Not

  static shared_ptr<RefinePredicate> make_cmp(Op op, const string &var,
                                              int64_t lit) {
    auto p = make_shared<RefinePredicate>();
    p->op = op;
    p->var_name = var;
    p->literal = lit;
    return p;
  }
  static shared_ptr<RefinePredicate> make_and(shared_ptr<RefinePredicate> a,
                                              shared_ptr<RefinePredicate> b) {
    auto p = make_shared<RefinePredicate>();
    p->op = And;
    p->children = {std::move(a), std::move(b)};
    return p;
  }
  static shared_ptr<RefinePredicate> make_or(shared_ptr<RefinePredicate> a,
                                             shared_ptr<RefinePredicate> b) {
    auto p = make_shared<RefinePredicate>();
    p->op = Or;
    p->children = {std::move(a), std::move(b)};
    return p;
  }
  static shared_ptr<RefinePredicate> make_length_gt(const string &var,
                                                    int64_t lit) {
    auto p = make_shared<RefinePredicate>();
    p->op = LengthGt;
    p->var_name = var;
    p->literal = lit;
    return p;
  }

  /// Pretty-print for error messages
  inline string to_string() const {
    switch (op) {
    case Gt:
      return var_name + " > " + std::to_string(literal);
    case Lt:
      return var_name + " < " + std::to_string(literal);
    case Ge:
      return var_name + " >= " + std::to_string(literal);
    case Le:
      return var_name + " <= " + std::to_string(literal);
    case Eq:
      return var_name + " == " + std::to_string(literal);
    case Ne:
      return var_name + " != " + std::to_string(literal);
    case LengthGt:
      return "length " + var_name + " > " + std::to_string(literal);
    case And:
      if (children.size() == 2)
        return children[0]->to_string() + " && " + children[1]->to_string();
      return "&&";
    case Or:
      if (children.size() == 2)
        return children[0]->to_string() + " || " + children[1]->to_string();
      return "||";
    case Not:
      if (!children.empty())
        return "!(" + children[0]->to_string() + ")";
      return "!";
    }
    return "?";
  }
};

/// Refined type: `{ var : BaseType | predicate }`
struct RefinedType final {
  string var_name;                       // bound variable name
  Type base_type;                        // the underlying type
  shared_ptr<RefinePredicate> predicate; // the constraint
};

// Check if a type is a Promise<T>
inline bool is_promise(const Type &type) {
  return holds_alternative<shared_ptr<PromiseType>>(type);
}

/// Unwrap Promise<T> to T. Returns an owned Type value sharing the same graph,
/// or the unchanged value when it is not a promise. A null PromiseType pointer
/// violates the Type invariant and must not be passed.
inline Type unwrap_promise(const Type &type) {
  if (auto p = get_if<shared_ptr<PromiseType>>(&type)) {
    return (*p)->valueType;
  }
  return type;
}

// Wrap T in Promise<T>
inline Type make_promise_type(const Type &inner) {
  auto pt = make_shared<PromiseType>();
  pt->valueType = inner;
  return Type(pt);
}

inline bool is_signed(const Type &type) {
  if (holds_alternative<BuiltinType>(type)) {
    const auto btype = get<BuiltinType>(type);
    return btype == SignedInt16 || btype == SignedInt32 ||
           btype == SignedInt64 || btype == SignedInt128;
  }
  return false;
}

inline bool is_unsigned(const Type &type) {
  if (holds_alternative<BuiltinType>(type)) {
    const auto btype = get<BuiltinType>(type);
    return btype == UnsignedInt16 || btype == UnsignedInt32 ||
           btype == UnsignedInt64 || btype == UnsignedInt128;
  }
  return false;
}

inline bool is_float(const Type &type) {
  if (holds_alternative<BuiltinType>(type)) {
    const auto btype = get<BuiltinType>(type);
    return btype == Float32 || btype == Float64 || btype == Float128;
  }
  return false;
}

inline bool is_integer(const Type &type) {
  if (holds_alternative<BuiltinType>(type)) {
    const auto btype = get<BuiltinType>(type);
    return is_signed(type) || is_unsigned(type);
  }
  return false;
}

inline bool is_numeric(const Type &type) {
  if (holds_alternative<BuiltinType>(type)) {
    const auto btype = get<BuiltinType>(type);
    return btype == Byte || is_integer(type) || is_float(type);
  }

  return false;
}

/// Return the derived numeric result, or the nullptr_t sentinel when the
/// operand pair is unsupported. The function retains no references.
inline Type derive_bin_op_result_type(const Type &lhs, const Type &rhs) {
  if (lhs == rhs && is_numeric(lhs)) {
    return lhs;
  }

  if (is_numeric(lhs) && is_numeric(rhs)) {
    return max(get<BuiltinType>(lhs), get<BuiltinType>(rhs));
  }

  return nullptr;
}
} // namespace yona::compiler::types

#endif /* YONA_MODEL_TYPES_H */
