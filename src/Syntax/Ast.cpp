#include "yona/Syntax/Ast.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <utility>
#include <variant>

using std::endl;
using yona::compiler::types::BuiltinTypeStrings;
#include "yona/Syntax/Utils.h"

namespace yona::ast {

using compiler::types::Type;

namespace {

class StreamFormatGuard {
  std::ostream &stream_;
  std::ios_base::fmtflags flags_;
  std::streamsize precision_;
  std::streamsize width_;
  char fill_;

public:
  explicit StreamFormatGuard(std::ostream &stream)
      : stream_(stream), flags_(stream.flags()), precision_(stream.precision()),
        width_(stream.width()), fill_(stream.fill()) {
    stream_.width(0);
  }

  ~StreamFormatGuard() {
    stream_.flags(flags_);
    stream_.precision(precision_);
    stream_.fill(fill_);
    stream_.width(width_);
  }

  StreamFormatGuard(const StreamFormatGuard &) = delete;
  StreamFormatGuard &operator=(const StreamFormatGuard &) = delete;
};

void writeUnicodeEscape(std::ostream &os, uint32_t scalar) {
  constexpr char hex_digits[] = "0123456789ABCDEF";
  const size_t digits = scalar <= 0xFFFF ? 4 : 8;
  char escape[10] = {'\\', digits == 4 ? 'u' : 'U'};
  for (size_t offset = 0; offset < digits; ++offset) {
    escape[digits + 1 - offset] = hex_digits[scalar & 0xF];
    scalar >>= 4;
  }
  os.write(escape, static_cast<std::streamsize>(digits + 2));
}

} // namespace

template <class T>
  requires derived_from<T, AstNode>
T *AstNode::parent_of_type() {
  if (parent == nullptr) {
    return nullptr;
  }
  AstNode *tmp = parent;
  do {
    if (const auto result = dynamic_cast<T *>(tmp); result != nullptr) {
      return result;
    }
    tmp = tmp->parent;
  } while (tmp != nullptr);
  return nullptr;
}

template <typename T>
LiteralExpr<T>::LiteralExpr(SourceRange token, T value)
    : LiteralExpr<void *>(token), value(std::move(value)) {}

// Template accept methods are now defined in the header

BinaryOpExpr::BinaryOpExpr(SourceRange token, ExprNode *left, ExprNode *right)
    : OpExpr(token), left(left ? left->with_parent<ExprNode>(this) : nullptr),
      right(right ? right->with_parent<ExprNode>(this) : nullptr) {}

BinaryOpExpr::~BinaryOpExpr() {
  delete left;
  delete right;
}

NameExpr::NameExpr(SourceRange token, string value)
    : ExprNode(token), value(std::move(value)) {}

void NameExpr::print(std::ostream &os) const { os << value; }

IdentifierExpr::IdentifierExpr(SourceRange token, NameExpr *name)
    : ValueExpr(token), name(name->with_parent<NameExpr>(this)) {}

IdentifierExpr::~IdentifierExpr() { delete name; }

void IdentifierExpr::print(std::ostream &os) const { os << *name; }

RecordNode::RecordNode(
    SourceRange token, NameExpr *recordType,
    vector<pair<IdentifierExpr *, TypeDefinition *>> identifiers)
    : ExprNode(token), recordType(recordType->with_parent<NameExpr>(this)),
      identifiers(nodes_with_parent(std::move(identifiers), this)) {}

RecordNode::~RecordNode() {
  delete recordType;
  for (const auto [fst, snd] : identifiers) {
    delete fst;
    delete snd;
  }
}

void RecordNode::print(std::ostream &os) const {
  os << *recordType << '(';
  size_t i = 0;
  for (const auto [ident, type_def] : identifiers) {
    os << *ident;
    if (type_def) {
      os << ':' << *type_def;
    }
    if (i++ < identifiers.size() - 1) {
      os << ", ";
    }
  }
  os << ')';
}

TrueLiteralExpr::TrueLiteralExpr(SourceRange token)
    : LiteralExpr<bool>(token, true) {}

void TrueLiteralExpr::print(std::ostream &os) const { os << "true"; }

FalseLiteralExpr::FalseLiteralExpr(SourceRange token)
    : LiteralExpr<bool>(token, false) {}

void FalseLiteralExpr::print(std::ostream &os) const { os << "false"; }

FloatExpr::FloatExpr(SourceRange token, const double value)
    : LiteralExpr<double>(token, value) {}

void FloatExpr::print(std::ostream &os) const { os << value; }

IntegerExpr::IntegerExpr(SourceRange token, const int64_t value)
    : LiteralExpr<int64_t>(token, value) {}

void IntegerExpr::print(std::ostream &os) const { os << value; }

void UnderscoreNode::print(std::ostream &os) const { os << "_"; }

ByteExpr::ByteExpr(SourceRange token, const unsigned char value)
    : LiteralExpr<unsigned char>(token, value) {}

void ByteExpr::print(std::ostream &os) const { os << value; }

StringExpr::StringExpr(SourceRange token, string value)
    : LiteralExpr<string>(token, std::move(value)) {}

void StringExpr::print(std::ostream &os) const { os << value; }

CharacterExpr::CharacterExpr(SourceRange token, const wchar_t value)
    : LiteralExpr<wchar_t>(token, value) {}

void CharacterExpr::print(std::ostream &os) const {
  StreamFormatGuard format_guard(os);
  const auto scalar = static_cast<uint32_t>(static_cast<char32_t>(value));
  os.put('\'');
  switch (scalar) {
  case '\n':
    os.write("\\n", 2);
    break;
  case '\r':
    os.write("\\r", 2);
    break;
  case '\t':
    os.write("\\t", 2);
    break;
  case '\\':
    os.write("\\\\", 2);
    break;
  case '\'':
    os.write("\\'", 2);
    break;
  case '\0':
    os.write("\\0", 2);
    break;
  default:
    if (scalar >= 32 && scalar <= 126) {
      os.put(static_cast<char>(scalar));
    } else {
      writeUnicodeEscape(os, scalar);
    }
    break;
  }
  os.put('\'');
}

UnitExpr::UnitExpr(SourceRange token)
    : LiteralExpr<nullptr_t>(token, nullptr) {}

void UnitExpr::print(std::ostream &os) const { os << "()"; }

TupleExpr::TupleExpr(SourceRange token, vector<ExprNode *> values)
    : ValueExpr(token), values(nodes_with_parent(std::move(values), this)) {}

TupleExpr::~TupleExpr() {
  for (const auto p : values)
    delete p;
}

void TupleExpr::print(std::ostream &os) const {
  os << '(';
  for (size_t i = 0; i < values.size(); i++) {
    os << *values[i];
    if (i < values.size() - 1) {
      os << ", ";
    }
  }
  os << ')';
}

DictExpr::DictExpr(SourceRange token,
                   vector<pair<ExprNode *, ExprNode *>> values)
    : ValueExpr(token), values(nodes_with_parent(std::move(values), this)) {}

DictExpr::~DictExpr() {
  for (const auto [fst, snd] : values) {
    delete fst;
    delete snd;
  }
}

void DictExpr::print(std::ostream &os) const {
  os << '{';
  for (size_t i = 0; i < values.size(); i++) {
    os << values[i].first << ": " << values[i].second;
    if (i < values.size() - 1) {
      os << ", ";
    }
  }
  os << '}';
}

ValuesSequenceExpr::ValuesSequenceExpr(SourceRange token,
                                       vector<ExprNode *> values)
    : SequenceExpr(token), values(nodes_with_parent(std::move(values), this)) {}

ValuesSequenceExpr::~ValuesSequenceExpr() {
  for (const auto p : values)
    delete p;
}

void ValuesSequenceExpr::print(std::ostream &os) const {
  os << '[';
  for (size_t i = 0; i < values.size(); i++) {
    os << values[i];
    if (i < values.size() - 1) {
      os << ", ";
    }
  }
  os << ']';
}

RangeSequenceExpr::RangeSequenceExpr(SourceRange token, ExprNode *start,
                                     ExprNode *end, ExprNode *step)
    : SequenceExpr(token), start(start->with_parent<ExprNode>(this)),
      end(end->with_parent<ExprNode>(this)),
      step(step ? step->with_parent<ExprNode>(this) : nullptr) {}

RangeSequenceExpr::~RangeSequenceExpr() {
  delete start;
  delete end;
  delete step;
}

void RangeSequenceExpr::print(std::ostream &os) const {
  os << '[';
  if (step) {
    os << *step << ", ";
  }
  os << *start << ".." << *end << ']';
}

SetExpr::SetExpr(SourceRange token, vector<ExprNode *> values)
    : ValueExpr(token), values(nodes_with_parent(std::move(values), this)) {}

SetExpr::~SetExpr() {
  for (const auto p : values)
    delete p;
}

void SetExpr::print(std::ostream &os) const {
  os << '{';
  for (size_t i = 0; i < values.size(); i++) {
    os << *values[i];
    if (i < values.size() - 1) {
      os << ", ";
    }
  }
  os << '}';
}

SymbolExpr::SymbolExpr(SourceRange token, string value)
    : ValueExpr(token), value(std::move(value)) {}

void SymbolExpr::print(std::ostream &os) const { os << ':' << value; }

PackageNameExpr::PackageNameExpr(SourceRange token, vector<NameExpr *> parts)
    : ValueExpr(token), parts(nodes_with_parent(std::move(parts), this)) {}

PackageNameExpr::~PackageNameExpr() {
  for (const auto p : parts)
    delete p;
}

string PackageNameExpr::to_string() const {
  vector<string> names;
  names.reserve(parts.size());
  for (const auto part : parts) {
    names.push_back(part->value);
  }
  return std::accumulate(std::next(names.begin()), names.end(),
                         names.empty() ? std::string() : names.front(),
                         [](const std::string &a, const std::string &b) {
                           return a + PACKAGE_DELIMITER + b;
                         });
}

void PackageNameExpr::print(std::ostream &os) const { os << to_string(); }

FqnExpr::FqnExpr(SourceRange token, optional<PackageNameExpr *> packageName,
                 NameExpr *moduleName)
    : ValueExpr(token),
      packageName(node_with_parent<PackageNameExpr>(packageName, this)),
      moduleName(moduleName->with_parent<NameExpr>(this)) {}

FqnExpr::~FqnExpr() {
  if (packageName.has_value()) {
    delete packageName.value();
  }
  delete moduleName;
}

string FqnExpr::to_string() const {
  string result =
      packageName.transform([](auto val) { return val->to_string(); })
          .value_or("");
  if (!result.empty()) {
    result += PACKAGE_DELIMITER;
  }
  result += moduleName->value;
  return result;
}

void FqnExpr::print(std::ostream &os) const { os << to_string(); }

FunctionExpr::FunctionExpr(SourceRange token, string name,
                           vector<PatternNode *> patterns,
                           vector<FunctionBody *> bodies,
                           std::optional<compiler::types::Type> type_sig)
    : ScopedNode(token), name(std::move(name)),
      patterns(nodes_with_parent(std::move(patterns), this)),
      bodies(nodes_with_parent(std::move(bodies), this)),
      type_signature(std::move(type_sig)) {}

FunctionExpr::~FunctionExpr() {
  for (const auto p : patterns)
    delete p;
  for (const auto p : bodies)
    delete p;
}

void FunctionExpr::print(std::ostream &os) const {
  os << name << " ";
  size_t i = 0;
  for (const auto p : patterns) {
    if (i < param_borrow.size() && param_borrow[i])
      os << "@borrow ";
    os << *p;
    if (++i < patterns.size()) {
      os << ' ';
    }
  }
  for (const auto p : bodies) {
    os << *p;
  }
}

BodyWithGuards::BodyWithGuards(SourceRange token, ExprNode *guard,
                               ExprNode *expr)
    : FunctionBody(token), guard(guard->with_parent<ExprNode>(this)),
      expr(expr->with_parent<ExprNode>(this)) {}

void BodyWithGuards::print(std::ostream &os) const {
  os << " | " << *guard << " = " << *expr;
}

BodyWithGuards::~BodyWithGuards() {
  delete guard;
  delete expr;
}

BodyWithoutGuards::BodyWithoutGuards(SourceRange token, ExprNode *expr)
    : FunctionBody(token), expr(expr->with_parent<ExprNode>(this)) {}

BodyWithoutGuards::~BodyWithoutGuards() { delete expr; }

void BodyWithoutGuards::print(std::ostream &os) const { os << " = " << *expr; }

ModuleDecl::ModuleDecl(
    SourceRange token, FqnExpr *fqn, const vector<string> &exports,
    const vector<string> &exported_types,
    const vector<string> &opaque_exported_types,
    const vector<string> &exported_traits, const vector<ReExport> &re_exports,
    const vector<FunctionExpr *> &functions,
    const vector<FunctionDeclaration *> &function_declarations,
    const vector<AdtDeclNode *> &adt_declarations,
    const vector<TraitDeclNode *> &trait_declarations,
    const vector<InstanceDeclNode *> &instance_declarations,
    const vector<ExternDeclExpr *> &extern_declarations)
    : AstNode(token), fqn(fqn->with_parent<FqnExpr>(this)), exports(exports),
      exported_types(exported_types),
      opaque_exported_types(opaque_exported_types),
      exported_traits(exported_traits), re_exports(re_exports),
      functions(nodes_with_parent(std::move(functions), this)),
      functionDeclarations(
          nodes_with_parent(std::move(function_declarations), this)),
      extern_declarations(extern_declarations),
      adt_declarations(nodes_with_parent(std::move(adt_declarations), this)),
      trait_declarations(
          nodes_with_parent(std::move(trait_declarations), this)),
      instance_declarations(
          nodes_with_parent(std::move(instance_declarations), this)) {}

ModuleDecl::~ModuleDecl() {
  delete fqn;
  for (const auto p : functions)
    delete p;
  for (const auto p : functionDeclarations)
    delete p;
  for (const auto p : adt_declarations)
    delete p;
  for (const auto p : trait_declarations)
    delete p;
  for (const auto p : instance_declarations)
    delete p;
  for (const auto p : extern_declarations)
    delete p;
}

void ModuleDecl::print(std::ostream &os) const {
  os << "module " << *fqn << endl;

  if (!exports.empty()) {
    os << "export ";
    for (size_t i = 0; i < exports.size(); i++) {
      os << exports[i];
      if (i < exports.size() - 1) {
        os << ", ";
      }
    }
    os << endl;
  }

  for (const auto &t : exported_types) {
    os << "export type " << t;
    if (std::find(opaque_exported_types.begin(), opaque_exported_types.end(),
                  t) != opaque_exported_types.end())
      os << " opaque";
    os << endl;
  }

  for (const auto &t : exported_traits) {
    os << "export trait " << t << endl;
  }

  for (const auto p : trait_declarations) {
    os << *p << endl;
  }

  for (const auto p : instance_declarations) {
    os << *p << endl;
  }

  for (const auto p : functionDeclarations) {
    os << *p << endl;
  }

  for (const auto p : functions) {
    os << *p << endl;
  }
}

RecordInstanceExpr::RecordInstanceExpr(
    SourceRange token, NameExpr *recordType,
    vector<pair<NameExpr *, ExprNode *>> items)
    : ValueExpr(token), recordType(recordType->with_parent<NameExpr>(this)),
      items(nodes_with_parent(std::move(items), this)) {}

RecordInstanceExpr::~RecordInstanceExpr() {
  delete recordType;
  for (const auto [fst, snd] : items) {
    delete fst;
    delete snd;
  }
}

void RecordInstanceExpr::print(std::ostream &os) const {
  os << *recordType << '(';

  for (size_t i = 0; i < items.size(); i++) {
    os << *items[i].first << " = " << *items[i].second;
    if (i < items.size() - 1) {
      os << ", ";
    }
  }

  os << ')';
}

LogicalNotOpExpr::LogicalNotOpExpr(SourceRange token, ExprNode *expr)
    : OpExpr(token), expr(expr->with_parent<ExprNode>(this)) {}

LogicalNotOpExpr::~LogicalNotOpExpr() { delete expr; }

void LogicalNotOpExpr::print(std::ostream &os) const { os << '!' << *expr; }

BinaryNotOpExpr::BinaryNotOpExpr(SourceRange token, ExprNode *expr)
    : OpExpr(token), expr(expr->with_parent<ExprNode>(this)) {}

BinaryNotOpExpr::~BinaryNotOpExpr() { delete expr; }

void BinaryNotOpExpr::print(std::ostream &os) const { os << '~' << *expr; }

PowerExpr::PowerExpr(SourceRange token, ExprNode *left, ExprNode *right)
    : BinaryOpExpr(token, left, right) {}

void PowerExpr::print(std::ostream &os) const { os << *left << "**" << *right; }

MultiplyExpr::MultiplyExpr(SourceRange token, ExprNode *left, ExprNode *right)
    : BinaryOpExpr(token, left, right) {}

void MultiplyExpr::print(std::ostream &os) const {
  os << *left << "*" << *right;
}

DivideExpr::DivideExpr(SourceRange token, ExprNode *left, ExprNode *right)
    : BinaryOpExpr(token, left, right) {}

void DivideExpr::print(std::ostream &os) const { os << *left << "/" << *right; }

ModuloExpr::ModuloExpr(SourceRange token, ExprNode *left, ExprNode *right)
    : BinaryOpExpr(token, left, right) {}

void ModuloExpr::print(std::ostream &os) const { os << *left << "%" << *right; }

AddExpr::AddExpr(SourceRange token, ExprNode *left, ExprNode *right)
    : BinaryOpExpr(token, left, right) {}

void AddExpr::print(std::ostream &os) const { os << *left << "+" << *right; }

SubtractExpr::SubtractExpr(SourceRange token, ExprNode *left, ExprNode *right)
    : BinaryOpExpr(token, left, right) {}

void SubtractExpr::print(std::ostream &os) const {
  os << *left << "-" << *right;
}

LeftShiftExpr::LeftShiftExpr(SourceRange token, ExprNode *left,
                             ExprNode *right)
    : BinaryOpExpr(token, left, right) {}

void LeftShiftExpr::print(std::ostream &os) const {
  os << *left << "<<" << *right;
}

RightShiftExpr::RightShiftExpr(SourceRange token, ExprNode *left,
                               ExprNode *right)
    : BinaryOpExpr(token, left, right) {}

void RightShiftExpr::print(std::ostream &os) const {
  os << *left << ">>" << *right;
}

ZerofillRightShiftExpr::ZerofillRightShiftExpr(SourceRange token,
                                               ExprNode *left, ExprNode *right)
    : BinaryOpExpr(token, left, right) {}

void ZerofillRightShiftExpr::print(std::ostream &os) const {
  os << *left << ">>>" << *right;
}

GteExpr::GteExpr(SourceRange token, ExprNode *left, ExprNode *right)
    : BinaryOpExpr(token, left, right) {}

void GteExpr::print(std::ostream &os) const { os << *left << ">=" << *right; }

LteExpr::LteExpr(SourceRange token, ExprNode *left, ExprNode *right)
    : BinaryOpExpr(token, left, right) {}

void LteExpr::print(std::ostream &os) const { os << *left << "<=" << *right; }

GtExpr::GtExpr(SourceRange token, ExprNode *left, ExprNode *right)
    : BinaryOpExpr(token, left, right) {}

void GtExpr::print(std::ostream &os) const { os << *left << ">" << *right; }

LtExpr::LtExpr(SourceRange token, ExprNode *left, ExprNode *right)
    : BinaryOpExpr(token, left, right) {}

void LtExpr::print(std::ostream &os) const { os << *left << "<" << *right; }

EqExpr::EqExpr(SourceRange token, ExprNode *left, ExprNode *right)
    : BinaryOpExpr(token, left, right) {}

void EqExpr::print(std::ostream &os) const { os << *left << "==" << *right; }

NeqExpr::NeqExpr(SourceRange token, ExprNode *left, ExprNode *right)
    : BinaryOpExpr(token, left, right) {}

void NeqExpr::print(std::ostream &os) const { os << *left << "!=" << *right; }

ConsLeftExpr::ConsLeftExpr(SourceRange token, ExprNode *left, ExprNode *right)
    : BinaryOpExpr(token, left, right) {}

void ConsLeftExpr::print(std::ostream &os) const {
  os << *left << "-|" << *right;
}

ConsRightExpr::ConsRightExpr(SourceRange token, ExprNode *left,
                             ExprNode *right)
    : BinaryOpExpr(token, left, right) {}

void ConsRightExpr::print(std::ostream &os) const {
  os << *left << "|-" << *right;
}

JoinExpr::JoinExpr(SourceRange token, ExprNode *left, ExprNode *right)
    : BinaryOpExpr(token, left, right) {}

void JoinExpr::print(std::ostream &os) const { os << *left << "++" << *right; }

RemoveExpr::RemoveExpr(SourceRange token, ExprNode *left, ExprNode *right)
    : BinaryOpExpr(token, left, right) {}

void RemoveExpr::print(std::ostream &os) const {
  os << *left << "--" << *right;
}

BitwiseAndExpr::BitwiseAndExpr(SourceRange token, ExprNode *left,
                               ExprNode *right)
    : BinaryOpExpr(token, left, right) {}

void BitwiseAndExpr::print(std::ostream &os) const {
  os << *left << "&" << *right;
}

BitwiseXorExpr::BitwiseXorExpr(SourceRange token, ExprNode *left,
                               ExprNode *right)
    : BinaryOpExpr(token, left, right) {}

void BitwiseXorExpr::print(std::ostream &os) const {
  os << *left << "^" << *right;
}

BitwiseOrExpr::BitwiseOrExpr(SourceRange token, ExprNode *left,
                             ExprNode *right)
    : BinaryOpExpr(token, left, right) {}

void BitwiseOrExpr::print(std::ostream &os) const {
  os << *left << "|" << *right;
}

LogicalAndExpr::LogicalAndExpr(SourceRange token, ExprNode *left,
                               ExprNode *right)
    : BinaryOpExpr(token, left, right) {}

void LogicalAndExpr::print(std::ostream &os) const {
  os << *left << "&&" << *right;
}

LogicalOrExpr::LogicalOrExpr(SourceRange token, ExprNode *left,
                             ExprNode *right)
    : BinaryOpExpr(token, left, right) {}

void LogicalOrExpr::print(std::ostream &os) const {
  os << *left << "||" << *right;
}

InExpr::InExpr(SourceRange token, ExprNode *left, ExprNode *right)
    : BinaryOpExpr(token, left, right) {}

void InExpr::print(std::ostream &os) const { os << *left << " in " << *right; }

PipeLeftExpr::PipeLeftExpr(SourceRange token, ExprNode *left, ExprNode *right)
    : BinaryOpExpr(token, left, right) {}

void PipeLeftExpr::print(std::ostream &os) const {
  os << *left << " <| " << *right;
}

PipeRightExpr::PipeRightExpr(SourceRange token, ExprNode *left,
                             ExprNode *right)
    : BinaryOpExpr(token, left, right) {}

void PipeRightExpr::print(std::ostream &os) const {
  os << *left << " |> " << *right;
}

LetExpr::LetExpr(SourceRange token, vector<AliasExpr *> aliases,
                 ExprNode *expr)
    : ScopedNode(token), aliases(nodes_with_parent(std::move(aliases), this)),
      expr(expr->with_parent<ExprNode>(this)) {}

LetExpr::~LetExpr() {
  for (const auto p : aliases)
    delete p;
  delete expr;
}

void LetExpr::print(std::ostream &os) const {
  os << "let" << endl;
  for (const auto p : aliases) {
    os << "\t" << *p << endl;
  }
  os << "in" << endl << "\t" << *expr;
}

MainNode::MainNode(SourceRange token, AstNode *node)
    : ScopedNode(token), node(node->with_parent<AstNode>(this)) {}

MainNode::~MainNode() { delete node; }

void MainNode::print(std::ostream &os) const { os << "main = " << *node; }

IfExpr::IfExpr(SourceRange token, ExprNode *condition, ExprNode *thenExpr,
               ExprNode *elseExpr)
    : ExprNode(token),
      condition(condition ? condition->with_parent<ExprNode>(this) : nullptr),
      thenExpr(thenExpr ? thenExpr->with_parent<ExprNode>(this) : nullptr),
      elseExpr(elseExpr ? elseExpr->with_parent<ExprNode>(this) : nullptr) {}

IfExpr::~IfExpr() {
  delete condition;
  delete thenExpr;
  delete elseExpr;
}

void IfExpr::print(std::ostream &os) const {
  os << "if " << *condition << " then " << *thenExpr;
  if (elseExpr) {
    os << " else " << *elseExpr;
  }
}

ApplyExpr::ApplyExpr(SourceRange token, CallExpr *call,
                     vector<variant<ExprNode *, ValueExpr *>> args)
    : ExprNode(token), call(call->with_parent<CallExpr>(this)),
      args(nodes_with_parent(std::move(args), this)) {}

ApplyExpr::~ApplyExpr() {
  delete call;
  for (const auto p : args) {
    if (holds_alternative<ExprNode *>(p)) {
      delete get<ExprNode *>(p);
    } else {
      delete get<ValueExpr *>(p);
    }
  }
}

void ApplyExpr::print(std::ostream &os) const {
  os << *call << '(';
  size_t i = 0;
  for (const auto p : args) {
    if (holds_alternative<ExprNode *>(p)) {
      os << *get<ExprNode *>(p);
    } else {
      os << *get<ValueExpr *>(p);
    }
    if (i++ < args.size() - 1) {
      os << ", ";
    }
  }
  os << ')';
}

DoExpr::DoExpr(SourceRange token, vector<ExprNode *> steps)
    : ExprNode(token), steps(steps) {}

DoExpr::~DoExpr() {
  for (const auto p : steps) {
    delete p;
  }
}

void DoExpr::print(std::ostream &os) const {
  os << "do" << endl;
  for (const auto p : steps) {
    os << "  " << *p << endl;
  }
  os << "end";
}

ImportExpr::ImportExpr(SourceRange token, vector<ImportClauseExpr *> clauses,
                       ExprNode *expr)
    : ScopedNode(token), clauses(nodes_with_parent(std::move(clauses), this)),
      expr(expr->with_parent<ExprNode>(this)) {}

ImportExpr::~ImportExpr() {
  for (const auto p : clauses) {
    delete p;
  }
  delete expr;
}

void ImportExpr::print(std::ostream &os) const {
  os << "import" << endl;
  for (const auto p : clauses) {
    os << "  " << *p << endl;
  }
  os << "in" << endl;
  os << "  " << *expr;
}

ExternDeclExpr::ExternDeclExpr(SourceRange token, string name,
                               compiler::types::Type type, ExprNode *body,
                               ExternPromiseKind ext_promise)
    : ExprNode(token), name(std::move(name)), declared_type(std::move(type)),
      body(body ? body->with_parent<ExprNode>(this) : nullptr),
      extern_promise(ext_promise) {}

ExternDeclExpr::~ExternDeclExpr() { delete body; }

void ExternDeclExpr::print(std::ostream &os) const {
  os << "extern " << name << " in " << *body;
}

RaiseExpr::RaiseExpr(SourceRange token, ExprNode *value)
    : ExprNode(token), value(value->with_parent<ExprNode>(this)) {}

RaiseExpr::~RaiseExpr() { delete value; }

void RaiseExpr::print(std::ostream &os) const { os << "raise " << *value; }

WithExpr::WithExpr(SourceRange token, bool daemon, ExprNode *contextExpr,
                   NameExpr *name, ExprNode *bodyExpr)
    : ScopedNode(token), daemon(daemon),
      contextExpr(contextExpr->with_parent<ExprNode>(this)),
      name(name->with_parent<NameExpr>(this)),
      bodyExpr(bodyExpr->with_parent<ExprNode>(this)) {}

WithExpr::~WithExpr() {
  delete contextExpr;
  delete name;
  delete bodyExpr;
}

void WithExpr::print(std::ostream &os) const {
  os << "with ";
  if (daemon) {
    os << "daemon ";
  }
  os << *contextExpr;
  if (name) {
    os << " as " << *name;
  }
  os << endl << *bodyExpr << endl;
  os << "end";
}

FieldAccessExpr::FieldAccessExpr(SourceRange token,
                                 IdentifierExpr *identifier, NameExpr *name)
    : ExprNode(token),
      identifier(identifier->with_parent<IdentifierExpr>(this)),
      name(name->with_parent<NameExpr>(this)) {}

FieldAccessExpr::~FieldAccessExpr() {
  delete identifier;
  delete name;
}

void FieldAccessExpr::print(std::ostream &os) const {
  os << *identifier << '.' << *name;
}

FieldUpdateExpr::FieldUpdateExpr(SourceRange token,
                                 IdentifierExpr *identifier,
                                 vector<pair<NameExpr *, ExprNode *>> updates)
    : ExprNode(token),
      identifier(identifier->with_parent<IdentifierExpr>(this)),
      updates(nodes_with_parent(std::move(updates), this)) {}

FieldUpdateExpr::~FieldUpdateExpr() {
  delete identifier;
  for (const auto [fst, snd] : updates) {
    delete fst;
    delete snd;
  }
}

void FieldUpdateExpr::print(std::ostream &os) const {
  os << *identifier << "(";
  size_t i = 0;
  for (const auto [fst, snd] : updates) {
    os << *fst << " = " << *snd;
    if (i++ < updates.size() - 1) {
      os << ", ";
    }
  }
  os << ")";
}

LambdaAlias::LambdaAlias(SourceRange token, NameExpr *name,
                         FunctionExpr *lambda)
    : AliasExpr(token), name(name->with_parent<NameExpr>(this)),
      lambda(lambda->with_parent<FunctionExpr>(this)) {}

LambdaAlias::~LambdaAlias() {
  delete name;
  delete lambda;
}

void LambdaAlias::print(std::ostream &os) const {
  os << *name << " = " << *lambda;
}

ValueAlias::ValueAlias(SourceRange token, IdentifierExpr *identifier,
                       ExprNode *expr)
    : AliasExpr(token),
      identifier(identifier->with_parent<IdentifierExpr>(this)),
      expr(expr->with_parent<ExprNode>(this)) {}

ValueAlias::~ValueAlias() {
  delete identifier;
  delete expr;
}

void ValueAlias::print(std::ostream &os) const {
  os << *identifier << " = " << *expr;
}

PatternAlias::PatternAlias(SourceRange token, PatternNode *pattern,
                           ExprNode *expr)
    : AliasExpr(token), pattern(pattern->with_parent<PatternNode>(this)),
      expr(expr->with_parent<ExprNode>(this)) {}

PatternAlias::~PatternAlias() {
  delete pattern;
  delete expr;
}

void PatternAlias::print(std::ostream &os) const {
  os << *pattern << " = " << *expr;
}

FqnAlias::FqnAlias(SourceRange token, NameExpr *name, FqnExpr *fqn)
    : AliasExpr(token), name(name->with_parent<NameExpr>(this)),
      fqn(fqn->with_parent<FqnExpr>(this)) {}

FqnAlias::~FqnAlias() {
  delete name;
  delete fqn;
}

void FqnAlias::print(std::ostream &os) const { os << *name << " = " << *fqn; }

FunctionAlias::FunctionAlias(SourceRange token, NameExpr *name,
                             NameExpr *alias)
    : AliasExpr(token), name(name->with_parent<NameExpr>(this)),
      alias(alias ? alias->with_parent<NameExpr>(this) : nullptr) {}

FunctionAlias::~FunctionAlias() {
  delete name;
  delete alias;
}

void FunctionAlias::print(std::ostream &os) const {
  os << *name << " = " << *alias;
}

AliasCall::AliasCall(SourceRange token, NameExpr *alias, NameExpr *funName)
    : CallExpr(token), alias(alias->with_parent<NameExpr>(this)),
      funName(funName->with_parent<NameExpr>(this)) {}

AliasCall::~AliasCall() {
  delete alias;
  delete funName;
}

void AliasCall::print(std::ostream &os) const {
  os << *alias << "::" << *funName;
}

NameCall::NameCall(SourceRange token, NameExpr *name)
    : CallExpr(token), name(name->with_parent<NameExpr>(this)) {}

NameCall::~NameCall() { delete name; }

void NameCall::print(std::ostream &os) const { os << *name; }

ModuleCall::ModuleCall(SourceRange token,
                       const variant<FqnExpr *, ExprNode *> &fqn,
                       NameExpr *funName)
    : CallExpr(token), fqn(node_with_parent(fqn, this)),
      funName(funName->with_parent<NameExpr>(this)) {}

ModuleCall::~ModuleCall() {
  if (holds_alternative<FqnExpr *>(fqn)) {
    delete get<FqnExpr *>(fqn);
  } else {
    delete get<ExprNode *>(fqn);
  }
  delete funName;
}

void ModuleCall::print(std::ostream &os) const {
  if (holds_alternative<FqnExpr *>(fqn)) {
    os << *get<FqnExpr *>(fqn);
  } else {
    os << *get<ExprNode *>(fqn);
  }
  os << "::" << *funName;
}

ExprCall::ExprCall(SourceRange token, ExprNode *expr)
    : CallExpr(token), expr(expr->with_parent<ExprNode>(this)) {}

ExprCall::~ExprCall() { delete expr; }

void ExprCall::print(std::ostream &os) const { os << "(" << *expr << ")"; }

ModuleImport::ModuleImport(SourceRange token, FqnExpr *fqn, NameExpr *name)
    : ImportClauseExpr(token), fqn(fqn->with_parent<FqnExpr>(this)),
      name(name ? name->with_parent<NameExpr>(this) : nullptr) {}

ModuleImport::~ModuleImport() {
  delete fqn;
  delete name;
}

void ModuleImport::print(std::ostream &os) const {
  os << *fqn;
  if (name) {
    os << " as " << *name;
  }
}

FunctionsImport::FunctionsImport(SourceRange token,
                                 vector<FunctionAlias *> aliases,
                                 FqnExpr *fromFqn)
    : ImportClauseExpr(token),
      aliases(nodes_with_parent(std::move(aliases), this)),
      fromFqn(fromFqn->with_parent<FqnExpr>(this)) {}

FunctionsImport::~FunctionsImport() {
  for (const auto p : aliases) {
    delete p;
  }
  delete fromFqn;
}

void FunctionsImport::print(std::ostream &os) const {
  size_t i = 0;
  for (const auto p : aliases) {
    os << *p;
    if (i++ < aliases.size() - 1) {
      os << ", ";
    }
  }
  os << " from " << *fromFqn;
}

SeqGeneratorExpr::SeqGeneratorExpr(SourceRange token, ExprNode *reducerExpr,
                                   CollectionExtractorExpr *collectionExtractor,
                                   ExprNode *stepExpression)
    : GeneratorExpr(token),
      reducerExpr(reducerExpr->with_parent<ExprNode>(this)),
      collectionExtractor(
          collectionExtractor->with_parent<CollectionExtractorExpr>(this)),
      stepExpression(stepExpression) {}

SeqGeneratorExpr::~SeqGeneratorExpr() {
  delete reducerExpr;
  delete collectionExtractor;
  delete stepExpression;
}

void SeqGeneratorExpr::print(std::ostream &os) const {
  os << "[" << *reducerExpr << " | " << *collectionExtractor << "]";
}

SetGeneratorExpr::SetGeneratorExpr(SourceRange token, ExprNode *reducerExpr,
                                   CollectionExtractorExpr *collectionExtractor,
                                   ExprNode *stepExpression)
    : GeneratorExpr(token),
      reducerExpr(reducerExpr->with_parent<ExprNode>(this)),
      collectionExtractor(
          collectionExtractor->with_parent<CollectionExtractorExpr>(this)),
      stepExpression(stepExpression) {}

SetGeneratorExpr::~SetGeneratorExpr() {
  delete reducerExpr;
  delete collectionExtractor;
  delete stepExpression;
}

void SetGeneratorExpr::print(std::ostream &os) const {
  os << "{" << *reducerExpr << " | " << *collectionExtractor << "}";
}

DictGeneratorReducer::DictGeneratorReducer(SourceRange token, ExprNode *key,
                                           ExprNode *value)
    : ExprNode(token), key(key->with_parent<ExprNode>(this)),
      value(value->with_parent<ExprNode>(this)) {}

DictGeneratorReducer::~DictGeneratorReducer() {
  delete key;
  delete value;
}

void DictGeneratorReducer::print(std::ostream &os) const {
  os << *key << " : " << *value;
}

DictGeneratorExpr::DictGeneratorExpr(
    SourceRange token, DictGeneratorReducer *reducerExpr,
    CollectionExtractorExpr *collectionExtractor, ExprNode *stepExpression)
    : GeneratorExpr(token),
      reducerExpr(reducerExpr->with_parent<DictGeneratorReducer>(this)),
      collectionExtractor(
          collectionExtractor->with_parent<CollectionExtractorExpr>(this)),
      stepExpression(stepExpression) {}

DictGeneratorExpr::~DictGeneratorExpr() {
  delete reducerExpr;
  delete collectionExtractor;
  delete stepExpression;
}

void DictGeneratorExpr::print(std::ostream &os) const {
  os << "{" << *reducerExpr << " | " << *collectionExtractor << "}";
}

ValueCollectionExtractorExpr::ValueCollectionExtractorExpr(
    SourceRange token, const IdentifierOrUnderscore identifier_or_underscore,
    ExprNode *collection, ExprNode *condition)
    : CollectionExtractorExpr(token),
      expr(node_with_parent(identifier_or_underscore, this)),
      collection(collection ? collection->with_parent<ExprNode>(this)
                            : nullptr),
      condition(condition ? condition->with_parent<ExprNode>(this) : nullptr) {}

void release_identifier_or_underscore(const IdentifierOrUnderscore expr) {
  if (holds_alternative<IdentifierExpr *>(expr)) {
    delete get<IdentifierExpr *>(expr);
  } else {
    delete get<UnderscoreNode *>(expr);
  }
}

ValueCollectionExtractorExpr::~ValueCollectionExtractorExpr() {
  release_identifier_or_underscore(expr);
  delete collection;
  delete condition;
}

void ValueCollectionExtractorExpr::print(std::ostream &os) const {
  if (holds_alternative<IdentifierExpr *>(expr)) {
    os << *get<IdentifierExpr *>(expr);
  } else {
    os << *get<UnderscoreNode *>(expr);
  }
}

KeyValueCollectionExtractorExpr::KeyValueCollectionExtractorExpr(
    SourceRange token, const IdentifierOrUnderscore keyExpr,
    const IdentifierOrUnderscore valueExpr, ExprNode *collection,
    ExprNode *condition)
    : CollectionExtractorExpr(token), keyExpr(node_with_parent(keyExpr, this)),
      valueExpr(node_with_parent(valueExpr, this)),
      collection(collection ? collection->with_parent<ExprNode>(this)
                            : nullptr),
      condition(condition ? condition->with_parent<ExprNode>(this) : nullptr) {}

KeyValueCollectionExtractorExpr::~KeyValueCollectionExtractorExpr() {
  release_identifier_or_underscore(keyExpr);
  release_identifier_or_underscore(valueExpr);
  delete collection;
  delete condition;
}

void KeyValueCollectionExtractorExpr::print(std::ostream &os) const {
  if (holds_alternative<IdentifierExpr *>(keyExpr)) {
    os << *get<IdentifierExpr *>(keyExpr);
  } else {
    os << *get<UnderscoreNode *>(keyExpr);
  }
  os << " = ";
  if (holds_alternative<IdentifierExpr *>(valueExpr)) {
    os << *get<IdentifierExpr *>(valueExpr);
  } else {
    os << *get<UnderscoreNode *>(valueExpr);
  }
}

PatternWithGuards::PatternWithGuards(SourceRange token, ExprNode *guard,
                                     ExprNode *expr)
    : PatternNode(token), guard(guard->with_parent<ExprNode>(this)),
      expr(expr->with_parent<ExprNode>(this)) {}

PatternWithGuards::~PatternWithGuards() {
  delete guard;
  delete expr;
}

void PatternWithGuards::print(std::ostream &os) const {
  os << *expr << " if " << *guard;
}

PatternWithoutGuards::PatternWithoutGuards(SourceRange token, ExprNode *expr)
    : PatternNode(token), expr(expr->with_parent<ExprNode>(this)) {}

PatternWithoutGuards::~PatternWithoutGuards() { delete expr; }

void PatternWithoutGuards::print(std::ostream &os) const { os << *expr; }

PatternExpr::PatternExpr(
    SourceRange token,
    const variant<Pattern *, PatternWithoutGuards *,
                  vector<PatternWithGuards *>> &patternExpr)
    : ExprNode(token), patternExpr(patternExpr) {
  // Set parent pointers for the pattern expressions
  if (holds_alternative<Pattern *>(this->patternExpr)) {
    get<Pattern *>(this->patternExpr)->parent = this;
  } else if (holds_alternative<PatternWithoutGuards *>(this->patternExpr)) {
    get<PatternWithoutGuards *>(this->patternExpr)->parent = this;
  } else {
    for (auto *p : get<vector<PatternWithGuards *>>(this->patternExpr)) {
      p->parent = this;
    }
  }
}

PatternExpr::~PatternExpr() {
  if (holds_alternative<Pattern *>(patternExpr)) {
    delete get<Pattern *>(patternExpr);
  } else if (holds_alternative<PatternWithoutGuards *>(patternExpr)) {
    delete get<PatternWithoutGuards *>(patternExpr);
  } else {
    for (const auto p : get<vector<PatternWithGuards *>>(patternExpr)) {
      delete p;
    }
  }
}

void PatternExpr::print(std::ostream &os) const {
  if (holds_alternative<Pattern *>(patternExpr)) {
    os << *get<Pattern *>(patternExpr);
  } else if (holds_alternative<PatternWithoutGuards *>(patternExpr)) {
    os << *get<PatternWithoutGuards *>(patternExpr);
  } else {
    size_t i = 0;
    for (const auto p : get<vector<PatternWithGuards *>>(patternExpr)) {
      os << *p;
      if (i++ < get<vector<PatternWithGuards *>>(patternExpr).size() - 1) {
        os << endl;
      }
    }
  }
}

CatchPatternExpr::CatchPatternExpr(
    SourceRange token, Pattern *matchPattern,
    const variant<PatternWithoutGuards *, vector<PatternWithGuards *>> &pattern)
    : ExprNode(token), matchPattern(matchPattern->with_parent<Pattern>(this)),
      pattern(pattern) {
  // Set parent pointers for the pattern expressions
  if (holds_alternative<PatternWithoutGuards *>(this->pattern)) {
    get<PatternWithoutGuards *>(this->pattern)->parent = this;
  } else {
    for (auto *p : get<vector<PatternWithGuards *>>(this->pattern)) {
      p->parent = this;
    }
  }
}

CatchPatternExpr::~CatchPatternExpr() {
  delete matchPattern;
  if (holds_alternative<PatternWithoutGuards *>(pattern)) {
    delete get<PatternWithoutGuards *>(pattern);
  } else {
    for (const auto p : get<vector<PatternWithGuards *>>(pattern)) {
      delete p;
    }
  }
}

void CatchPatternExpr::print(std::ostream &os) const {
  os << *matchPattern << " ";
  if (holds_alternative<PatternWithoutGuards *>(pattern)) {
    os << *get<PatternWithoutGuards *>(pattern);
  } else {
    size_t i = 0;
    for (const auto p : get<vector<PatternWithGuards *>>(pattern)) {
      os << *p;
      if (i++ < get<vector<PatternWithGuards *>>(pattern).size() - 1) {
        os << endl;
      }
    }
  }
}

CatchExpr::CatchExpr(SourceRange token, vector<CatchPatternExpr *> patterns)
    : ExprNode(token), patterns(nodes_with_parent(std::move(patterns), this)) {}

CatchExpr::~CatchExpr() {
  for (const auto p : patterns) {
    delete p;
  }
}

void CatchExpr::print(std::ostream &os) const {
  size_t i = 0;
  for (const auto p : patterns) {
    os << *p;
    if (i++ < patterns.size() - 1) {
      os << endl;
    }
  }
}

TryCatchExpr::TryCatchExpr(SourceRange token, ExprNode *tryExpr,
                           CatchExpr *catchExpr)
    : ExprNode(token), tryExpr(tryExpr->with_parent<ExprNode>(this)),
      catchExpr(catchExpr->with_parent<CatchExpr>(this)) {}

TryCatchExpr::~TryCatchExpr() {
  delete tryExpr;
  delete catchExpr;
}

void TryCatchExpr::print(std::ostream &os) const {
  os << "try" << endl
     << "  " << *tryExpr << endl
     << "catch" << endl
     << "  " << *catchExpr << endl;
}

PatternValue::PatternValue(
    SourceRange token,
    const variant<LiteralExpr<nullptr_t> *, LiteralExpr<void *> *, SymbolExpr *,
                  IdentifierExpr *> &expr)
    : PatternNode(token), expr(node_with_parent(expr, this)) {}

PatternValue::~PatternValue() {
  if (holds_alternative<LiteralExpr<nullptr_t> *>(expr)) {
    delete get<LiteralExpr<nullptr_t> *>(expr);
  } else if (holds_alternative<LiteralExpr<void *> *>(expr)) {
    delete get<LiteralExpr<void *> *>(expr);
  } else if (holds_alternative<SymbolExpr *>(expr)) {
    delete get<SymbolExpr *>(expr);
  } else {
    delete get<IdentifierExpr *>(expr);
  }
}

void PatternValue::print(std::ostream &os) const {
  if (holds_alternative<LiteralExpr<nullptr_t> *>(expr)) {
    os << *get<LiteralExpr<nullptr_t> *>(expr);
  } else if (holds_alternative<LiteralExpr<void *> *>(expr)) {
    os << *get<LiteralExpr<void *> *>(expr);
  } else if (holds_alternative<SymbolExpr *>(expr)) {
    os << *get<SymbolExpr *>(expr);
  } else {
    os << *get<IdentifierExpr *>(expr);
  }
}

AsDataStructurePattern::AsDataStructurePattern(SourceRange token,
                                               IdentifierExpr *identifier,
                                               DataStructurePattern *pattern)
    : PatternNode(token),
      identifier(identifier->with_parent<IdentifierExpr>(this)),
      pattern(pattern->with_parent<DataStructurePattern>(this)) {}

AsDataStructurePattern::~AsDataStructurePattern() {
  delete identifier;
  delete pattern;
}

void AsDataStructurePattern::print(std::ostream &os) const {
  os << *identifier << "@(" << *pattern << ")";
}

void UnderscorePattern::print(std::ostream &os) const { os << '_'; }

TuplePattern::TuplePattern(SourceRange token, vector<Pattern *> patterns)
    : PatternNode(token),
      patterns(nodes_with_parent(std::move(patterns), this)) {}

TuplePattern::~TuplePattern() {
  for (const auto p : patterns) {
    delete p;
  }
}

void TuplePattern::print(std::ostream &os) const {
  os << '(';
  size_t i = 0;
  for (const auto p : patterns) {
    os << *p;
    if (i++ < patterns.size() - 1) {
      os << ", ";
    }
  }
  os << ')';
}

SeqPattern::SeqPattern(SourceRange token, vector<Pattern *> patterns)
    : PatternNode(token),
      patterns(nodes_with_parent(std::move(patterns), this)) {}

SeqPattern::~SeqPattern() {
  for (const auto p : patterns) {
    delete p;
  }
}

void SeqPattern::print(std::ostream &os) const {
  os << '[';
  size_t i = 0;
  for (const auto p : patterns) {
    os << *p;
    if (i++ < patterns.size() - 1) {
      os << ", ";
    }
  }
  os << ']';
}

HeadTailsPattern::HeadTailsPattern(SourceRange token,
                                   vector<PatternWithoutSequence *> heads,
                                   TailPattern *tail)
    : PatternNode(token), heads(nodes_with_parent(std::move(heads), this)),
      tail(tail->with_parent<TailPattern>(this)) {}

HeadTailsPattern::~HeadTailsPattern() {
  for (const auto p : heads) {
    delete p;
  }
  delete tail;
}

void HeadTailsPattern::print(std::ostream &os) const {
  size_t i = 0;
  for (const auto p : heads) {
    os << *p;
    if (i++ < heads.size() - 1) {
      os << ", ";
    }
  }
  os << " -| ";
  os << *tail;
}

TailsHeadPattern::TailsHeadPattern(SourceRange token, TailPattern *tail,
                                   vector<PatternWithoutSequence *> heads)
    : PatternNode(token), tail(tail->with_parent<TailPattern>(this)),
      heads(nodes_with_parent(std::move(heads), this)) {}

TailsHeadPattern::~TailsHeadPattern() {
  delete tail;
  for (const auto p : heads) {
    delete p;
  }
}

void TailsHeadPattern::print(std::ostream &os) const {
  os << *tail;
  os << " |- ";
  size_t i = 0;
  for (const auto p : heads) {
    os << *p;
    if (i++ < heads.size() - 1) {
      os << ", ";
    }
  }
}

HeadTailsHeadPattern::HeadTailsHeadPattern(
    SourceRange token, vector<PatternWithoutSequence *> left,
    TailPattern *tail, vector<PatternWithoutSequence *> right)
    : PatternNode(token), left(nodes_with_parent(std::move(left), this)),
      tail(tail->with_parent<TailPattern>(this)),
      right(nodes_with_parent(std::move(right), this)) {}

HeadTailsHeadPattern::~HeadTailsHeadPattern() {
  for (const auto p : left) {
    delete p;
  }
  delete tail;
  for (const auto p : right) {
    delete p;
  }
}

void HeadTailsHeadPattern::print(std::ostream &os) const {
  size_t i = 0;
  for (const auto p : left) {
    os << *p;
    if (i++ < left.size() - 1) {
      os << ", ";
    }
  }
  os << " -| " << tail << " |- ";
  os << *tail;
  i = 0;
  for (const auto p : right) {
    os << *p;
    if (i++ < right.size() - 1) {
      os << ", ";
    }
  }
}

DictPattern::DictPattern(SourceRange token,
                         vector<pair<PatternValue *, Pattern *>> keyValuePairs)
    : PatternNode(token),
      keyValuePairs(nodes_with_parent(std::move(keyValuePairs), this)) {}

DictPattern::~DictPattern() {
  for (const auto [fst, snd] : keyValuePairs) {
    delete fst;
    delete snd;
  }
}

void DictPattern::print(std::ostream &os) const {
  os << '{';
  size_t i = 0;
  for (const auto [fst, snd] : keyValuePairs) {
    os << *fst << " = " << *snd;
    if (i++ < keyValuePairs.size() - 1) {
      os << ", ";
    }
  }
  os << '}';
}

RecordPattern::RecordPattern(SourceRange token, string recordType,
                             vector<pair<NameExpr *, Pattern *>> items)
    : PatternNode(token), recordType(std::move(recordType)),
      items(nodes_with_parent(std::move(items), this)) {}

RecordPattern::~RecordPattern() {
  for (const auto [fst, snd] : items) {
    delete fst;
    delete snd;
  }
}

void RecordPattern::print(std::ostream &os) const {
  os << recordType << " (";
  size_t i = 0;
  for (const auto [fst, snd] : items) {
    os << *fst << " = " << *snd;
    if (i++ < items.size() - 1) {
      os << ", ";
    }
  }
  os << ')';
}

OrPattern::OrPattern(SourceRange token,
                     vector<unique_ptr<PatternNode>> patterns)
    : PatternNode(token), patterns(std::move(patterns)) {
  for (auto &pattern : this->patterns) {
    pattern->parent = this;
  }
}

OrPattern::~OrPattern() = default;

void OrPattern::print(std::ostream &os) const {
  for (size_t i = 0; i < patterns.size(); ++i) {
    os << *patterns[i];
    if (i < patterns.size() - 1) {
      os << " | ";
    }
  }
}

// CaseClause implementation
CaseClause::CaseClause(SourceRange token, Pattern *pattern, ExprNode *body,
                       ExprNode *guard)
    : ExprNode(token), pattern(pattern->with_parent<Pattern>(this)),
      body(body->with_parent<ExprNode>(this)),
      guard(guard ? guard->with_parent<ExprNode>(this) : nullptr) {}

CaseClause::~CaseClause() {
  delete pattern;
  delete body;
  delete guard;
}

void CaseClause::print(std::ostream &os) const {
  os << *pattern;
  if (guard)
    os << " | " << *guard;
  os << " -> " << *body;
}

// CaseExpr implementation
CaseExpr::CaseExpr(SourceRange token, ExprNode *expr,
                   vector<CaseClause *> clauses)
    : ExprNode(token), expr(expr->with_parent<ExprNode>(this)),
      clauses(nodes_with_parent(std::move(clauses), this)) {}

CaseExpr::~CaseExpr() {
  delete expr;
  for (const auto clause : clauses) {
    delete clause;
  }
}

void CaseExpr::print(std::ostream &os) const {
  os << "case " << *expr << " of" << endl;
  for (const auto clause : clauses) {
    os << "  | " << *clause << endl;
  }
}

void BuiltinTypeNode::print(std::ostream &os) const {
  os << BuiltinTypeStrings[type];
}

UserDefinedTypeNode::UserDefinedTypeNode(SourceRange token, NameExpr *name)
    : TypeNameNode(token), name(name->with_parent<NameExpr>(this)) {}

UserDefinedTypeNode::~UserDefinedTypeNode() { delete name; }

void UserDefinedTypeNode::print(std::ostream &os) const { os << *name; }

TypeDeclaration::TypeDeclaration(SourceRange token, TypeNameNode *name,
                                 vector<NameExpr *> type_vars)
    : AstNode(token), name(name->with_parent<TypeNameNode>(this)),
      typeVars(nodes_with_parent(std::move(type_vars), this)) {}

TypeDeclaration::~TypeDeclaration() {
  delete name;
  for (const auto p : typeVars) {
    delete p;
  }
}

void TypeDeclaration::print(std::ostream &os) const {
  os << *name << " ";
  size_t i = 0;
  for (const auto p : typeVars) {
    os << *p;
    if (i++ < typeVars.size() - 1) {
      os << " ";
    }
  }
}

TypeDefinition::TypeDefinition(SourceRange token, TypeNameNode *name,
                               vector<TypeNameNode *> type_names)
    : AstNode(token), name(name->with_parent<TypeNameNode>(this)),
      typeNames(nodes_with_parent(std::move(type_names), this)) {}

TypeDefinition::~TypeDefinition() {
  delete name;
  for (const auto p : typeNames) {
    delete p;
  }
}

void TypeDefinition::print(std::ostream &os) const {
  os << *name;
  size_t i = 0;
  for (const auto p : typeNames) {
    os << " " << *p;
    if (i++ < typeNames.size() - 1) {
      os << " ";
    }
  }
}

// FieldType implementation
string FieldType::to_string() const {
  if (is_tuple_type) {
    string result = "(";
    for (size_t i = 0; i < tuple_types.size(); i++) {
      if (i > 0)
        result += ", ";
      result += tuple_types[i].to_string();
    }
    return result + ")";
  }
  if (!is_function_type) {
    string result = name;
    for (const auto &argument : type_arguments)
      result += " " + argument.to_string();
    return result;
  }
  string result = "(";
  for (size_t i = 0; i < param_types.size(); i++) {
    if (i > 0)
      result += " -> ";
    result += param_types[i].to_string();
  }
  if (!param_types.empty() && !return_types.empty())
    result += " -> ";
  if (!return_types.empty())
    result += return_types[0].to_string();
  result += ")";
  return result;
}

// AdtConstructor implementation
AdtConstructor::AdtConstructor(SourceRange token, string name,
                               vector<FieldType> field_types,
                               vector<string> field_names)
    : AstNode(token), name(std::move(name)),
      field_type_names(std::move(field_types)),
      field_names(std::move(field_names)) {}

AdtConstructor::~AdtConstructor() = default;

void AdtConstructor::print(std::ostream &os) const {
  os << name;
  for (const auto &ft : field_type_names) {
    os << " " << ft.to_string();
  }
}

// AdtDeclNode implementation
AdtDeclNode::AdtDeclNode(SourceRange token, string name,
                         vector<string> type_params,
                         vector<AdtConstructor *> variants,
                         vector<string> derive_traits)
    : AstNode(token), name(std::move(name)),
      type_params(std::move(type_params)),
      variants(nodes_with_parent(std::move(variants), this)),
      derive_traits(std::move(derive_traits)) {}

AdtDeclNode::~AdtDeclNode() {
  for (const auto p : variants)
    delete p;
}

void AdtDeclNode::print(std::ostream &os) const {
  os << "type " << name;
  for (const auto &tp : type_params) {
    os << " " << tp;
  }
  os << " = ";
  for (size_t i = 0; i < variants.size(); i++) {
    os << *variants[i];
    if (i < variants.size() - 1) {
      os << " | ";
    }
  }
}

// ConstructorPattern implementation
ConstructorPattern::ConstructorPattern(SourceRange token, string name,
                                       vector<PatternNode *> sub_patterns)
    : PatternNode(token), constructor_name(std::move(name)),
      sub_patterns(nodes_with_parent(std::move(sub_patterns), this)) {}

ConstructorPattern::~ConstructorPattern() {
  for (const auto p : sub_patterns)
    delete p;
}

void ConstructorPattern::print(std::ostream &os) const {
  os << constructor_name;
  for (const auto p : sub_patterns) {
    os << " " << *p;
  }
}

TypedPattern::TypedPattern(SourceRange token, string name, string type_name)
    : PatternNode(token), binding_name(std::move(name)),
      type_name(std::move(type_name)) {}

RecordLiteralExpr::RecordLiteralExpr(SourceRange token,
                                     vector<pair<string, ExprNode *>> fields)
    : ExprNode(token), fields(std::move(fields)) {}

RecordLiteralExpr::~RecordLiteralExpr() {
  for (auto &[_, expr] : fields)
    delete expr;
}

void RecordLiteralExpr::print(std::ostream &os) const {
  os << "{ ";
  for (size_t i = 0; i < fields.size(); i++) {
    if (i > 0)
      os << ", ";
    os << fields[i].first << " = " << *fields[i].second;
  }
  os << " }";
}

void TypedPattern::print(std::ostream &os) const {
  os << "(" << binding_name << " : " << type_name << ")";
}

TypeNode::TypeNode(SourceRange token, TypeDeclaration *declaration,
                   vector<TypeDeclaration *> definitions)
    : AstNode(token),
      declaration(declaration->with_parent<TypeDeclaration>(this)),
      definitions(nodes_with_parent(std::move(definitions), this)) {}

TypeNode::~TypeNode() {
  delete declaration;
  for (const auto p : definitions) {
    delete p;
  }
}

void TypeNode::print(std::ostream &os) const {
  os << "type " << *declaration << " = ";
  size_t i = 0;
  for (const auto p : definitions) {
    os << *p;
    if (i++ < definitions.size() - 1) {
      os << "| " << endl;
    }
  }
}

TypeInstance::TypeInstance(SourceRange token, TypeNameNode *name,
                           vector<ExprNode *> exprs)
    : AstNode(token), name(name->with_parent<TypeNameNode>(this)),
      exprs(nodes_with_parent(std::move(exprs), this)) {}

TypeInstance::~TypeInstance() {
  delete name;
  for (const auto p : exprs) {
    delete p;
  }
}

void TypeInstance::print(std::ostream &os) const {
  os << *name << "(";
  size_t i = 0;
  for (const auto p : exprs) {
    os << *p;
    if (i++ < exprs.size() - 1) {
      os << ", ";
    }
  }
  os << ')';
}

FunctionDeclaration::FunctionDeclaration(
    SourceRange token, NameExpr *function_name,
    vector<TypeDefinition *> type_definitions)
    : AstNode(token), functionName(function_name->with_parent<NameExpr>(this)),
      typeDefinitions(nodes_with_parent(std::move(type_definitions), this)) {}

FunctionDeclaration::~FunctionDeclaration() {
  delete functionName;
  for (const auto p : typeDefinitions) {
    delete p;
  }
}

void FunctionDeclaration::print(std::ostream &os) const {
  os << *functionName << " :: ";
  size_t i = 0;
  for (const auto p : typeDefinitions) {
    os << *p;
    if (i++ < typeDefinitions.size() - 1) {
      os << " -> ";
    }
  }
}

// TraitDeclNode implementation
TraitDeclNode::TraitDeclNode(SourceRange token, string name,
                             vector<string> type_params,
                             vector<TraitMethodSig> methods,
                             vector<pair<string, string>> superclasses)
    : AstNode(token), name(std::move(name)),
      type_params(std::move(type_params)), methods(std::move(methods)),
      superclasses(std::move(superclasses)) {}

TraitDeclNode::~TraitDeclNode() {
  for (auto &m : methods) {
    delete m.default_impl;
  }
}

void TraitDeclNode::print(std::ostream &os) const {
  if (!superclasses.empty()) {
    for (size_t i = 0; i < superclasses.size(); i++) {
      if (i > 0)
        os << ", ";
      os << superclasses[i].first << " " << superclasses[i].second;
    }
    os << " => ";
  }
  os << "trait " << name;
  for (const auto &type_param : type_params)
    os << " " << type_param;
  os << endl;
  for (const auto &m : methods) {
    os << "    " << m.name << " : ...";
    if (m.default_impl)
      os << " (default)";
    os << endl;
  }
  os << "end";
}

// InstanceDeclNode implementation
InstanceDeclNode::InstanceDeclNode(SourceRange token, string trait_name,
                                   vector<string> type_names,
                                   vector<FunctionExpr *> methods,
                                   vector<pair<string, string>> constraints,
                                   vector<string> type_params)
    : AstNode(token), trait_name(std::move(trait_name)),
      type_names(std::move(type_names)),
      methods(nodes_with_parent(std::move(methods), this)),
      constraints(std::move(constraints)), type_params(std::move(type_params)) {
}

InstanceDeclNode::~InstanceDeclNode() {
  for (const auto p : methods)
    delete p;
}

void InstanceDeclNode::print(std::ostream &os) const {
  os << "instance ";
  if (!constraints.empty()) {
    for (size_t i = 0; i < constraints.size(); i++) {
      if (i > 0)
        os << ", ";
      os << constraints[i].first << " " << constraints[i].second;
    }
    os << " => ";
  }
  os << trait_name;
  if (!type_names.empty()) {
    os << " ";
    if (!type_params.empty()) {
      os << "(" << type_names.front();
      for (auto &tp : type_params)
        os << " " << tp;
      os << ")";
    } else {
      os << type_names.front();
    }
    for (size_t i = 1; i < type_names.size(); ++i)
      os << " " << type_names[i];
  }
  os << endl;
  for (const auto *m : methods) {
    os << "    " << *m << endl;
  }
  os << "end";
}

// ===== Algebraic Effects =====

EffectDeclNode::EffectDeclNode(SourceRange token, string name,
                               string type_param,
                               vector<EffectOpSig> operations)
    : AstNode(token), name(std::move(name)), type_param(std::move(type_param)),
      operations(std::move(operations)) {}
EffectDeclNode::~EffectDeclNode() = default;
void EffectDeclNode::print(std::ostream &os) const { os << "effect " << name; }

PerformExpr::PerformExpr(SourceRange token, string effect_name,
                         string operation_name, vector<ExprNode *> args)
    : ExprNode(token), effect_name(std::move(effect_name)),
      operation_name(std::move(operation_name)), args(std::move(args)) {}
PerformExpr::~PerformExpr() {
  for (auto *a : args)
    delete a;
}
void PerformExpr::print(std::ostream &os) const {
  os << "perform " << effect_name << "." << operation_name;
}

HandlerClause::HandlerClause(SourceRange token, string effect_name,
                             string op_name, vector<string> arg_names,
                             string resume_name, ExprNode *body)
    : AstNode(token), effect_name(std::move(effect_name)),
      operation_name(std::move(op_name)), arg_names(std::move(arg_names)),
      resume_name(std::move(resume_name)), is_return_clause(false),
      body(body ? body->with_parent<ExprNode>(this) : nullptr) {}
HandlerClause::HandlerClause(SourceRange token, string return_binding,
                             ExprNode *body)
    : AstNode(token), is_return_clause(true),
      return_binding(std::move(return_binding)),
      body(body ? body->with_parent<ExprNode>(this) : nullptr) {}
HandlerClause::~HandlerClause() { delete body; }
void HandlerClause::print(std::ostream &os) const {
  if (is_return_clause)
    os << "return " << return_binding;
  else
    os << effect_name << "." << operation_name;
}

HandleExpr::HandleExpr(SourceRange token, ExprNode *body,
                       vector<HandlerClause *> clauses)
    : ExprNode(token), body(body ? body->with_parent<ExprNode>(this) : nullptr),
      clauses(std::move(clauses)) {}
HandleExpr::~HandleExpr() {
  delete body;
  for (auto *c : clauses)
    delete c;
}
void HandleExpr::print(std::ostream &os) const { os << "handle ... end"; }

std::ostream &operator<<(std::ostream &os, const AstNode &obj) {
  obj.print(os);
  return os;
}

// Explicit template instantiations for LiteralExpr
template class LiteralExpr<bool>;
template class LiteralExpr<double>;
template class LiteralExpr<int>;
template class LiteralExpr<unsigned char>;
template class LiteralExpr<string>;
template class LiteralExpr<wchar_t>;
template class LiteralExpr<nullptr_t>;

} // namespace yona::ast
