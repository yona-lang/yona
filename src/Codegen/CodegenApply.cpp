//
// Codegen — Function application (call sites)
//
// Apply-chain flattening, argument evaluation, partial/curry apply,
// direct/extern/higher-order call emission, auto-await, and the
// codegen_apply entry point itself.
//
// Split out from CodegenFunction.cpp which remains responsible for
// function *definition*, free-var analysis, and compile_function.
//

#include "yona/Codegen/AcceleratorLowering.h"
#include "yona/Codegen/Codegen.h"
#include "yona/Semantics/BorrowEscapeAnalysis.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Type.h>

#include <iostream>

namespace yona::compiler::codegen {

using yona::compiler::AccelKernel;
using yona::compiler::AccelMatch;
using yona::compiler::ErrorCode;
using yona::compiler::is_unlowerable_column_apply;
using yona::compiler::match_transparent_apply;

// Closure layout constants — must match CodegenFunction.cpp /
// Runtime archive entry points.
static constexpr int CLOSURE_FIELD_FN = 0;    // fn_ptr
static constexpr int CLOSURE_FIELD_ARITY = 2; // arity
static constexpr int CLOSURE_FIELD_BORROW_MASK = 5;
static constexpr int CLOSURE_HDR_SIZE = 6;

static int64_t borrow_mask(const std::vector<bool> &borrowed,
                           size_t offset = 0) {
  int64_t mask = 0;
  for (size_t i = offset; i < borrowed.size() && i - offset < 64; ++i)
    if (borrowed[i])
      mask |= int64_t{1} << (i - offset);
  return mask;
}

using llvm::Argument;
using llvm::ArrayType;
using llvm::BasicBlock;
using llvm::Constant;
using llvm::ConstantExpr;
using llvm::ConstantFP;
using llvm::ConstantInt;
using llvm::Function;
using llvm::FunctionType;
using llvm::GlobalValue;
using llvm::GlobalVariable;
using llvm::IRBuilder;
using llvm::PointerType;
using llvm::StructType;
using llvm::Value;
using LType = llvm::Type;

static CType yona_type_to_ctype(const types::Type &t);

struct TypeDescriptorNode {
  std::string name;
  std::vector<TypeDescriptorNode> arguments;
};

static std::optional<TypeDescriptorNode>
parse_type_descriptor(std::string_view text, size_t &offset) {
  while (offset < text.size() && text[offset] == ' ')
    ++offset;
  const size_t begin = offset;
  while (offset < text.size() && text[offset] != '(' && text[offset] != ',' &&
         text[offset] != ')')
    ++offset;
  if (begin == offset)
    return std::nullopt;
  TypeDescriptorNode node{std::string(text.substr(begin, offset - begin)), {}};
  while (!node.name.empty() && node.name.back() == ' ')
    node.name.pop_back();
  if (offset >= text.size() || text[offset] != '(')
    return node;
  ++offset;
  while (offset < text.size() && text[offset] != ')') {
    auto argument = parse_type_descriptor(text, offset);
    if (!argument)
      return std::nullopt;
    node.arguments.push_back(std::move(*argument));
    if (offset < text.size() && text[offset] == ',')
      ++offset;
    else if (offset < text.size() && text[offset] != ')')
      return std::nullopt;
  }
  if (offset >= text.size() || text[offset] != ')')
    return std::nullopt;
  ++offset;
  return node;
}

static std::optional<TypeDescriptorNode>
parse_type_descriptor(const std::string &text) {
  size_t offset = 0;
  auto node = parse_type_descriptor(text, offset);
  while (offset < text.size() && text[offset] == ' ')
    ++offset;
  return node && offset == text.size() ? node : std::nullopt;
}

static SemanticTypeIdentity semantic_identity_of(const TypedValue &value);

static void bind_descriptor_variables(
    const TypeDescriptorNode &formal, const SemanticTypeIdentity &actual,
    std::unordered_map<std::string, SemanticTypeIdentity> &substitutions) {
  if (formal.name == "VAR" && formal.arguments.size() == 1) {
    substitutions.insert_or_assign(formal.arguments.front().name, actual);
    return;
  }
  size_t formal_offset = formal.name == "ADT" ? 1 : 0;
  for (size_t i = formal_offset; i < formal.arguments.size() &&
                                 i - formal_offset < actual.arguments.size();
       ++i)
    bind_descriptor_variables(formal.arguments[i],
                              actual.arguments[i - formal_offset],
                              substitutions);
}

static std::optional<SemanticTypeIdentity> identity_from_descriptor(
    const TypeDescriptorNode &descriptor,
    const std::unordered_map<std::string, SemanticTypeIdentity>
        &substitutions) {
  if (descriptor.name == "VAR" && descriptor.arguments.size() == 1) {
    const auto found = substitutions.find(descriptor.arguments.front().name);
    return found == substitutions.end()
               ? std::nullopt
               : std::optional<SemanticTypeIdentity>(found->second);
  }
  SemanticTypeIdentity result;
  if (descriptor.name == "INT")
    result.type = CType::INT;
  else if (descriptor.name == "FLOAT")
    result.type = CType::FLOAT;
  else if (descriptor.name == "BOOL")
    result.type = CType::BOOL;
  else if (descriptor.name == "STRING")
    result.type = CType::STRING;
  else if (descriptor.name == "SYMBOL")
    result.type = CType::SYMBOL;
  else if (descriptor.name == "UNIT")
    result.type = CType::UNIT;
  else if (descriptor.name == "BYTE_ARRAY")
    result.type = CType::BYTE_ARRAY;
  else if (descriptor.name == "INT_ARRAY")
    result.type = CType::INT_ARRAY;
  else if (descriptor.name == "FLOAT_ARRAY")
    result.type = CType::FLOAT_ARRAY;
  else if (descriptor.name == "Seq")
    result.type = CType::SEQ;
  else if (descriptor.name == "Set")
    result.type = CType::SET;
  else if (descriptor.name == "Dict")
    result.type = CType::DICT;
  else if (descriptor.name == "TUPLE")
    result.type = CType::TUPLE;
  else if (descriptor.name == "FUNCTION")
    result.type = CType::FUNCTION;
  else if (descriptor.name == "Promise")
    result.type = CType::PROMISE;
  else if (descriptor.name == "ADT" && !descriptor.arguments.empty()) {
    result.type = CType::ADT;
    result.adt_name = descriptor.arguments.front().name;
  } else {
    return std::nullopt;
  }
  const size_t argument_offset = descriptor.name == "ADT" ? 1 : 0;
  for (size_t i = argument_offset; i < descriptor.arguments.size(); ++i) {
    auto argument =
        identity_from_descriptor(descriptor.arguments[i], substitutions);
    if (!argument)
      return std::nullopt;
    result.arguments.push_back(std::move(*argument));
  }
  return result;
}

static std::optional<SemanticTypeIdentity> resolve_descriptor_identity(
    const std::string &result_descriptor,
    const std::vector<std::string> &parameter_descriptors,
    const std::vector<TypedValue> &args) {
  const auto result = parse_type_descriptor(result_descriptor);
  if (!result)
    return std::nullopt;
  std::unordered_map<std::string, SemanticTypeIdentity> substitutions;
  for (size_t pi = 0; pi < parameter_descriptors.size() && pi < args.size();
       ++pi) {
    const auto parameter = parse_type_descriptor(parameter_descriptors[pi]);
    if (parameter)
      bind_descriptor_variables(*parameter, semantic_identity_of(args[pi]),
                                substitutions);
  }
  return identity_from_descriptor(*result, substitutions);
}

static void flatten_function_descriptor(
    const TypeDescriptorNode &descriptor,
    std::vector<const TypeDescriptorNode *> &parameters) {
  if (descriptor.name != "FUNCTION" || descriptor.arguments.size() != 2)
    return;
  parameters.push_back(&descriptor.arguments[0]);
  flatten_function_descriptor(descriptor.arguments[1], parameters);
}

static std::string semantic_head_name(const TypedValue &value) {
  if (value.type == CType::ADT && !value.adt_type_name.empty())
    return value.adt_type_name;
  switch (value.type) {
  case CType::INT:
    return "Int";
  case CType::FLOAT:
    return "Float";
  case CType::BOOL:
    return "Bool";
  case CType::STRING:
    return "String";
  case CType::SYMBOL:
    return "Symbol";
  case CType::UNIT:
    return "Unit";
  case CType::SEQ:
    return "Seq";
  case CType::SET:
    return "Set";
  case CType::DICT:
    return "Dict";
  case CType::TUPLE:
    return "Tuple";
  case CType::FUNCTION:
    return "Function";
  case CType::PROMISE:
    return "Promise";
  case CType::BYTE_ARRAY:
    return "ByteArray";
  case CType::INT_ARRAY:
    return "IntArray";
  case CType::FLOAT_ARRAY:
    return "FloatArray";
  case CType::CHANNEL:
    return "Channel";
  default:
    return value.adt_type_name;
  }
}

static bool descriptor_parameter_matches(const TypeDescriptorNode &formal,
                                         const TypedValue &actual,
                                         const auto &trait,
                                         const auto &instance) {
  if (formal.name == "VAR" && formal.arguments.size() == 1) {
    const auto parameter =
        std::find(trait.type_params.begin(), trait.type_params.end(),
                  formal.arguments.front().name);
    if (parameter == trait.type_params.end())
      return true;
    const size_t index = static_cast<size_t>(
        std::distance(trait.type_params.begin(), parameter));
    if (index >= instance.type_names.size())
      return false;
    const auto &head = instance.type_names[index];
    return !head.empty() &&
           (std::islower(static_cast<unsigned char>(head.front())) ||
            head == semantic_head_name(actual));
  }
  const auto expected = identity_from_descriptor(formal, {});
  if (!expected || expected->type != actual.type)
    return false;
  return expected->adt_name.empty() ||
         expected->adt_name == actual.adt_type_name;
}

Value *coerce_to_type(IRBuilder<> &builder, Value *v, LType *expected) {
  if (!v || v->getType() == expected)
    return v;
  if (v->getType()->isIntegerTy() && expected->isPointerTy())
    return builder.CreateIntToPtr(v, expected);
  if (v->getType()->isPointerTy() && expected->isIntegerTy())
    return builder.CreatePtrToInt(v, expected);
  if (v->getType()->isIntegerTy() && expected->isIntegerTy())
    return builder.CreateZExtOrTrunc(v, expected);
  if (v->getType()->isDoubleTy() && expected->isIntegerTy(64))
    return builder.CreateBitCast(v, expected);
  if (v->getType()->isIntegerTy(64) && expected->isDoubleTy())
    return builder.CreateBitCast(v, expected);
  return v;
}

TypedValue Codegen::emit_accelerator_kernel(const AccelMatch &match) {
  if (match.site)
    set_debug_loc(match.site->Range);

  std::vector<TypedValue> args;
  CType ret = CType::INT;
  switch (match.kernel) {
  case AccelKernel::IntMapSquare: {
    TypedValue arr_tv = codegen(match.array);
    if (!arr_tv.val)
      return {};
    if (arr_tv.type == CType::PROMISE)
      arr_tv = auto_await(arr_tv);
    args.push_back(arr_tv);
    ret = CType::INT_ARRAY;
    break;
  }
  case AccelKernel::IntMapAdd:
  case AccelKernel::IntMapMul:
  case AccelKernel::IntFilterGt:
  case AccelKernel::IntFilterLt: {
    TypedValue scalar_tv;
    if (match.scalar_is_literal)
      scalar_tv = {
          ConstantInt::get(LType::getInt64Ty(*context_), match.lit_i64),
          CType::INT};
    else {
      scalar_tv = codegen(match.scalar);
      if (!scalar_tv.val)
        return {};
      if (scalar_tv.type == CType::PROMISE)
        scalar_tv = auto_await(scalar_tv);
      if (match.negate_scalar && match.kernel == AccelKernel::IntMapAdd)
        scalar_tv.val = builder_->CreateNeg(scalar_tv.val, "accel.neg");
    }
    TypedValue arr_tv = codegen(match.array);
    if (!arr_tv.val)
      return {};
    if (arr_tv.type == CType::PROMISE)
      arr_tv = auto_await(arr_tv);
    args.push_back(scalar_tv);
    args.push_back(arr_tv);
    ret = CType::INT_ARRAY;
    break;
  }
  case AccelKernel::IntReduceSum: {
    TypedValue arr_tv = codegen(match.array);
    if (!arr_tv.val)
      return {};
    if (arr_tv.type == CType::PROMISE)
      arr_tv = auto_await(arr_tv);
    args.push_back(arr_tv);
    ret = CType::INT;
    break;
  }
  case AccelKernel::FloatScale: {
    TypedValue scalar_tv;
    if (match.scalar_is_literal)
      scalar_tv = {
          ConstantFP::get(LType::getDoubleTy(*context_), match.lit_f64),
          CType::FLOAT};
    else {
      scalar_tv = codegen(match.scalar);
      if (!scalar_tv.val)
        return {};
      if (scalar_tv.type == CType::PROMISE)
        scalar_tv = auto_await(scalar_tv);
      if (scalar_tv.type == CType::INT) {
        scalar_tv.val = builder_->CreateSIToFP(scalar_tv.val,
                                               LType::getDoubleTy(*context_));
        scalar_tv.type = CType::FLOAT;
      }
    }
    TypedValue arr_tv = codegen(match.array);
    if (!arr_tv.val)
      return {};
    if (arr_tv.type == CType::PROMISE)
      arr_tv = auto_await(arr_tv);
    args.push_back(scalar_tv);
    args.push_back(arr_tv);
    ret = CType::FLOAT_ARRAY;
    break;
  }
  case AccelKernel::FloatReduceSum: {
    TypedValue arr_tv = codegen(match.array);
    if (!arr_tv.val)
      return {};
    if (arr_tv.type == CType::PROMISE)
      arr_tv = auto_await(arr_tv);
    args.push_back(arr_tv);
    ret = CType::FLOAT;
    break;
  }
  case AccelKernel::None:
    return {};
  }

  std::vector<LType *> arg_tys;
  std::vector<Value *> vals;
  for (auto &a : args) {
    LType *expected = llvm_type(a.type);
    arg_tys.push_back(expected);
    vals.push_back(coerce_to_type(*builder_, a.val, expected));
  }
  auto *ft = llvm::FunctionType::get(llvm_type(ret), arg_tys, false);
  auto *fn = module_->getFunction(match.abi_symbol);
  if (!fn)
    fn = Function::Create(ft, Function::ExternalLinkage, match.abi_symbol,
                          module_);
  Value *call = builder_->CreateCall(fn, vals, "accel_kernel");
  return {call, ret};
}

// ===== codegen_apply helpers =====

Codegen::ApplyChain Codegen::flatten_apply_chain(ApplyExpr *node) {
  ApplyChain result;
  ApplyExpr *cur = node;
  while (cur) {
    result.chain.push_back(cur);
    if (auto *nc = dynamic_cast<NameCall *>(cur->call)) {
      result.fn_name = nc->name->value;
      break;
    } else if (auto *mc = dynamic_cast<ModuleCall *>(cur->call)) {
      // FQN call: Std\List::map — auto-load module interface
      result.fn_name = mc->funName->value;
      if (auto *fqn = std::get_if<FqnExpr *>(&mc->fqn)) {
        auto [fqn_str, fqn_path] = build_fqn_path(*fqn);
        result.module_fqn = fqn_str;
        load_module_interface(fqn_path);
        register_import(fqn_str, result.fn_name, result.fn_name);
      }
      break;
    } else if (auto *ec = dynamic_cast<ExprCall *>(cur->call)) {
      if (auto *inner = dynamic_cast<ApplyExpr *>(ec->expr))
        cur = inner;
      else
        break;
    } else
      break;
  }
  return result;
}

Codegen::EvaluatedArgs
Codegen::evaluate_apply_args(const std::vector<ApplyExpr *> &chain) {
  EvaluatedArgs result;
  for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
    for (auto &a : (*it)->args) {
      last_lambda_name_.clear();
      TypedValue tv;
      if (std::holds_alternative<ExprNode *>(a))
        tv = codegen(std::get<ExprNode *>(a));
      else
        tv = codegen(std::get<ValueExpr *>(a));
      // FUNCTION args may have nullptr val (deferred compilation) — that's OK
      if (tv.type != CType::FUNCTION && !tv) {
        result.all_args.clear();
        return result;
      }
      // Auto-await PROMISE args (functions expect concrete values)
      if (tv.type == CType::PROMISE)
        tv = auto_await(tv);
      result.all_args.push_back(tv);
      result.arg_lambda_names.push_back(last_lambda_name_);
    }
  }
  return result;
}

static bool is_type_variable(const types::Type &type, std::string &name) {
  auto named = std::get_if<std::shared_ptr<types::NamedType>>(&type);
  if (!named || !*named || (*named)->name.empty() ||
      !std::islower(static_cast<unsigned char>((*named)->name.front())))
    return false;
  name = (*named)->name;
  return true;
}

static bool contains_type_variable(const types::Type &type) {
  std::string variable;
  if (is_type_variable(type, variable))
    return true;
  if (const auto *collection =
          std::get_if<std::shared_ptr<types::SingleItemCollectionType>>(&type);
      collection && *collection)
    return contains_type_variable((*collection)->valueType);
  if (const auto *dict =
          std::get_if<std::shared_ptr<types::DictCollectionType>>(&type);
      dict && *dict)
    return contains_type_variable((*dict)->keyType) ||
           contains_type_variable((*dict)->valueType);
  if (const auto *function =
          std::get_if<std::shared_ptr<types::FunctionType>>(&type);
      function && *function)
    return contains_type_variable((*function)->argumentType) ||
           contains_type_variable((*function)->returnType);
  if (const auto *product =
          std::get_if<std::shared_ptr<types::ProductType>>(&type);
      product && *product)
    return std::any_of(
        (*product)->types.begin(), (*product)->types.end(),
        [](const auto &element) { return contains_type_variable(element); });
  if (const auto *named = std::get_if<std::shared_ptr<types::NamedType>>(&type);
      named && *named &&
      !std::holds_alternative<std::nullptr_t>((*named)->type))
    return contains_type_variable((*named)->type);
  return false;
}

static std::vector<const types::Type *>
function_arguments(const types::Type &signature) {
  std::vector<const types::Type *> result;
  auto *current = &signature;
  while (
      std::holds_alternative<std::shared_ptr<types::FunctionType>>(*current)) {
    const auto &function =
        std::get<std::shared_ptr<types::FunctionType>>(*current);
    result.push_back(&function->argumentType);
    current = &function->returnType;
  }
  return result;
}

struct LambdaArgumentHint {
  CType type = CType::INT;
  std::string adt_type_name;
  std::vector<SemanticTypeIdentity> semantic_arguments;
  bool known = false;
};

static SemanticTypeIdentity semantic_identity_of(const TypedValue &value) {
  SemanticTypeIdentity identity;
  identity.type = value.type;
  identity.adt_name = value.adt_type_name;
  identity.arguments = !value.semantic_subtypes.empty()
                           ? value.semantic_subtypes
                           : value.adt_semantic_arguments;
  if (identity.arguments.empty())
    for (const auto subtype : value.subtypes)
      identity.arguments.push_back({subtype, {}, {}});
  return identity;
}

static void
append_specialization_identity(std::string &name,
                               const SemanticTypeIdentity &identity) {
  name += "_" + std::to_string(static_cast<int>(identity.type));
  if (!identity.adt_name.empty())
    name += "_" + identity.adt_name;
  for (const auto &argument : identity.arguments) {
    name += "_of";
    append_specialization_identity(name, argument);
  }
}

static LambdaArgumentHint
hint_from_identity(const SemanticTypeIdentity &identity) {
  return {identity.type, identity.adt_name, identity.arguments, true};
}

static LambdaArgumentHint
checked_lambda_argument_hint(typechecker::TypeChecker &checker,
                             typechecker::MonoTypePtr type) {
  type = checker.zonk(type);
  if (!type)
    return {};
  if (type->tag == typechecker::MonoType::App) {
    const auto &name = type->type_name;
    // An unresolved source type variable is represented as an App by the
    // checker. It is not a nominal heap ADT: marking it as ADT turns the
    // universal i64 carrier into a pointer and produces invalid IR in
    // callbacks such as `map (\x -> x + 1) None`.
    if (!name.empty() && std::islower(static_cast<unsigned char>(name.front())))
      return {};
    if (name == "Seq")
      return {CType::SEQ, {}, {}, true};
    if (name == "Set")
      return {CType::SET, {}, {}, true};
    if (name == "Dict")
      return {CType::DICT, {}, {}, true};
    if (name == "ByteArray")
      return {CType::BYTE_ARRAY, {}, {}, true};
    if (name == "IntArray")
      return {CType::INT_ARRAY, {}, {}, true};
    if (name == "FloatArray")
      return {CType::FLOAT_ARRAY, {}, {}, true};
    if (name == "Promise")
      return {CType::PROMISE, {}, {}, true};
    return {CType::ADT, name, {}, true};
  }
  if (type->tag == typechecker::MonoType::Arrow)
    return {CType::FUNCTION, {}, {}, true};
  if (type->tag == typechecker::MonoType::MTuple)
    return {CType::TUPLE, {}, {}, true};
  if (type->tag != typechecker::MonoType::Con)
    return {};
  using typechecker::TyCon;
  switch (type->con) {
  case TyCon::Int:
  case TyCon::Char:
  case TyCon::Byte:
    return {CType::INT, {}, {}, true};
  case TyCon::Float:
    return {CType::FLOAT, {}, {}, true};
  case TyCon::Bool:
    return {CType::BOOL, {}, {}, true};
  case TyCon::String:
    return {CType::STRING, {}, {}, true};
  case TyCon::Symbol:
    return {CType::SYMBOL, {}, {}, true};
  case TyCon::Unit:
    return {CType::UNIT, {}, {}, true};
  case TyCon::Seq:
    return {CType::SEQ, {}, {}, true};
  case TyCon::Set:
    return {CType::SET, {}, {}, true};
  case TyCon::Dict:
    return {CType::DICT, {}, {}, true};
  case TyCon::Tuple:
    return {CType::TUPLE, {}, {}, true};
  case TyCon::Function:
    return {CType::FUNCTION, {}, {}, true};
  case TyCon::Promise:
    return {CType::PROMISE, {}, {}, true};
  case TyCon::ByteArray:
    return {CType::BYTE_ARRAY, {}, {}, true};
  }
  return {};
}

static typechecker::MonoTypePtr
find_channel_payload_type(typechecker::TypeChecker &checker,
                          typechecker::MonoTypePtr type) {
  type = checker.zonk(type);
  if (!type)
    return nullptr;
  if (type->tag == typechecker::MonoType::App) {
    if ((type->type_name == "Sender" || type->type_name == "Receiver") &&
        !type->args.empty())
      return checker.zonk(type->args.front());
    for (auto *argument : type->args)
      if (auto *payload = find_channel_payload_type(checker, argument))
        return payload;
  } else if (type->tag == typechecker::MonoType::MTuple) {
    for (auto *element : type->elements)
      if (auto *payload = find_channel_payload_type(checker, element))
        return payload;
  }
  return nullptr;
}

static LambdaArgumentHint lambda_argument_hint(const types::Type &type) {
  std::string variable;
  if (is_type_variable(type, variable))
    return {};
  LambdaArgumentHint hint{yona_type_to_ctype(type), {}, {}, true};
  if (const auto *named = std::get_if<std::shared_ptr<types::NamedType>>(&type);
      named && *named && hint.type == CType::ADT) {
    hint.adt_type_name = (*named)->name;
  }
  return hint;
}

static std::vector<std::vector<LambdaArgumentHint>>
infer_lambda_argument_hints(const types::Type &signature,
                            const std::vector<TypedValue> &call_args) {
  const auto formal_args = function_arguments(signature);
  std::unordered_map<std::string, LambdaArgumentHint> substitutions;
  std::function<void(const types::Type &, const SemanticTypeIdentity &)> bind;
  bind = [&](const types::Type &formal, const SemanticTypeIdentity &actual) {
    std::string variable;
    if (is_type_variable(formal, variable)) {
      substitutions.insert_or_assign(variable, hint_from_identity(actual));
      return;
    }
    if (const auto *collection =
            std::get_if<std::shared_ptr<types::SingleItemCollectionType>>(
                &formal);
        collection && *collection && !actual.arguments.empty()) {
      bind((*collection)->valueType, actual.arguments.front());
      return;
    }
    if (const auto *dict =
            std::get_if<std::shared_ptr<types::DictCollectionType>>(&formal);
        dict && *dict && actual.arguments.size() >= 2) {
      bind((*dict)->keyType, actual.arguments[0]);
      bind((*dict)->valueType, actual.arguments[1]);
      return;
    }
    if (const auto *product =
            std::get_if<std::shared_ptr<types::ProductType>>(&formal);
        product && *product) {
      for (size_t i = 0;
           i < (*product)->types.size() && i < actual.arguments.size(); ++i)
        bind((*product)->types[i], actual.arguments[i]);
      return;
    }
    if (const auto *named =
            std::get_if<std::shared_ptr<types::NamedType>>(&formal);
        named && *named &&
        !std::holds_alternative<std::nullptr_t>((*named)->type) &&
        !actual.arguments.empty()) {
      if (const auto *product =
              std::get_if<std::shared_ptr<types::ProductType>>(&(*named)->type);
          product && *product) {
        for (size_t i = 0;
             i < (*product)->types.size() && i < actual.arguments.size(); ++i)
          bind((*product)->types[i], actual.arguments[i]);
      } else {
        bind((*named)->type, actual.arguments.front());
      }
    }
  };
  for (size_t i = 0; i < formal_args.size() && i < call_args.size(); ++i) {
    bind(*formal_args[i], semantic_identity_of(call_args[i]));
  }

  std::vector<std::vector<LambdaArgumentHint>> hints(call_args.size());
  for (size_t i = 0; i < formal_args.size() && i < call_args.size(); ++i) {
    if (call_args[i].type != CType::FUNCTION ||
        !std::holds_alternative<std::shared_ptr<types::FunctionType>>(
            *formal_args[i]))
      continue;
    for (const auto *parameter : function_arguments(*formal_args[i])) {
      std::string variable;
      auto substitution = is_type_variable(*parameter, variable)
                              ? substitutions.find(variable)
                              : substitutions.end();
      hints[i].push_back(substitution != substitutions.end()
                             ? substitution->second
                             : lambda_argument_hint(*parameter));
    }
  }
  return hints;
}

void Codegen::precompile_function_args(EvaluatedArgs &args,
                                       const std::string &callee_name) {
  std::optional<types::Type> signature;
  if (auto deferred = deferred_functions_.find(callee_name);
      deferred != deferred_functions_.end() &&
      deferred->second.ast->type_signature) {
    signature = *deferred->second.ast->type_signature;
  } else if (auto ext = imports_.extern_functions.find(callee_name);
             ext != imports_.extern_functions.end()) {
    if (auto source = imports_.imported_sources.find(ext->second);
        source != imports_.imported_sources.end()) {
      auto parsed =
          reparse_genfn(source->second.local_name, source->second.source_text);
      if (parsed && !parsed->functions.empty() &&
          parsed->functions[0]->type_signature)
        signature = *parsed->functions[0]->type_signature;
    }
  }
  // Infer source-level variables from every concrete argument, then apply
  // them to arguments whose literal representation carries no element
  // identity (most importantly `[]`). This makes a call such as
  // `collect (Iterator (Int, String)) []` specialize both parameters to
  // the same tuple element type; otherwise recursive helpers compile the
  // empty accumulator as `Seq Int` and omit required heap retains.
  if (signature) {
    const auto formal_args = function_arguments(*signature);
    std::unordered_map<std::string, SemanticTypeIdentity> substitutions;
    for (size_t i = 0; i < formal_args.size() && i < args.all_args.size();
         ++i) {
      const auto formal =
          parse_type_descriptor(source_type_descriptor(*formal_args[i]));
      if (formal)
        bind_descriptor_variables(
            *formal, semantic_identity_of(args.all_args[i]), substitutions);
    }
    for (size_t i = 0; i < formal_args.size() && i < args.all_args.size();
         ++i) {
      auto &actual = args.all_args[i];
      if (actual.type != CType::SEQ && actual.type != CType::SET &&
          actual.type != CType::DICT)
        continue;
      if (!actual.semantic_subtypes.empty())
        continue;
      const auto formal =
          parse_type_descriptor(source_type_descriptor(*formal_args[i]));
      if (!formal)
        continue;
      const auto inferred = identity_from_descriptor(*formal, substitutions);
      if (!inferred || inferred->type != actual.type ||
          inferred->arguments.empty())
        continue;
      actual.semantic_subtypes = inferred->arguments;
      actual.subtypes.clear();
      for (const auto &argument : inferred->arguments)
        actual.subtypes.push_back(argument.type);
    }
  }
  const auto hints =
      signature
          ? infer_lambda_argument_hints(*signature, args.all_args)
          : std::vector<std::vector<LambdaArgumentHint>>(args.all_args.size());
  for (size_t ai = 0; ai < args.all_args.size(); ai++) {
    if (args.all_args[ai].type == CType::FUNCTION && !args.all_args[ai].val &&
        !args.arg_lambda_names[ai].empty()) {
      auto &lname = args.arg_lambda_names[ai];
      auto def_it = deferred_functions_.find(lname);
      if (def_it != deferred_functions_.end()) {
        bool has_call_site_hints =
            ai < hints.size() &&
            std::any_of(
                hints[ai].begin(), hints[ai].end(),
                [](const LambdaArgumentHint &hint) { return hint.known; });
        if (!has_call_site_hints) {
          if (auto compiled = compiled_functions_.find(lname);
              compiled != compiled_functions_.end() && compiled->second.fn) {
            args.all_args[ai] = {compiled->second.fn,
                                 CType::FUNCTION,
                                 {compiled->second.return_type}};
            continue;
          }
        }
        std::vector<TypedValue> hint_args;
        std::vector<std::optional<LambdaArgumentHint>> checked_hints;
        // The source of an imported generic function is reparsed after
        // type checking. Its AST nodes are not owned by the original
        // type checker, whose node-keyed cache can otherwise hand us
        // stale (and, after allocator reuse, unrelated) type hints.
        // Inside the isolation scope, use the imported signature and
        // concrete call-site identities above instead.
        if (type_checker_ && genfn_isolation_depth_ == 0) {
          auto *checked =
              type_checker_->zonk(type_checker_->type_of(def_it->second.ast));
          for (size_t pi = 0;
               pi < def_it->second.param_names.size() && checked &&
               checked->tag == typechecker::MonoType::Arrow;
               ++pi) {
            auto *parameter = type_checker_->zonk(checked->param_type);
            checked_hints.push_back(
                parameter && parameter->tag != typechecker::MonoType::Var
                    ? std::optional<LambdaArgumentHint>(
                          checked_lambda_argument_hint(*type_checker_,
                                                       parameter))
                    : std::nullopt);
            checked = type_checker_->zonk(checked->return_type);
          }
        }
        for (size_t pi = 0; pi < def_it->second.param_names.size(); pi++) {
          const bool has_contextual_hint =
              ai < hints.size() && pi < hints[ai].size() && hints[ai][pi].known;
          const LambdaArgumentHint hint =
              has_contextual_hint ? hints[ai][pi]
              : pi < checked_hints.size() && checked_hints[pi].has_value()
                  ? *checked_hints[pi]
                  : LambdaArgumentHint{};
          auto value = dummy_typed_value(hint.type);
          if (hint.type == CType::ADT && !hint.adt_type_name.empty()) {
            value.val =
                Constant::getNullValue(adt_llvm_type(hint.adt_type_name));
          }
          value.adt_type_name = hint.adt_type_name;
          value.semantic_subtypes = hint.semantic_arguments;
          value.adt_semantic_arguments = hint.semantic_arguments;
          for (const auto &argument : hint.semantic_arguments)
            value.subtypes.push_back(argument.type);
          hint_args.push_back(std::move(value));
        }
        auto cf = compile_function(lname, def_it->second, hint_args);
        args.all_args[ai] = {cf.fn, CType::FUNCTION, {cf.return_type}};
      } else if (imports_.extern_functions.count(lname)) {
        auto imported = materialize_imported_function_value(lname);
        if (imported)
          args.all_args[ai] = imported;
      }
    }
  }
}

void Codegen::wrap_function_args_in_closures(
    std::vector<TypedValue> &all_args) {
  for (size_t ai = 0; ai < all_args.size(); ai++) {
    if (all_args[ai].type == CType::FUNCTION && all_args[ai].val &&
        isa<Function>(all_args[ai].val)) {
      // Ensure subtypes have return type info
      if (all_args[ai].subtypes.empty()) {
        auto cf_it = compiled_functions_.find(
            cast<Function>(all_args[ai].val)->getName().str());
        if (cf_it != compiled_functions_.end())
          all_args[ai].subtypes = {cf_it->second.return_type};
      }
      CType fn_ret = (!all_args[ai].subtypes.empty()) ? all_args[ai].subtypes[0]
                                                      : CType::INT;
      auto *underlying_fn = cast<Function>(all_args[ai].val);
      all_args[ai].val = wrap_in_closure(underlying_fn, fn_ret);
    }
  }
}

TypedValue
Codegen::codegen_adt_construct(const std::string &fn_name,
                               const std::vector<TypedValue> &all_args) {
  auto &info = types_.adt_constructors[fn_name];
  auto tag_ty = LType::getInt64Ty(*context_);
  auto i64_ty = LType::getInt64Ty(*context_);

  // Helper: cast value to i64 for storage. ADT structs (multi-i64) don't
  // fit in a single i64 — box them to heap first and pass the pointer.
  // The boxed ADT inherits its inner field heap_mask from `subtypes`.
  auto to_i64 = [&](const TypedValue &tv) -> Value * {
    Value *arg_val = tv.val;
    if (arg_val->getType() == i64_ty)
      return arg_val;
    if (arg_val->getType()->isIntegerTy())
      return builder_->CreateZExtOrTrunc(arg_val, i64_ty);
    if (arg_val->getType()->isPointerTy())
      return builder_->CreatePtrToInt(arg_val, i64_ty);
    if (arg_val->getType()->isDoubleTy())
      return builder_->CreateBitCast(arg_val, i64_ty);
    if (arg_val->getType()->isStructTy()) {
      // Non-recursive ADT struct {tag, fields...} — box to heap.
      auto *sty = llvm::cast<llvm::StructType>(arg_val->getType());
      unsigned num_fields = sty->getNumElements();
      if (num_fields == 1)
        return builder_->CreateExtractValue(arg_val, {0}, "adt_tag_value");
      auto *tag_v = builder_->CreateExtractValue(arg_val, {0});
      auto *boxed = builder_->CreateCall(
          rt_.adt_alloc_, {tag_v, ConstantInt::get(i64_ty, num_fields - 1)});
      for (unsigned fi = 1; fi < num_fields; fi++) {
        auto *fv = builder_->CreateExtractValue(arg_val, {fi});
        builder_->CreateCall(rt_.adt_set_field_,
                             {boxed, ConstantInt::get(i64_ty, fi - 1), fv});
      }
      // Carry the inner ADT's per-field heap mask so this newly boxed
      // copy frees its heap fields when destroyed.
      int64_t inner_mask = 0;
      for (size_t fi = 0; fi < tv.subtypes.size() && fi < 64; fi++)
        if (is_heap_type(tv.subtypes[fi]))
          inner_mask |= ((int64_t)1 << fi);
      if (inner_mask != 0)
        builder_->CreateCall(rt_.adt_set_heap_mask_,
                             {boxed, ConstantInt::get(i64_ty, inner_mask)});
      return builder_->CreatePtrToInt(boxed, i64_ty, "adt_field_box");
    }
    return arg_val;
  };

  // Capture per-field CTypes of the supplied args for downstream pattern
  // matching — ADT's field_types is often empty for generic fields (e.g.
  // `type Linear a = Linear a`), so we can't rely on the registry.
  std::vector<CType> field_subtypes;
  for (size_t ai = 0; ai < all_args.size() && ai < (size_t)info.arity; ai++)
    field_subtypes.push_back(all_args[ai].type);

  {
    // Every ADT uses the same heap ABI. A single representation is
    // essential for generic functions, closures, and separately compiled
    // modules: their call signatures cannot depend on whether a concrete
    // constructor happened to be recursive or carried a heap field.
    auto *node_ptr =
        builder_->CreateCall(rt_.adt_alloc_,
                             {ConstantInt::get(tag_ty, info.tag),
                              ConstantInt::get(i64_ty, info.arity)},
                             "adt_node");
    int64_t adt_heap_mask = 0;
    for (size_t ai = 0; ai < all_args.size() && ai < (size_t)info.arity; ai++) {
      Value *arg_val = to_i64(all_args[ai]);
      // Storing a heap-typed value into the ADT transfers/shares
      // ownership — rc_inc so the field's lifetime is tied to the ADT,
      // not the original binding.
      if (is_heap_value(all_args[ai]) &&
          !all_args[ai].val->getType()->isStructTy())
        emit_rc_inc(all_args[ai].val, all_args[ai].type);
      builder_->CreateCall(rt_.adt_set_field_,
                           {node_ptr, ConstantInt::get(i64_ty, ai), arg_val});
      if (is_heap_value(all_args[ai]) && ai < 64)
        adt_heap_mask |= ((int64_t)1 << ai);
    }
    if (adt_heap_mask != 0)
      builder_->CreateCall(rt_.adt_set_heap_mask_,
                           {node_ptr, ConstantInt::get(i64_ty, adt_heap_mask)});
    TypedValue result{node_ptr, CType::ADT};
    result.adt_type_name = info.type_name;
    result.subtypes = field_subtypes;
    if (const auto params = types_.adt_type_params.find(info.type_name);
        params != types_.adt_type_params.end()) {
      result.adt_type_arguments.assign(params->second.size(), CType::INT);
      result.adt_type_argument_names.resize(params->second.size());
      result.adt_semantic_arguments.resize(params->second.size());
      for (size_t fi = 0;
           fi < all_args.size() && fi < info.declared_field_types.size();
           ++fi) {
        const auto &declared = info.declared_field_types[fi];
        const auto parameter = std::find(params->second.begin(),
                                         params->second.end(), declared.name);
        if (parameter == params->second.end())
          continue;
        const size_t pi = static_cast<size_t>(
            std::distance(params->second.begin(), parameter));
        result.adt_type_arguments[pi] = all_args[fi].type;
        result.adt_type_argument_names[pi] = all_args[fi].adt_type_name;
        auto &identity = result.adt_semantic_arguments[pi];
        identity.type = all_args[fi].type;
        identity.adt_name = all_args[fi].adt_type_name;
        identity.arguments = !all_args[fi].semantic_subtypes.empty()
                                 ? all_args[fi].semantic_subtypes
                                 : all_args[fi].adt_semantic_arguments;
      }
    }
    for (auto &a : all_args)
      if (a.boxed_heap) {
        result.boxed_heap = true;
        break;
      }
    return result;
  }
}

std::unordered_map<std::string, Codegen::CompiledFunction>::iterator
Codegen::resolve_apply_function(const std::string &fn_name,
                                const std::vector<TypedValue> &all_args,
                                const ApplyExpr *application) {
  // Source-defined polymorphic helpers need the same complete semantic
  // specialization as exported GENFNs.  In particular, an imported public
  // GENFN may call private siblings registered under their local names.
  // Caching those siblings by the local name alone erases nested element
  // identities (for example Seq (Int, String)) and can compile an unsafe
  // ownership path for every later instantiation.
  if (auto deferred = deferred_functions_.find(fn_name);
      deferred != deferred_functions_.end() &&
      deferred->second.ast->type_signature &&
      contains_type_variable(*deferred->second.ast->type_signature)) {
    std::string specialization = fn_name + "__genfn";
    for (const auto &argument : all_args)
      append_specialization_identity(specialization,
                                     semantic_identity_of(argument));
    if (auto compiled = compiled_functions_.find(specialization);
        compiled != compiled_functions_.end())
      return compiled;

    // Copy before compilation: nested codegen can grow the deferred map.
    const DeferredFunction definition = deferred->second;
    compile_function(specialization, definition, all_args);
    return compiled_functions_.find(specialization);
  }

  // First try direct lookup, then resolve through named_values_ indirection
  auto cf_it = compiled_functions_.find(fn_name);
  if (cf_it == compiled_functions_.end()) {
    // Check if fn_name is an alias for a compiled function (e.g., let g =
    // partial_fn)
    auto nv_it = named_values_.find(fn_name);
    if (nv_it != named_values_.end() && nv_it->second.type == CType::FUNCTION &&
        nv_it->second.val) {
      if (auto *llvm_fn = dyn_cast<Function>(nv_it->second.val)) {
        // Find by LLVM function name
        cf_it = compiled_functions_.find(llvm_fn->getName().str());
      }
    }
  }
  if (cf_it == compiled_functions_.end()) {
    // Check deferred
    auto def_it = deferred_functions_.find(fn_name);
    if (def_it != deferred_functions_.end()) {
      compile_function(fn_name, def_it->second, all_args);
      cf_it = compiled_functions_.find(fn_name);
    }
  }

  // Trait method resolution: if not found yet, check if fn_name is a trait
  // method
  if (cf_it == compiled_functions_.end()) {
    CType first_arg_type = all_args.empty() ? CType::INT : all_args[0].type;
    std::string first_adt_name =
        all_args.empty() ? "" : all_args[0].adt_type_name;
    std::string resolved;
    if (type_checker_ && application && genfn_isolation_depth_ == 0) {
      const ApplyExpr *selected_application = application;
      std::optional<typechecker::TypeChecker::SelectedTraitInstance>
          selected_instance;
      while (selected_application && !selected_instance) {
        selected_instance =
            type_checker_->selected_trait_instance(selected_application);
        auto *expression_call =
            dynamic_cast<ExprCall *>(selected_application->call);
        selected_application =
            expression_call ? dynamic_cast<ApplyExpr *>(expression_call->expr)
                            : nullptr;
      }
      if (selected_instance) {
        const auto &selected = *selected_instance;
        std::string key = selected.trait_name;
        for (const auto &head : selected.type_names)
          key += ":" + head;
        if (const auto instance = types_.trait_instances.find(key);
            instance != types_.trait_instances.end()) {
          if (const auto method =
                  instance->second.method_mangled_names.find(fn_name);
              method != instance->second.method_mangled_names.end())
            resolved = method->second;
        }
      }
    }
    // Reparsed GENFN bodies are created after the importing module's
    // TypeChecker pass, so they have no AST-keyed selection entry. Resolve
    // concrete overloaded methods from their complete call ABI instead of
    // falling back to the receiver alone. Nominal ADTs are checked as well
    // as CType so equally represented instance heads remain distinct.
    if (resolved.empty()) {
      std::vector<std::string> candidates;
      for (const auto &[_, trait] : types_.traits) {
        if (std::find(trait.method_names.begin(), trait.method_names.end(),
                      fn_name) == trait.method_names.end())
          continue;
        for (const auto &[__, instance] : types_.trait_instances) {
          if (instance.trait_name != trait.name)
            continue;
          const auto method = instance.method_mangled_names.find(fn_name);
          if (method == instance.method_mangled_names.end())
            continue;
          const auto meta = imports_.meta.find(method->second);
          const auto compiled = compiled_functions_.find(method->second);
          const auto *parameter_types = meta != imports_.meta.end()
                                            ? &meta->second.param_types
                                        : compiled != compiled_functions_.end()
                                            ? &compiled->second.param_types
                                            : nullptr;
          bool matches = true;
          const auto signature = trait.method_type_descriptors.find(fn_name);
          const auto descriptor =
              signature == trait.method_type_descriptors.end()
                  ? std::nullopt
                  : parse_type_descriptor(signature->second);
          if (descriptor) {
            std::vector<const TypeDescriptorNode *> parameters;
            flatten_function_descriptor(*descriptor, parameters);
            if (parameters.size() != all_args.size())
              matches = false;
            for (size_t i = 0; matches && i < all_args.size(); ++i) {
              if (!descriptor_parameter_matches(*parameters[i], all_args[i],
                                                trait, instance)) {
                matches = false;
              }
            }
          } else if (!parameter_types) {
            matches = false;
          }
          if (matches && parameter_types &&
              parameter_types->size() != all_args.size()) {
            matches = false;
          }
          if (!matches)
            continue;
          if (parameter_types && parameter_types->size() != all_args.size()) {
            continue;
          }
          for (size_t i = 0; matches && parameter_types && i < all_args.size();
               ++i) {
            if ((*parameter_types)[i] != all_args[i].type) {
              matches = false;
              break;
            }
            std::string expected_adt;
            if (meta != imports_.meta.end() &&
                i < meta->second.param_type_descriptors.size()) {
              const auto descriptor =
                  parse_type_descriptor(meta->second.param_type_descriptors[i]);
              if (descriptor && descriptor->name == "ADT" &&
                  !descriptor->arguments.empty())
                expected_adt = descriptor->arguments.front().name;
            } else if (compiled != compiled_functions_.end() &&
                       i < compiled->second.param_adt_names.size()) {
              expected_adt = compiled->second.param_adt_names[i];
            }
            if (!expected_adt.empty() &&
                expected_adt != all_args[i].adt_type_name) {
              matches = false;
              break;
            }
          }
          if (matches)
            candidates.push_back(method->second);
        }
      }
      std::sort(candidates.begin(), candidates.end());
      candidates.erase(std::unique(candidates.begin(), candidates.end()),
                       candidates.end());
      if (candidates.size() == 1)
        resolved = candidates.front();
    }
    if (resolved.empty())
      resolved = resolve_trait_method(fn_name, first_arg_type, first_adt_name);
    if (!resolved.empty()) {
      // Generic trait implementations must be specialized at nested
      // method calls too (`Eq Result` calling `Eq Option`), not only at
      // surface comparison operators. Calling the bootstrap declaration
      // directly can otherwise mix a flat generic struct signature with
      // a heap-specialized argument.
      const bool element_dispatch_method =
          fn_name == "eq" || fn_name == "compare" || fn_name == "hash" ||
          fn_name == "show";
      if (!all_args.empty() && imports_.imported_sources.count(resolved) &&
          (!all_args[0].adt_semantic_arguments.empty() ||
           !all_args[0].adt_type_arguments.empty() ||
           (element_dispatch_method && !all_args[0].subtypes.empty()))) {
        std::string specialization = resolved + "__";
        std::function<void(const SemanticTypeIdentity &)> append_identity;
        append_identity = [&](const SemanticTypeIdentity &identity) {
          specialization += std::to_string(static_cast<int>(identity.type));
          if (!identity.adt_name.empty())
            specialization += "_" + identity.adt_name;
          for (const auto &argument : identity.arguments) {
            specialization += "_of_";
            append_identity(argument);
          }
        };
        if (!all_args[0].adt_semantic_arguments.empty()) {
          for (const auto &argument : all_args[0].adt_semantic_arguments) {
            append_identity(argument);
            specialization += "_";
          }
        } else {
          const auto &arguments = !all_args[0].adt_type_arguments.empty()
                                      ? all_args[0].adt_type_arguments
                                      : all_args[0].subtypes;
          for (auto type : arguments)
            specialization += std::to_string(static_cast<int>(type)) + "_";
        }
        cf_it = compiled_functions_.find(specialization);
        if (cf_it == compiled_functions_.end()) {
          const auto &source = imports_.imported_sources.at(resolved);
          GenfnNameIsolation iso(*this, resolved);
          install_private_genfn_ctors(resolved);
          auto reparsed = reparse_genfn(source.local_name, source.source_text);
          if (reparsed && !reparsed->functions.empty()) {
            auto *function = reparsed->functions.front();
            reparsed->functions.clear();
            imports_.imported_ast_nodes.push_back(
                std::unique_ptr<FunctionExpr>(function));
            register_sibling_genfns(resolved);
            codegen_function_def(function, specialization);
            if (auto deferred = deferred_functions_.find(specialization);
                deferred != deferred_functions_.end())
              compile_function(specialization, deferred->second, all_args);
            cf_it = compiled_functions_.find(specialization);
            if (cf_it != compiled_functions_.end()) {
              CompiledFunction specialized = cf_it->second;
              iso.restore();
              compiled_functions_[specialization] = std::move(specialized);
              cf_it = compiled_functions_.find(specialization);
            }
          }
        }
        if (cf_it != compiled_functions_.end())
          return cf_it;
      }
      // Try compiled functions first
      cf_it = compiled_functions_.find(resolved);
      if (cf_it == compiled_functions_.end()) {
        // Try deferred
        auto def_it = deferred_functions_.find(resolved);
        if (def_it != deferred_functions_.end()) {
          compile_function(resolved, def_it->second, all_args);
          cf_it = compiled_functions_.find(resolved);
        }
      }
      if (cf_it == compiled_functions_.end() ||
          (cf_it->second.fn && cf_it->second.fn->isDeclaration() &&
           imports_.imported_sources.count(resolved))) {
        // Concrete imported instances (for example Num Int) have no
        // type arguments to trigger the specialization path above.
        // Their interface still carries GENFN source, so compile the
        // canonical implementation on demand instead of assuming a
        // separately linked module object exists.
        auto source = imports_.imported_sources.find(resolved);
        if (source != imports_.imported_sources.end()) {
          GenfnNameIsolation iso(*this, resolved);
          if (cf_it != compiled_functions_.end()) {
            compiled_functions_.erase(cf_it);
            cf_it = compiled_functions_.end();
          }
          install_private_genfn_ctors(resolved);
          auto reparsed = reparse_genfn(source->second.local_name,
                                        source->second.source_text);
          if (reparsed && !reparsed->functions.empty()) {
            auto *function = reparsed->functions.front();
            reparsed->functions.clear();
            imports_.imported_ast_nodes.push_back(
                std::unique_ptr<FunctionExpr>(function));
            register_sibling_genfns(resolved);
            codegen_function_def(function, resolved);
            if (auto deferred = deferred_functions_.find(resolved);
                deferred != deferred_functions_.end()) {
              compile_function(resolved, deferred->second, all_args);
              cf_it = compiled_functions_.find(resolved);
            }
            if (cf_it != compiled_functions_.end()) {
              CompiledFunction specialized = cf_it->second;
              iso.restore();
              compiled_functions_[resolved] = std::move(specialized);
              cf_it = compiled_functions_.find(resolved);
            }
          }
        }
      }
    }
  }
  return cf_it;
}

TypedValue
Codegen::codegen_higher_order_call(const std::string &fn_name,
                                   const std::vector<TypedValue> &all_args) {
  auto var_it = named_values_.find(fn_name);
  // Keep Unit arguments. `f()` is syntax for applying the Unit value, and
  // a closure such as `\_ -> ...` has one Unit-compatible parameter.  The
  // old blanket removal emitted a call with too few ABI arguments.
  std::vector<LType *> arg_types;
  std::vector<Value *> vals;
  for (auto &a : all_args) {
    arg_types.push_back(a.val->getType());
    vals.push_back(a.val);
  }
  CType first_arg = all_args.empty() ? CType::INT : all_args.front().type;
  CType ret_ctype = (!var_it->second.subtypes.empty())
                        ? var_it->second.subtypes[0]
                        : first_arg;
  auto ret_llvm = llvm_type(ret_ctype);
  auto var_val = var_it->second.val;
  auto decode_universal_result = [&](Value *universal) -> TypedValue {
    auto *i64_ty = LType::getInt64Ty(*context_);
    if (ret_ctype == CType::BOOL) {
      auto *integer = universal->getType()->isIntegerTy(64)
                          ? universal
                          : builder_->CreateZExtOrTrunc(universal, i64_ty);
      return {builder_->CreateICmpNE(integer, ConstantInt::get(i64_ty, 0),
                                     "closure_bool"),
              CType::BOOL};
    }
    if (ret_ctype == CType::FLOAT) {
      auto *bits = universal->getType()->isIntegerTy(64)
                       ? universal
                       : builder_->CreateZExtOrTrunc(universal, i64_ty);
      return {builder_->CreateBitCast(bits, LType::getDoubleTy(*context_),
                                      "closure_float"),
              CType::FLOAT};
    }
    if (ret_ctype == CType::STRING || ret_ctype == CType::SEQ ||
        ret_ctype == CType::SET || ret_ctype == CType::DICT ||
        ret_ctype == CType::FUNCTION || ret_ctype == CType::ADT ||
        ret_ctype == CType::BYTE_ARRAY || ret_ctype == CType::INT_ARRAY ||
        ret_ctype == CType::FLOAT_ARRAY || ret_ctype == CType::CHANNEL ||
        ret_ctype == CType::PROMISE) {
      auto *pointer =
          universal->getType()->isPointerTy()
              ? universal
              : builder_->CreateIntToPtr(
                    universal, PointerType::get(*context_, 0), "closure_ptr");
      TypedValue decoded{pointer, ret_ctype};
      decoded.adt_type_name = var_it->second.adt_type_name;
      return decoded;
    }
    return {universal, ret_ctype};
  };

  if (isa<Function>(var_val) || effect_resume_names_.count(fn_name)) {
    // Direct function pointer or effect resume fn ptr
    auto fn_type = llvm::FunctionType::get(ret_llvm, arg_types, false);
    auto result = builder_->CreateCall(fn_type, var_val, vals, "indirect_call");
    return {result, ret_ctype};
  } else {
    // Every closure call goes through the same universal ABI. Keeping the
    // ownership path uniform lets closures passed through generic
    // functions retain their exact borrow contract.
    auto i64_ty = LType::getInt64Ty(*context_);
    auto ptr_ty = PointerType::get(*context_, 0);

    // Load arity from closure[2] to handle over-application (currying)
    auto *arity_gep = builder_->CreateGEP(
        i64_ty, var_val, {ConstantInt::get(i64_ty, CLOSURE_FIELD_ARITY)},
        "arity_ptr");
    auto *arity_val = builder_->CreateLoad(i64_ty, arity_gep, "arity");

    // Apply args, handling over-application by iterating
    Value *current_closure = var_val;
    size_t args_consumed = 0;

    while (args_consumed < vals.size()) {
      auto *fn_i64 =
          builder_->CreateLoad(i64_ty, current_closure, "closure_fn_i64");
      auto *fn_ptr = builder_->CreateIntToPtr(fn_i64, ptr_ty, "closure_fn_ptr");

      // Load arity for current closure
      auto *cur_arity_gep = builder_->CreateGEP(
          i64_ty, current_closure,
          {ConstantInt::get(i64_ty, CLOSURE_FIELD_ARITY)}, "cur_arity_ptr");
      auto *cur_arity =
          builder_->CreateLoad(i64_ty, cur_arity_gep, "cur_arity");

      // For compile-time constant arity (common case), use it directly.
      // Otherwise, call with all remaining args (non-curried).
      int64_t static_arity = -1;
      if (auto *ci = dyn_cast<ConstantInt>(cur_arity))
        static_arity = ci->getZExtValue();

      size_t n_args_this_call;
      if (static_arity >= 0) {
        n_args_this_call = std::min(static_cast<size_t>(static_arity),
                                    vals.size() - args_consumed);
      } else {
        n_args_this_call = vals.size() - args_consumed;
      }

      auto *borrow_mask_gep = builder_->CreateGEP(
          i64_ty, current_closure,
          {ConstantInt::get(i64_ty, CLOSURE_FIELD_BORROW_MASK)},
          "borrow_mask_ptr");
      auto *current_borrow_mask =
          builder_->CreateLoad(i64_ty, borrow_mask_gep, "borrow_mask");

      // Apply the callback's ownership contract before the call. An
      // owned last-use argument transfers its existing reference; an
      // owned non-last-use argument is duplicated; a borrowed argument
      // remains entirely caller-owned.
      std::vector<std::pair<TypedValue, Value *>> borrowed_temporaries;
      for (size_t ai = 0; ai < n_args_this_call; ++ai) {
        const size_t argument_index = args_consumed + ai;
        if (argument_index >= all_args.size())
          break;
        const auto &argument = all_args[argument_index];
        if (!is_heap_type(argument.type) || !argument.val ||
            isa<Constant>(argument.val) ||
            argument.val->getType()->isStructTy())
          continue;

        auto *bit = builder_->CreateAnd(
            current_borrow_mask, ConstantInt::get(i64_ty, int64_t{1} << ai),
            "borrow_bit");
        auto *is_borrowed = builder_->CreateICmpNE(
            bit, ConstantInt::get(i64_ty, 0), "arg_is_borrowed");

        std::string binding;
        for (const auto &[name, value] : named_values_)
          if (value.val == argument.val) {
            binding = name;
            break;
          }

        // A recursive local function's environment is its first LLVM
        // parameter. Passing that self value to a borrowing callback
        // is not a consumable "last textual use": every recursive
        // invocation shares the same environment reference.
        auto *argument_parameter = dyn_cast<Argument>(argument.val);
        auto *active_function = builder_->GetInsertBlock()->getParent();
        const bool recursive_self_environment =
            argument_parameter && argument_parameter->getArgNo() == 0 &&
            active_function && active_function->arg_size() > 0 &&
            active_function->getArg(0) == argument_parameter &&
            argument_parameter->getType()->isPointerTy();
        if (recursive_self_environment && binding.empty())
          binding = active_function->getName().str();

        const bool last_use = !recursive_self_environment && !binding.empty() &&
                              current_fn_body_ &&
                              compiler::analysis::max_identifier_refs_on_path(
                                  current_fn_body_, binding) == 1;
        if (last_use) {
          auto *owned_transfer =
              builder_->CreateNot(is_borrowed, "owned_transfer");
          if (current_frame_alloca_) {
            auto *transfer_bb =
                BasicBlock::Create(*context_, "closure_transfer",
                                   builder_->GetInsertBlock()->getParent());
            auto *continue_bb =
                BasicBlock::Create(*context_, "closure_transfer_cont",
                                   builder_->GetInsertBlock()->getParent());
            builder_->CreateCondBr(owned_transfer, transfer_bb, continue_bb);
            builder_->SetInsertPoint(transfer_bb);
            emit_frame_transfer(argument.val);
            builder_->CreateBr(continue_bb);
            builder_->SetInsertPoint(continue_bb);
          }
          if (argument.type == CType::SEQ)
            mark_transferred(argument.val, TransferDomain::Seq);
          else
            mark_transferred(argument.val, TransferDomain::Map);
          // Normalize both contracts to a consumed last use. The
          // callback consumes an owned argument; after a borrowing
          // callback returns, the call site consumes it instead.
          borrowed_temporaries.emplace_back(argument, is_borrowed);
          // Pattern-bound heap heads carry one retained reference
          // in the active case-arm drop list. This normalized call
          // consumes that exact reference on both runtime paths, so
          // leaving the scheduled arm drop would release it twice.
          for (auto drops = arm_drop_stack_.rbegin();
               drops != arm_drop_stack_.rend(); ++drops) {
            auto &entries = *drops;
            const auto old_size = entries.size();
            entries.erase(std::remove_if(entries.begin(), entries.end(),
                                         [&](const auto &entry) {
                                           return entry.first == argument.val;
                                         }),
                          entries.end());
            if (entries.size() != old_size)
              break;
          }
        } else if (!binding.empty()) {
          auto *retain_bb =
              BasicBlock::Create(*context_, "closure_retain",
                                 builder_->GetInsertBlock()->getParent());
          auto *continue_bb =
              BasicBlock::Create(*context_, "closure_retain_cont",
                                 builder_->GetInsertBlock()->getParent());
          builder_->CreateCondBr(is_borrowed, continue_bb, retain_bb);
          builder_->SetInsertPoint(retain_bb);
          emit_rc_inc(argument.val, argument.type);
          builder_->CreateBr(continue_bb);
          builder_->SetInsertPoint(continue_bb);
        } else {
          borrowed_temporaries.emplace_back(argument, is_borrowed);
        }
      }

      // Build closure call: fn(env, args...)
      std::vector<LType *> call_arg_types = {ptr_ty};
      std::vector<Value *> call_vals = {current_closure};
      for (size_t ai = 0; ai < n_args_this_call; ai++) {
        call_arg_types.push_back(vals[args_consumed + ai]->getType());
        call_vals.push_back(vals[args_consumed + ai]);
      }

      // Use the actual return type for the closure call
      // Every closure wrapper has the universal i64 return ABI.  Decode
      // that carrier only after the final application; intermediate
      // curried results carry the next closure pointer as i64.
      auto *closure_ret_ty = static_cast<LType *>(i64_ty);
      auto *call_type =
          llvm::FunctionType::get(closure_ret_ty, call_arg_types, false);
      auto *result =
          builder_->CreateCall(call_type, fn_ptr, call_vals, "closure_call");

      // A borrowing callback leaves an anonymous temporary owned by
      // this call site, where it must be released.
      for (const auto &[temporary, is_borrowed] : borrowed_temporaries) {
        auto *drop_bb =
            BasicBlock::Create(*context_, "drop_borrowed_temporary",
                               builder_->GetInsertBlock()->getParent());
        auto *continue_bb =
            BasicBlock::Create(*context_, "borrowed_temporary_cont",
                               builder_->GetInsertBlock()->getParent());
        builder_->CreateCondBr(is_borrowed, drop_bb, continue_bb);
        builder_->SetInsertPoint(drop_bb);
        emit_frame_transfer(temporary.val);
        emit_rc_dec(temporary.val, temporary.type);
        builder_->CreateBr(continue_bb);
        builder_->SetInsertPoint(continue_bb);
      }

      args_consumed += n_args_this_call;

      if (args_consumed < vals.size()) {
        current_closure =
            builder_->CreateIntToPtr(result, ptr_ty, "curried_closure");
      } else {
        return decode_universal_result(result);
      }
    }

    // Genuine zero-argument closure call.
    auto *fn_i64 =
        builder_->CreateLoad(i64_ty, current_closure, "closure_fn_i64");
    auto *fn_ptr_val =
        builder_->CreateIntToPtr(fn_i64, ptr_ty, "closure_fn_ptr");
    auto *call_type = llvm::FunctionType::get(i64_ty, {ptr_ty}, false);
    auto *result = builder_->CreateCall(call_type, fn_ptr_val,
                                        {current_closure}, "closure_call");
    return decode_universal_result(result);
  }
}

TypedValue
Codegen::codegen_extern_call(ApplyExpr *node, const std::string &fn_name,
                             const std::vector<TypedValue> &all_args) {
  auto ext_it = imports_.extern_functions.find(fn_name);
  std::string mangled = ext_it->second;

  // On-demand GENFN re-parse. Source bodies compile in this Codegen
  // instance, so `perform` inside an imported function sees the current
  // handler_stack_ (effect-row GENFN, not a C++ name list). Types that
  // differ from the pre-compiled signature also force remonomorphization.
  auto genfn_it = imports_.imported_sources.find(mangled);
  bool types_differ = false;
  if (genfn_it != imports_.imported_sources.end()) {
    auto meta_it2 = imports_.meta.find(mangled);
    if (meta_it2 != imports_.meta.end()) {
      auto &meta = meta_it2->second;
      // Check if ALL metadata params are INT (indicates a boxed
      // extern wrapper where all types are i64 at the ABI level).
      bool all_meta_int = true;
      for (auto ct : meta.param_types)
        if (ct != CType::INT) {
          all_meta_int = false;
          break;
        }

      for (size_t i = 0; i < all_args.size() && i < meta.param_types.size();
           i++) {
        CType a = all_args[i].type, m = meta.param_types[i];
        if (a == m)
          continue;
        // For boxed extern wrappers (all-INT metadata), skip GENFN
        // when the arg type is still i64-compatible AND doesn't
        // need different semantic codegen. STRING, BOOL, SYMBOL
        // are fine (i64 values or pointers treated as i64).
        // ADT, TUPLE, FUNCTION need GENFN (different codegen).
        if (all_meta_int && m == CType::INT &&
            (a == CType::STRING || a == CType::BOOL || a == CType::SYMBOL ||
             a == CType::SEQ || a == CType::SET || a == CType::DICT ||
             a == CType::BYTE_ARRAY))
          continue;
        types_differ = true;
        break;
      }
    }
  }
  if (genfn_it != imports_.imported_sources.end()) {
    auto &ifs = genfn_it->second;
    std::string materialization_name = fn_name;
    // A lifted trait method is polymorphic even though it has a stable
    // exported bootstrap symbol.  Explicit calls (including callbacks
    // passed to a generic helper) must therefore retain the same complete
    // semantic identity as operator-driven dispatch.  Materializing plain
    // `Ord_Seq__compare`, for example, loses the element type and makes
    // `get` return an opaque ADT inside the implementation. Concrete
    // methods need the suffix too: caching `Hash Int` as plain `hash`
    // would shadow every later Hash instance in the module.
    materialization_name += "__genfn";
    for (const auto &argument : all_args) {
      auto identity = semantic_identity_of(argument);
      if (identity.arguments.empty())
        for (const auto subtype : argument.subtypes)
          identity.arguments.push_back({subtype, {}, {}});
      append_specialization_identity(materialization_name, identity);
    }
    if (auto cached = compiled_functions_.find(materialization_name);
        cached != compiled_functions_.end())
      return emit_direct_call(materialization_name, cached->second, all_args);
    // Reparse in an isolated defining-module scope, with any private ADT
    // constructors installed before parsing their expression syntax.
    GenfnNameIsolation iso(*this, mangled);
    install_private_genfn_ctors(mangled);
    auto reparsed = reparse_genfn(ifs.local_name, ifs.source_text);
    if (reparsed && !reparsed->functions.empty()) {
      auto *func_ast = reparsed->functions[0];
      reparsed->functions.clear();
      imports_.imported_ast_nodes.push_back(
          std::unique_ptr<FunctionExpr>(func_ast));
      int errors_before = Session->errorCount();
      register_sibling_genfns(mangled);
      codegen_function_def(func_ast, materialization_name);
      auto def_it2 = deferred_functions_.find(materialization_name);
      if (def_it2 != deferred_functions_.end()) {
        // Retain the exact defining GENFN identity. Recursive source aliases
        // and private dependencies must be scoped by this owner, while trait
        // implementation owners deliberately redispatch same-named methods.
        def_it2->second.imported_owner = mangled;
        compile_function(materialization_name, def_it2->second, all_args);
        auto cf_it2 = compiled_functions_.find(materialization_name);
        if (Session->errorCount() > errors_before) {
          compiled_functions_.erase(materialization_name);
          deferred_functions_.erase(materialization_name);
          iso.restore();
          // Fall through to the precompiled extern instead of a
          // half-compiled GENFN body that could not resolve helpers.
        } else if (cf_it2 != compiled_functions_.end()) {
          // Restoring the importer scope reinstates any hidden
          // bootstrap declaration under the same logical name.
          // Preserve the freshly monomorphized function metadata
          // (notably structural tuple return fields) across that
          // restoration instead of retaining a reference into the
          // map that restore() is about to mutate.
          CompiledFunction specialized = cf_it2->second;
          iso.restore();
          compiled_functions_[materialization_name] = std::move(specialized);
          auto &cf2 = compiled_functions_[materialization_name];
          size_t genfn_arity =
              cf2.param_types.size() - cf2.capture_names.size();
          if (all_args.size() < genfn_arity)
            return codegen_partial_apply(materialization_name, cf2, all_args);
          // Same Perceus DUP / return-subtype path as a local call.
          // A raw CreateCall skipped rc_inc on reused named Json
          // values, so a second `get j key` saw a consumed object.
          return emit_direct_call(materialization_name, cf2, all_args);
        } else {
          iso.restore();
        }
      } else {
        iso.restore();
      }
    }
    // Fallthrough: if re-parse failed, call as extern
  }

  // Try to get return type from module metadata
  CType ret_ctype = CType::INT; // default
  auto meta_it = imports_.meta.find(mangled);
  if (meta_it != imports_.meta.end()) {
    ret_ctype = meta_it->second.return_type;
  } else {
    // Check CFFI signatures
    auto cffi_it = types_.cffi_signatures.find(mangled);
    if (cffi_it != types_.cffi_signatures.end())
      ret_ctype = cffi_it->second.return_type;
  }

  // Declare the extern function. Boxed wrappers (all-INT metadata)
  // return i64; typed wrappers (with FLOAT etc.) return their native type.
  auto i64_ty_local = LType::getInt64Ty(*context_);
  bool is_boxed = true;
  if (meta_it != imports_.meta.end()) {
    for (auto ct : meta_it->second.param_types)
      if (ct != CType::INT) {
        is_boxed = false;
        break;
      }
    if (meta_it->second.return_type != CType::INT &&
        meta_it->second.return_type != CType::BOOL &&
        meta_it->second.return_type != CType::SYMBOL)
      is_boxed = (meta_it->second.return_type == CType::STRING ||
                  meta_it->second.return_type == CType::SEQ ||
                  meta_it->second.return_type == CType::SET ||
                  meta_it->second.return_type == CType::DICT ||
                  meta_it->second.return_type == CType::ADT) &&
                 is_boxed;
  }

  std::vector<LType *> arg_types;
  if (meta_it != imports_.meta.end()) {
    for (size_t ai = 0; ai < all_args.size(); ai++) {
      if (!is_boxed && ai < meta_it->second.param_types.size())
        arg_types.push_back(llvm_type(meta_it->second.param_types[ai]));
      else
        arg_types.push_back(all_args[ai].val->getType());
    }
  } else {
    for (auto &a : all_args)
      arg_types.push_back(a.val->getType());
  }
  auto ret_llvm = is_boxed ? i64_ty_local : llvm_type(ret_ctype);
  auto fn_type = llvm::FunctionType::get(ret_llvm, arg_types, false);

  auto *ext_fn = module_->getFunction(mangled);
  if (!ext_fn) {
    ext_fn =
        Function::Create(fn_type, Function::ExternalLinkage, mangled, module_);
  }

  std::optional<CompiledFunction> ext_cf;
  if (meta_it != imports_.meta.end()) {
    ext_cf = compiled_function_from_meta(ext_fn, meta_it->second, ret_ctype);
    prepare_callee_owned_heap_args(*ext_cf, all_args);
  }

  std::vector<Value *> vals;
  for (size_t ai = 0; ai < all_args.size(); ai++) {
    Value *arg_val = all_args[ai].val;
    auto *expected_ty =
        ai < arg_types.size() ? arg_types[ai] : arg_val->getType();
    if (arg_val->getType() != expected_ty) {
      if (arg_val->getType()->isStructTy() && expected_ty->isIntegerTy()) {
        auto *sty = llvm::cast<llvm::StructType>(arg_val->getType());
        unsigned num_elems = sty->getNumElements();
        if (num_elems <= 1) {
          /* Nullary / tag-only ADT: pass the discriminant. */
          arg_val = builder_->CreateExtractValue(arg_val, {0}, "adt_tag_arg");
          if (arg_val->getType() != expected_ty)
            arg_val = builder_->CreateZExtOrTrunc(arg_val, expected_ty);
        } else {
          /* Non-recursive ADT with fields: box to a heap ADT so C
           * sees a pointer (as i64), not a discarded payload. */
          auto *tag_v = builder_->CreateExtractValue(arg_val, {0});
          if (!tag_v->getType()->isIntegerTy(64))
            tag_v = builder_->CreateZExtOrTrunc(tag_v, i64_ty_local);
          auto *boxed = builder_->CreateCall(
              rt_.adt_alloc_,
              {tag_v, ConstantInt::get(i64_ty_local, num_elems - 1)},
              "adt_box_arg");
          int64_t heap_mask = 0;
          for (unsigned fi = 1; fi < num_elems; fi++) {
            auto *fv = builder_->CreateExtractValue(arg_val, {fi});
            if (!fv->getType()->isIntegerTy(64)) {
              if (fv->getType()->isPointerTy())
                fv = builder_->CreatePtrToInt(fv, i64_ty_local);
              else if (fv->getType()->isIntegerTy())
                fv = builder_->CreateZExtOrTrunc(fv, i64_ty_local);
              else if (fv->getType()->isDoubleTy())
                fv = builder_->CreateBitCast(fv, i64_ty_local);
            }
            builder_->CreateCall(
                rt_.adt_set_field_,
                {boxed, ConstantInt::get(i64_ty_local, fi - 1), fv});
            size_t sub_i = fi - 1;
            if (sub_i < all_args[ai].subtypes.size() &&
                is_heap_type(all_args[ai].subtypes[sub_i]) && sub_i < 64)
              heap_mask |= ((int64_t)1 << sub_i);
          }
          if (heap_mask != 0)
            builder_->CreateCall(
                rt_.adt_set_heap_mask_,
                {boxed, ConstantInt::get(i64_ty_local, heap_mask)});
          arg_val = builder_->CreatePtrToInt(boxed, expected_ty, "adt_box_i64");
        }
      } else if (arg_val->getType()->isIntegerTy() &&
                 expected_ty->isPointerTy())
        arg_val = builder_->CreateIntToPtr(arg_val, expected_ty);
      else if (arg_val->getType()->isPointerTy() && expected_ty->isIntegerTy())
        arg_val = builder_->CreatePtrToInt(arg_val, expected_ty);
      else if (arg_val->getType()->isIntegerTy() && expected_ty->isIntegerTy())
        arg_val = builder_->CreateZExtOrTrunc(arg_val, expected_ty);
    }
    vals.push_back(arg_val);
  }
  Value *ext_result = ext_fn->getReturnType()->isVoidTy()
                          ? builder_->CreateCall(ext_fn, vals)
                          : builder_->CreateCall(ext_fn, vals, "extern_call");
  if (ext_cf)
    cleanup_borrowed_temporary_args(*ext_cf, all_args);
  if (ext_fn->getReturnType()->isVoidTy())
    ext_result = ConstantInt::get(LType::getInt64Ty(*context_), 0);

  // Callee-owns for Set/Dict extern ops that consume their heap input
  // (e.g. Set.insert, Dict.put). For SET/DICT args and SET/DICT returns
  // (interchangeable since `{}` parses as SET), the callee either mutates
  // in place or path-copies + rc_dec's the old. Mark the arg as
  // transferred so the caller's function-exit DROP doesn't double-dec.
  // Mark MAP-domain ownership transfer so cleanup/exit skip rc_dec for
  // consumed SET/DICT values. Per-branch transfer_scope remains SEQ-only.
  if (!all_args.empty()) {
    CType a0 = all_args[0].type;
    bool a0_map = (a0 == CType::SET || a0 == CType::DICT);
    bool ret_map = (ret_ctype == CType::SET || ret_ctype == CType::DICT);
    if (a0_map && ret_map) {
      if (all_args[0].val && !isa<Constant>(all_args[0].val)) {
        mark_transferred(all_args[0].val, TransferDomain::Map);
        emit_frame_transfer(all_args[0].val);
      }
    }
  }

  // Convert boxed i64 result to the correct LLVM type
  if (is_boxed) {
    if (ret_ctype == CType::BOOL) {
      ext_result = builder_->CreateICmpNE(
          ext_result, ConstantInt::get(i64_ty_local, 0), "bool_conv");
    } else if (ret_ctype == CType::STRING || ret_ctype == CType::SEQ ||
               ret_ctype == CType::SET || ret_ctype == CType::DICT ||
               ret_ctype == CType::FUNCTION || ret_ctype == CType::ADT) {
      ext_result = builder_->CreateIntToPtr(
          ext_result, PointerType::get(*context_, 0), "ptr_conv");
    }
  } else if (ret_ctype == CType::ADT && ext_result->getType()->isIntegerTy()) {
    // Non-boxed call but ADT return: convert i64 to ptr for downstream
    // pattern matching to use the heap layout path.
    ext_result = builder_->CreateIntToPtr(
        ext_result, PointerType::get(*context_, 0), "adt_ptr_conv");
  }
  const bool return_linear =
      meta_it != imports_.meta.end() && meta_it->second.return_linear;
  if (return_linear && ext_result->getType()->isIntegerTy())
    ext_result = builder_->CreateIntToPtr(
        ext_result, PointerType::get(*context_, 0), "linear_return_ptr");
  TypedValue result{ext_result, return_linear ? CType::ADT : ret_ctype};
  if (return_linear) {
    result.adt_type_name = "Linear";
    result.subtypes = {meta_it->second.return_type};
  } else if (ret_ctype == CType::ADT && meta_it != imports_.meta.end() &&
             !meta_it->second.return_adt_name.empty())
    result.adt_type_name = meta_it->second.return_adt_name;
  if (meta_it != imports_.meta.end()) {
    if (auto identity = resolve_descriptor_identity(
            meta_it->second.return_type_descriptor,
            meta_it->second.param_type_descriptors, all_args)) {
      result.type = identity->type;
      result.adt_type_name = identity->adt_name;
      result.semantic_subtypes = identity->arguments;
      result.adt_semantic_arguments = identity->arguments;
      result.subtypes.clear();
      for (const auto &argument : identity->arguments) {
        result.subtypes.push_back(argument.type);
        result.adt_type_arguments.push_back(argument.type);
        result.adt_type_argument_names.push_back(argument.adt_name);
      }
      if (is_heap_type(result.type) && result.type != CType::TUPLE &&
          result.val->getType()->isIntegerTy())
        result.val =
            builder_->CreateIntToPtr(result.val, PointerType::get(*context_, 0),
                                     "generic_extern_return_ptr");
      else if (result.type == CType::FLOAT &&
               result.val->getType()->isIntegerTy(64))
        result.val =
            builder_->CreateBitCast(result.val, LType::getDoubleTy(*context_),
                                    "generic_extern_return_float");
      else if (result.type == CType::BOOL &&
               !result.val->getType()->isIntegerTy(1))
        result.val = builder_->CreateICmpNE(
            result.val, ConstantInt::get(result.val->getType(), 0),
            "generic_extern_return_bool");
    }
  }
  return result;
}

TypedValue
Codegen::codegen_partial_apply(const std::string &fn_name, CompiledFunction &cf,
                               const std::vector<TypedValue> &all_args) {
  size_t func_arity = cf.param_types.size() - cf.capture_names.size();
  size_t n_provided = all_args.size();
  size_t n_remaining = func_arity - n_provided;
  auto ptr_ty = PointerType::get(*context_, 0);
  auto i64_ty = LType::getInt64Ty(*context_);

  bool can_embed_args = true;
  for (const auto &arg : all_args) {
    if (!arg.val || (!isa<Constant>(arg.val) && !isa<Function>(arg.val))) {
      can_embed_args = false;
      break;
    }
  }
  if (can_embed_args) {
    std::vector<LType *> wrapper_params;
    for (size_t i = n_provided; i < func_arity; i++)
      wrapper_params.push_back(llvm_type(cf.param_types[i]));
    auto *wrapper_type =
        llvm::FunctionType::get(cf.fn->getReturnType(), wrapper_params, false);
    std::string wrapper_name = fn_name + "_partial" +
                               std::to_string(n_provided) + "of" +
                               std::to_string(func_arity);
    auto *wrapper = Function::Create(wrapper_type, Function::InternalLinkage,
                                     wrapper_name, module_);
    auto saved_block = builder_->GetInsertBlock();
    auto saved_point = builder_->GetInsertPoint();
    auto *entry = BasicBlock::Create(*context_, "entry", wrapper);
    builder_->SetInsertPoint(entry);
    std::vector<Value *> inner_call_args;
    for (auto &a : all_args)
      inner_call_args.push_back(a.val);
    for (auto &arg : wrapper->args())
      inner_call_args.push_back(&arg);
    for (auto &cap_name : cf.capture_names) {
      auto cap_it = named_values_.find(cap_name);
      if (cap_it != named_values_.end())
        inner_call_args.push_back(cap_it->second.val);
    }
    auto *result = builder_->CreateCall(cf.fn, inner_call_args, "partial_call");
    builder_->CreateRet(result);
    builder_->SetInsertPoint(saved_block, saved_point);
    CompiledFunction partial_cf;
    partial_cf.fn = wrapper;
    partial_cf.return_type = cf.return_type;
    for (size_t i = n_provided; i < func_arity; i++)
      partial_cf.param_types.push_back(cf.param_types[i]);
    compiled_functions_[wrapper_name] = partial_cf;
    named_values_[wrapper_name] = {wrapper, CType::FUNCTION};
    return {wrapper, CType::FUNCTION, {cf.return_type}};
  }

  // Build the wrapper's parameter types (remaining params)
  std::vector<LType *> wrapper_params = {ptr_ty};
  for (size_t i = n_provided; i < func_arity; i++)
    wrapper_params.push_back(llvm_type(cf.param_types[i]));

  auto ret_llvm = i64_ty;
  auto wrapper_type = llvm::FunctionType::get(ret_llvm, wrapper_params, false);
  std::string wrapper_name = fn_name + "_partial" + std::to_string(n_provided) +
                             "of" + std::to_string(func_arity);
  auto *wrapper = Function::Create(wrapper_type, Function::InternalLinkage,
                                   wrapper_name, module_);

  // Save state
  auto saved_block = builder_->GetInsertBlock();
  auto saved_point = builder_->GetInsertPoint();

  auto *entry = BasicBlock::Create(*context_, "entry", wrapper);
  builder_->SetInsertPoint(entry);

  std::vector<Value *> inner_call_args;
  auto arg_it = wrapper->arg_begin();
  Value *env = &*arg_it++;

  // First: the partially applied args captured in the closure env.
  for (size_t i = 0; i < n_provided; i++) {
    Value *cap =
        builder_->CreateCall(rt_.closure_get_cap_,
                             {env, ConstantInt::get(i64_ty, i)}, "partial_cap");
    auto *expected_ty = cf.fn->getArg(i)->getType();
    if (cap->getType() != expected_ty) {
      if (expected_ty->isPointerTy())
        cap = builder_->CreateIntToPtr(cap, expected_ty);
      else if (expected_ty->isIntegerTy())
        cap = builder_->CreateZExtOrTrunc(cap, expected_ty);
    }
    inner_call_args.push_back(cap);
  }

  // Then: the remaining params (wrapper's normal parameters).
  for (; arg_it != wrapper->arg_end(); ++arg_it)
    inner_call_args.push_back(&*arg_it);

  // Then: original function's captures (from enclosing scope)
  for (auto &cap_name : cf.capture_names) {
    auto cap_it = named_values_.find(cap_name);
    if (cap_it != named_values_.end())
      inner_call_args.push_back(cap_it->second.val);
  }

  auto *result = builder_->CreateCall(cf.fn, inner_call_args, "partial_call");
  Value *ret_val = result;
  if (ret_val->getType() != i64_ty) {
    if (ret_val->getType()->isPointerTy())
      ret_val = builder_->CreatePtrToInt(ret_val, i64_ty);
    else if (ret_val->getType()->isDoubleTy())
      ret_val = builder_->CreateBitCast(ret_val, i64_ty);
    else if (ret_val->getType()->isIntegerTy())
      ret_val = builder_->CreateZExtOrTrunc(ret_val, i64_ty);
  }
  builder_->CreateRet(ret_val);

  // Restore
  builder_->SetInsertPoint(saved_block, saved_point);

  Value *closure = builder_->CreateCall(
      rt_.closure_create_,
      {wrapper, ConstantInt::get(i64_ty, static_cast<int64_t>(cf.return_type)),
       ConstantInt::get(i64_ty, n_remaining),
       ConstantInt::get(i64_ty, n_provided)},
      wrapper_name + "_closure");
  const int64_t parameter_borrow_mask =
      borrow_mask(cf.borrowed_params, n_provided);
  if (parameter_borrow_mask != 0)
    builder_->CreateCall(
        rt_.closure_set_borrow_mask_,
        {closure, ConstantInt::get(i64_ty, parameter_borrow_mask)});
  int64_t heap_mask = 0;
  for (size_t i = 0; i < n_provided; i++) {
    Value *cap = all_args[i].val;
    Value *stored = cap;
    if (stored->getType()->isPointerTy())
      stored = builder_->CreatePtrToInt(stored, i64_ty);
    else if (stored->getType()->isIntegerTy() && stored->getType() != i64_ty)
      stored = builder_->CreateZExtOrTrunc(stored, i64_ty);
    builder_->CreateCall(rt_.closure_set_cap_,
                         {closure, ConstantInt::get(i64_ty, i), stored});
    if (is_heap_value(all_args[i])) {
      emit_rc_inc(cap, all_args[i].type);
      if (i < 64)
        heap_mask |= ((int64_t)1 << i);
    }
  }
  if (heap_mask != 0)
    builder_->CreateCall(rt_.closure_set_heap_mask_,
                         {closure, ConstantInt::get(i64_ty, heap_mask)});
  return {closure, CType::FUNCTION, {cf.return_type}};
}

TypedValue
Codegen::codegen_curry_apply(const std::string &fn_name, CompiledFunction &cf,
                             const std::vector<TypedValue> &all_args) {
  size_t func_arity = cf.param_types.size() - cf.capture_names.size();
  auto i64_ty = LType::getInt64Ty(*context_);
  auto ptr_ty = PointerType::get(*context_, 0);

  // Call with first func_arity args
  std::vector<Value *> first_args;
  for (size_t ai = 0; ai < func_arity; ai++) {
    Value *arg_val = all_args[ai].val;
    if (ai < cf.fn->arg_size()) {
      auto *expected_ty = cf.fn->getArg(ai)->getType();
      if (arg_val->getType() != expected_ty) {
        if (arg_val->getType()->isIntegerTy() && expected_ty->isPointerTy())
          arg_val = builder_->CreateIntToPtr(arg_val, expected_ty);
        else if (arg_val->getType()->isPointerTy() &&
                 expected_ty->isIntegerTy())
          arg_val = builder_->CreatePtrToInt(arg_val, expected_ty);
      }
    }
    first_args.push_back(arg_val);
  }
  for (auto &cap_name : cf.capture_names) {
    auto it = named_values_.find(cap_name);
    if (it != named_values_.end())
      first_args.push_back(it->second.val);
  }

  Value *current_fn = builder_->CreateCall(cf.fn, first_args, "curry_call");

  // Apply remaining args one at a time via closure convention
  for (size_t ai = func_arity; ai < all_args.size(); ai++) {
    // current_fn is a closure ptr (or i64 encoding of one)
    if (current_fn->getType()->isIntegerTy())
      current_fn = builder_->CreateIntToPtr(current_fn, ptr_ty);

    auto *fn_i64 = builder_->CreateLoad(i64_ty, current_fn, "curry_fn_i64");
    auto *fn_ptr = builder_->CreateIntToPtr(fn_i64, ptr_ty, "curry_fn_ptr");

    std::vector<LType *> arg_types = {ptr_ty};
    std::vector<Value *> call_vals = {current_fn};

    if (all_args[ai].type != CType::UNIT) {
      arg_types.push_back(all_args[ai].val->getType());
      call_vals.push_back(all_args[ai].val);
    }

    auto *call_type = llvm::FunctionType::get(i64_ty, arg_types, false);
    current_fn =
        builder_->CreateCall(call_type, fn_ptr, call_vals, "curry_apply");
  }

  // Final result is i64
  return {current_fn, CType::INT};
}

void Codegen::prepare_callee_owned_heap_args(
    const CompiledFunction &cf, const std::vector<TypedValue> &all_args) {
  // Perceus DUP for heap args. Standard rule: at non-last-use sites we
  // rc_inc so the consumed-by-callee ref doesn't pull the caller's
  // binding out from under us. Last-use (or single-use globally) sites
  // skip the inc and transfer the existing ref directly — this is what
  // unlocks the in-place tail fast path in the foldl/map/filter
  // recursion, where each recursive call passes the pattern-bound `t`
  // exactly once.
  for (size_t ai = 0; ai < all_args.size(); ai++) {
    CType ct = all_args[ai].type;
    CType callee_ct = (ai < cf.param_types.size()) ? cf.param_types[ai] : ct;
    // Result/Option payloads are typed INT in .yonai even when the
    // bits are a heap ADT pointer (e.g. `Ok j` from `Std\Json.parse`).
    // Honor the callee param type so a reused `j` is DUP'd.
    if (!is_heap_type(ct) && is_heap_type(callee_ct))
      ct = callee_ct;
    if (!all_args[ai].val || isa<Constant>(all_args[ai].val))
      continue;
    if (all_args[ai].val->getType()->isStructTy())
      continue;
    std::string named_as;
    for (auto &[k, v] : named_values_)
      if (v.val == all_args[ai].val) {
        named_as = k;
        break;
      }
    if (!is_heap_type(ct) && all_args[ai].boxed_heap) {
      if (named_as.empty())
        continue;
      int uses = current_fn_body_
                     ? count_identifier_refs(current_fn_body_, named_as)
                     : 2;
      if (uses > 1)
        emit_rc_inc(all_args[ai].val, CType::ADT);
      continue;
    }
    if (!is_heap_type(ct))
      continue;
    if (named_as.empty())
      continue; // anonymous → transfer (no inc)
    // Borrow inference: if the callee borrows this param, no rc_inc
    // needed — the caller retains ownership and the callee only reads.
    if (ai < cf.borrowed_params.size() && cf.borrowed_params[ai])
      continue;
    // For SEQ args, skip the inc when the binding has exactly one
    // textual occurrence in the enclosing function body — that
    // single use is also the last use, so we can transfer.
    if (ct == CType::SEQ && current_fn_body_) {
      int uses = count_identifier_refs(current_fn_body_, named_as);
      if (uses <= 1) {
        mark_transferred(all_args[ai].val, TransferDomain::Seq);
        emit_frame_transfer(all_args[ai].val);
        continue;
      }
    }
    // Same single-use check for SET/DICT under the callee-owns ABI
    // extended to user-defined calls. Last-use args are transferred
    // (no rc_inc); the callee's function-exit drop handles cleanup.
    if ((ct == CType::SET || ct == CType::DICT) && current_fn_body_) {
      int uses = count_identifier_refs(current_fn_body_, named_as);
      if (uses <= 1) {
        mark_transferred(all_args[ai].val, TransferDomain::Map);
        emit_frame_transfer(all_args[ai].val);
        continue;
      }
    }
    emit_rc_inc(all_args[ai].val, ct);
  }
}

void Codegen::cleanup_borrowed_temporary_args(
    const CompiledFunction &cf, const std::vector<TypedValue> &all_args) {
  for (size_t ai = 0; ai < all_args.size(); ai++) {
    if (ai >= cf.borrowed_params.size() || !cf.borrowed_params[ai])
      continue;
    CType ct = all_args[ai].type;
    if (!is_heap_type(ct))
      continue;
    if (!all_args[ai].val || isa<Constant>(all_args[ai].val))
      continue;
    if (all_args[ai].val->getType()->isStructTy())
      continue;

    bool is_named = false;
    for (auto &[k, v] : named_values_)
      if (v.val == all_args[ai].val) {
        is_named = true;
        break;
      }
    if (!is_named)
      emit_rc_dec(all_args[ai].val, ct);
  }
}

TypedValue Codegen::emit_direct_call(const std::string &fn_name,
                                     CompiledFunction &cf,
                                     const std::vector<TypedValue> &all_args) {
  prepare_callee_owned_heap_args(cf, all_args);

  std::vector<Value *> call_args;

  // For recursive closure calls, prepend the env pointer as first argument
  if (cf.closure_env)
    call_args.push_back(cf.closure_env);

  size_t fn_arg_offset = cf.closure_env ? 1 : 0;
  for (size_t ai = 0; ai < all_args.size(); ai++) {
    Value *arg_val = all_args[ai].val;
    // Coerce arg type if it doesn't match the function's expected param type.
    // This handles closures returning i64 when the callee expects ptr (ADT).
    if (ai + fn_arg_offset < cf.fn->arg_size()) {
      auto *expected_ty = cf.fn->getArg(ai + fn_arg_offset)->getType();
      if (arg_val->getType() != expected_ty) {
        if (arg_val->getType()->isIntegerTy() && expected_ty->isPointerTy())
          arg_val = builder_->CreateIntToPtr(arg_val, expected_ty);
        else if (arg_val->getType()->isPointerTy() &&
                 expected_ty->isIntegerTy())
          arg_val = builder_->CreatePtrToInt(arg_val, expected_ty);
        else if (arg_val->getType()->isStructTy() &&
                 expected_ty->isIntegerTy()) {
          auto *st = llvm::cast<llvm::StructType>(arg_val->getType());
          auto i64_ty = LType::getInt64Ty(*context_);
          if (st->getNumElements() == 1) {
            arg_val = builder_->CreateExtractValue(arg_val, {0}, "adt_tag_arg");
            if (arg_val->getType() != expected_ty)
              arg_val = builder_->CreateZExtOrTrunc(arg_val, expected_ty);
          } else {
            auto *tag_val =
                builder_->CreateExtractValue(arg_val, {0}, "adt_tag_arg");
            if (tag_val->getType() != i64_ty)
              tag_val = builder_->CreateZExtOrTrunc(tag_val, i64_ty);
            auto *boxed = builder_->CreateCall(
                rt_.adt_alloc_,
                {tag_val, ConstantInt::get(i64_ty, st->getNumElements() - 1)},
                "adt_box_arg");
            int64_t heap_mask = 0;
            for (unsigned fi = 1; fi < st->getNumElements(); fi++) {
              auto *field =
                  builder_->CreateExtractValue(arg_val, {fi}, "adt_field_arg");
              if (field->getType() != i64_ty) {
                if (field->getType()->isPointerTy())
                  field = builder_->CreatePtrToInt(field, i64_ty);
                else if (field->getType()->isIntegerTy())
                  field = builder_->CreateZExtOrTrunc(field, i64_ty);
              }
              builder_->CreateCall(
                  rt_.adt_set_field_,
                  {boxed, ConstantInt::get(i64_ty, fi - 1), field});
              size_t subtype_index = fi - 1;
              if (subtype_index < all_args[ai].subtypes.size() &&
                  all_args[ai].subtypes[subtype_index] != CType::ADT &&
                  is_heap_type(all_args[ai].subtypes[subtype_index])) {
                if (fi <= 64)
                  heap_mask |= (int64_t{1} << subtype_index);
                emit_rc_inc(field, all_args[ai].subtypes[subtype_index]);
              }
            }
            if (heap_mask != 0)
              builder_->CreateCall(
                  rt_.adt_set_heap_mask_,
                  {boxed, ConstantInt::get(i64_ty, heap_mask)});
            arg_val = builder_->CreatePtrToInt(boxed, expected_ty);
          }
        }
      }
    }
    call_args.push_back(arg_val);
  }

  // Append capture values
  for (auto &cap_name : cf.capture_names) {
    auto it = named_values_.find(cap_name);
    if (it != named_values_.end())
      call_args.push_back(it->second.val);
  }

  const auto promiseResultType = [&]() {
    CType ResultType = cf.promise_inner_type;
    if (auto Found = named_values_.find(fn_name);
        Found != named_values_.end() && !Found->second.subtypes.empty())
      ResultType = Found->second.subtypes.front();
    if (cf.fn->getName() == "YonaStdTaskSpawn" && !all_args.empty() &&
        !all_args.front().subtypes.empty())
      ResultType = all_args.front().subtypes.front();
    return ResultType;
  };
  const auto resultDescriptor = [&](CType ResultType) -> Value * {
    const char *DescriptorName = is_heap_type(ResultType)
                                     ? "YonaRuntimeReferenceTypeDescriptor"
                                     : "YonaRuntimeUnmanagedTypeDescriptor";
    auto *Descriptor = module_->getGlobalVariable(DescriptorName, true);
    if (Descriptor == nullptr) {
      Descriptor = new GlobalVariable(*module_, LType::getInt8Ty(*context_),
                                      true, GlobalValue::ExternalLinkage,
                                      nullptr, DescriptorName);
    }
    return Descriptor;
  };

  // Native promise (NAT / extern native): C returns YonaTask* — call
  // directly; auto-await uses YonaRuntimeTaskAwait (same as thread-pool
  // promises).
  if (cf.return_type == CType::PROMISE &&
      cf.extern_promise == ast::ExternPromiseKind::NativePtr) {
    const CType inner_ret = promiseResultType();
    call_args.push_back(resultDescriptor(inner_ret));
    auto *result = builder_->CreateCall(cf.fn, call_args, "native_promise");
    return TypedValue{result, CType::PROMISE, {inner_ret}};
  }

  // io-async: call directly — function submits to io_uring and returns ID
  if (cf.return_type == CType::PROMISE &&
      cf.extern_promise == ast::ExternPromiseKind::IoUring) {
    const CType inner_ret = promiseResultType();
    // Call the function directly — it returns the uring ID as i64
    auto *result = builder_->CreateCall(cf.fn, call_args, "io_submit");
    TypedValue tv{result, CType::PROMISE, {inner_ret}};
    tv.promise_await = PromiseAwaitPath::IoUring;
    return tv;
  }

  // Thread-pool async (extern async): wrap in thread pool call → returns
  // PROMISE
  if (cf.return_type == CType::PROMISE) {
    const CType inner_ret = promiseResultType();
    Value *Descriptor = resultDescriptor(inner_ret);

    auto i64_ty = LType::getInt64Ty(*context_);
    auto ptr_ty = PointerType::get(*context_, 0);

    // Helper: coerce a value to i64 for the async runtime interface
    auto to_i64 = [&](Value *v) -> Value * {
      if (v->getType() == i64_ty)
        return v;
      if (v->getType()->isPointerTy())
        return builder_->CreatePtrToInt(v, i64_ty);
      if (v->getType()->isIntegerTy())
        return builder_->CreateZExtOrTrunc(v, i64_ty);
      if (v->getType()->isDoubleTy())
        return builder_->CreateBitCast(v, i64_ty);
      return v;
    };

    Value *promise;
    if (call_args.size() <= 1) {
      // 0 or 1 arg: submit the function with its result ownership descriptor.
      Value *arg = call_args.empty() ? ConstantInt::get(i64_ty, 0)
                                     : to_i64(call_args[0]);
      if (current_group_)
        promise = builder_->CreateCall(rt_.async_call_grouped_,
                                       {cf.fn, arg, Descriptor, current_group_},
                                       "async_call_g");
      else
        promise = builder_->CreateCall(rt_.async_call_,
                                       {cf.fn, arg, Descriptor}, "async_call");
    } else {
      // Multi-argument calls carry their values in an invocation-owned
      // context. The worker destroys the context on success, failure, or
      // cancellation, so overlapping calls never share mutable storage.
      const auto context_name =
          fn_name + "_async_context_" + std::to_string(lambda_counter_++);
      std::vector<LType *> context_fields;
      context_fields.reserve(call_args.size());
      for (Value *call_arg : call_args)
        context_fields.push_back(call_arg->getType());
      auto *context_type =
          StructType::create(*context_, context_fields, context_name);

      auto wrapper_type = llvm::FunctionType::get(i64_ty, {i64_ty}, false);
      auto *wrapper_fn =
          Function::Create(wrapper_type, Function::InternalLinkage,
                           context_name + "_invoke", module_);

      auto saved_block = builder_->GetInsertBlock();
      auto saved_point = builder_->GetInsertPoint();

      auto *wrapper_entry = BasicBlock::Create(*context_, "entry", wrapper_fn);
      builder_->SetInsertPoint(wrapper_entry);
      Value *wrapper_context = builder_->CreateIntToPtr(
          wrapper_fn->getArg(0), ptr_ty, "async_context");

      std::vector<Value *> wrapper_call_args;
      wrapper_call_args.reserve(call_args.size());
      for (size_t ai = 0; ai < call_args.size(); ai++) {
        Value *field =
            builder_->CreateStructGEP(context_type, wrapper_context, ai,
                                      "async_arg_ptr" + std::to_string(ai));
        wrapper_call_args.push_back(builder_->CreateLoad(
            call_args[ai]->getType(), field, "async_arg" + std::to_string(ai)));
      }

      auto *wrapper_result =
          builder_->CreateCall(cf.fn, wrapper_call_args, "async_invoke");
      builder_->CreateRet(to_i64(wrapper_result));

      builder_->SetInsertPoint(saved_block, saved_point);

      const uint64_t context_size =
          module_->getDataLayout().getTypeAllocSize(context_type);
      Value *context = builder_->CreateCall(
          rt_.async_context_alloc_, {ConstantInt::get(i64_ty, context_size)},
          "async_context");
      for (size_t ai = 0; ai < call_args.size(); ai++) {
        Value *field = builder_->CreateStructGEP(
            context_type, context, ai, "async_arg_ptr" + std::to_string(ai));
        builder_->CreateStore(call_args[ai], field);
      }

      if (current_group_)
        promise = builder_->CreateCall(
            rt_.async_call_context_grouped_,
            {wrapper_fn, context, Descriptor, current_group_},
            "async_context_g");
      else
        promise = builder_->CreateCall(rt_.async_call_context_,
                                       {wrapper_fn, context, Descriptor},
                                       "async_context_call");
    }
    return {promise, CType::PROMISE, {inner_ret}};
  }

  // TCO: for self-recursive tail calls, emit RC cleanup BEFORE the call
  // so LLVM's TailCallElimination can convert the call to a loop.
  auto *current_fn = builder_->GetInsertBlock()->getParent();
  bool is_self_recursive = (cf.fn == current_fn) && !tco_fn_name_.empty();

  if (is_self_recursive && !tco_cleanup_done_) {
    // Emit Perceus DROP for heap params that are NOT passed through.
    // Pass-through = same LLVM value in both the param and the call arg.
    auto ptr_ty = PointerType::get(*context_, 0);
    for (size_t pi = 0;
         pi < tco_param_names_.size() && pi < tco_param_ctypes_.size(); pi++) {
      CType ct = tco_param_ctypes_[pi];
      if (ct == CType::SEQ || !is_heap_type(ct))
        continue;
      if (pi < tco_borrowed_params_.size() && tco_borrowed_params_[pi])
        continue;
      if (pi >= current_fn->arg_size())
        continue;
      auto *param = current_fn->getArg((unsigned)pi);
      if (param->getType()->isStructTy())
        continue;

      // Check if this param is passed through unchanged
      bool is_pass_through = false;
      if (pi < call_args.size() && call_args[pi] == param)
        is_pass_through = true;
      // Skip if already consumed by a callee-owns extern op
      // (e.g., Dict.put / Set.insert transferred the param)
      if (is_transferred(param, TransferDomain::Map))
        continue;
      if (is_transferred(param, TransferDomain::Seq))
        continue;

      if (!is_pass_through) {
        emit_rc_dec(param, ct);
      }
    }
    tco_cleanup_done_ = true;
  }

  auto *call_inst = cf.fn->getReturnType()->isVoidTy()
                        ? builder_->CreateCall(cf.fn, call_args)
                        : builder_->CreateCall(cf.fn, call_args, "calltmp");
  cleanup_borrowed_temporary_args(cf, all_args);
  if (is_self_recursive)
    call_inst->setTailCall(true);
  else if (cf.fn == current_fn)
    call_inst->setTailCall(true);

  // Perceus-linear: user-defined callees are callee-owns for seq args.
  // For ANONYMOUS seq args (fresh expression results): ownership passes
  // cleanly to the callee — mark as transferred so outer scope cleanups
  // skip their rc_dec. For NAMED args we needed an rc_inc BEFORE the
  // call (otherwise the callee might free our only ref) — that's
  // handled in the precall_seq_dups loop above where args were prepared.
  for (size_t ai = 0; ai < all_args.size(); ai++) {
    CType ct = all_args[ai].type;
    if (ct != CType::SEQ && ct != CType::SET && ct != CType::DICT)
      continue;
    if (!all_args[ai].val || isa<Constant>(all_args[ai].val))
      continue;
    if (all_args[ai].val->getType()->isStructTy())
      continue;
    bool is_named = false;
    for (auto &[k, v] : named_values_)
      if (v.val == all_args[ai].val) {
        is_named = true;
        break;
      }
    if (!is_named) {
      if (ai < cf.borrowed_params.size() && cf.borrowed_params[ai])
        continue;
      if (ct == CType::SEQ)
        mark_transferred(all_args[ai].val, TransferDomain::Seq);
      else
        mark_transferred(all_args[ai].val, TransferDomain::Map);
      emit_frame_transfer(all_args[ai].val);
    }
  }

  Value *result_val = cf.fn->getReturnType()->isVoidTy()
                          ? static_cast<Value *>(ConstantInt::get(
                                LType::getInt64Ty(*context_), 0))
                          : static_cast<Value *>(call_inst);
  TypedValue result{result_val, cf.return_type};
  if (cf.return_type == CType::ADT && !cf.return_adt_name.empty())
    result.adt_type_name = cf.return_adt_name;
  if (!cf.return_subtypes.empty())
    result.subtypes = cf.return_subtypes;
  if (!cf.return_semantic_subtypes.empty())
    result.semantic_subtypes = cf.return_semantic_subtypes;
  if (auto identity = resolve_descriptor_identity(
          cf.return_type_descriptor, cf.param_type_descriptors, all_args)) {
    result.type = identity->type;
    result.adt_type_name = identity->adt_name;
    result.semantic_subtypes = identity->arguments;
    result.adt_semantic_arguments = identity->arguments;
    result.subtypes.clear();
    for (const auto &argument : identity->arguments) {
      result.subtypes.push_back(argument.type);
      result.adt_type_arguments.push_back(argument.type);
      result.adt_type_argument_names.push_back(argument.adt_name);
    }
    if (is_heap_type(result.type) && result.type != CType::TUPLE &&
        result.val->getType()->isIntegerTy())
      result.val = builder_->CreateIntToPtr(
          result.val, PointerType::get(*context_, 0), "generic_return_ptr");
    else if (result.type == CType::FLOAT &&
             result.val->getType()->isIntegerTy(64))
      result.val = builder_->CreateBitCast(
          result.val, LType::getDoubleTy(*context_), "generic_return_float");
    else if (result.type == CType::BOOL &&
             !result.val->getType()->isIntegerTy(1))
      result.val = builder_->CreateICmpNE(
          result.val, ConstantInt::get(result.val->getType(), 0),
          "generic_return_bool");
  }
  return result;
}

// ===== codegen_apply — main dispatcher =====

TypedValue Codegen::codegen_apply(ApplyExpr *node) {
  set_debug_loc(node->Range);

  // 1. Flatten juxtaposition chain: f x y → collect all args and root name
  auto [fn_name, module_fqn, chain] = flatten_apply_chain(node);
  ExprNode *expression_callee = nullptr;
  if (fn_name.empty() && !chain.empty()) {
    if (auto *call = dynamic_cast<ExprCall *>(chain.back()->call))
      expression_callee = call->expr;
  }

  if (accelerator_lowering_enabled_) {
    if (auto plan = match_transparent_apply(node))
      return emit_accelerator_kernel(*plan);
    if (strict_accelerator_ && is_unlowerable_column_apply(node)) {
      Session->recordError();
      Session->diagnostics().error(
          node->Range, ErrorCode::E0700,
          "accelerator lambda is not in the fixed Std\\Gpu kernel library "
          "(arbitrary lambdas are not compiled to SPIR-V; use a fixed shape "
          "or drop --strict-accelerator for the host path)");
      return {};
    }
  }

  // 2. Evaluate all arguments
  auto eval = evaluate_apply_args(chain);
  auto &all_args = eval.all_args;
  if (all_args.empty() && !chain.empty() && !chain.back()->args.empty())
    return {}; // evaluation failed (signalled by cleared all_args)

  // 3. Pre-compile deferred lambda args and wrap Function* in closures
  precompile_function_args(eval, fn_name);
  wrap_function_args_in_closures(all_args);

  if (expression_callee) {
    // A branch that yields a local function must materialize each
    // deferred alternative before codegen_if creates its PHI. Compiling
    // an alternative while emitting the branch can temporarily move the
    // builder into the callee and leave cross-function branch targets.
    std::function<void(AstNode *)> materialize_function_values;
    materialize_function_values = [&](AstNode *expression) {
      if (!expression)
        return;
      if (expression->get_type() == ast::AST_IDENTIFIER_EXPR) {
        const auto name =
            static_cast<IdentifierExpr *>(expression)->name->value;
        auto deferred = deferred_functions_.find(name);
        if (deferred == deferred_functions_.end())
          return;
        auto compiled = compile_function(name, deferred->second, all_args);
        named_values_[name] = {
            wrap_in_closure(compiled.fn, compiled.return_type),
            CType::FUNCTION,
            {compiled.return_type}};
        return;
      }
      if (expression->get_type() == ast::AST_IF_EXPR) {
        auto *conditional = static_cast<IfExpr *>(expression);
        materialize_function_values(conditional->thenExpr);
        materialize_function_values(conditional->elseExpr);
      }
    };
    materialize_function_values(expression_callee);

    last_lambda_name_.clear();
    TypedValue callee_value = codegen(expression_callee);
    const std::string callee_lambda_name = last_lambda_name_;
    if (callee_value.type != CType::FUNCTION)
      return {};
    CType return_type = callee_value.subtypes.empty()
                            ? CType::INT
                            : callee_value.subtypes.front();
    if (!callee_value.val && !callee_lambda_name.empty()) {
      auto deferred = deferred_functions_.find(callee_lambda_name);
      if (deferred == deferred_functions_.end())
        return {};
      auto compiled =
          compile_function(callee_lambda_name, deferred->second, all_args);
      return_type = compiled.return_type;
      callee_value = {wrap_in_closure(compiled.fn, return_type),
                      CType::FUNCTION,
                      {return_type}};
    } else if (callee_value.val && isa<Function>(callee_value.val)) {
      callee_value.val =
          wrap_in_closure(cast<Function>(callee_value.val), return_type);
    }
    if (!callee_value.val)
      return {};

    const std::string temporary_name = "__expression_callee";
    auto previous = named_values_.find(temporary_name);
    std::optional<TypedValue> saved;
    std::optional<ActiveTypedValueSnapshot> saved_snapshot;
    if (previous != named_values_.end()) {
      saved = previous->second;
      saved_snapshot.emplace(*this, *saved);
    }
    named_values_[temporary_name] = callee_value;
    auto result = codegen_higher_order_call(temporary_name, all_args);
    // A literal lambda allocates its closure for this application. Values
    // selected from bindings remain owned by their lexical scope.
    if (expression_callee->get_type() == ast::AST_FUNCTION_EXPR)
      emit_rc_dec(callee_value.val, CType::FUNCTION);
    if (saved)
      named_values_[temporary_name] = *saved;
    else
      named_values_.erase(temporary_name);
    return result;
  }

  // 4. Check if it's an ADT constructor call
  auto adt_it = types_.adt_constructors.find(fn_name);
  if (adt_it != types_.adt_constructors.end() && adt_it->second.arity > 0)
    return codegen_adt_construct(fn_name, all_args);

  // 4b. Compile-time intrinsic: typeOf x → Type ADT constructor based on
  // argument's CType
  if (fn_name == "typeOf" && all_args.size() == 1) {
    std::string ctor_name;
    switch (all_args[0].type) {
    case CType::INT:
      ctor_name = "TypeInt";
      break;
    case CType::FLOAT:
      ctor_name = "TypeFloat";
      break;
    case CType::BOOL:
      ctor_name = "TypeBool";
      break;
    case CType::STRING:
      ctor_name = "TypeString";
      break;
    case CType::SYMBOL:
      ctor_name = "TypeSymbol";
      break;
    case CType::UNIT:
      ctor_name = "TypeUnit";
      break;
    case CType::SEQ:
      ctor_name = "TypeSeq";
      break;
    case CType::SET:
      ctor_name = "TypeSet";
      break;
    case CType::DICT:
      ctor_name = "TypeDict";
      break;
    case CType::TUPLE:
      ctor_name = "TypeTuple";
      break;
    case CType::FUNCTION:
      ctor_name = "TypeFunction";
      break;
    case CType::PROMISE:
      ctor_name = "TypePromise";
      break;
    case CType::BYTE_ARRAY:
      ctor_name = "TypeByteArray";
      break;
    case CType::INT_ARRAY:
      ctor_name = "TypeIntArray";
      break;
    case CType::FLOAT_ARRAY:
      ctor_name = "TypeFloatArray";
      break;
    case CType::SUM:
      ctor_name = "TypeSum";
      break;
    case CType::RECORD:
      ctor_name = "TypeRecord";
      break;
    case CType::CHANNEL: {
      ctor_name = "TypeAdt";
      auto *str = builder_->CreateGlobalString("Channel", "type_adt_name");
      return codegen_adt_construct("TypeAdt", {{str, CType::STRING}});
    }
    case CType::ADT: {
      // TypeAdt String — construct with the ADT type name as a String field
      ctor_name = "TypeAdt";
      std::string adt_name = !all_args[0].adt_type_name.empty()
                                 ? all_args[0].adt_type_name
                                 : "Unknown";
      auto *str = builder_->CreateGlobalString(adt_name, "type_adt_name");
      std::vector<TypedValue> ctor_args;
      ctor_args.push_back({str, CType::STRING});
      return codegen_adt_construct("TypeAdt", ctor_args);
    }
    }
    // Zero-arity Type constructor — use codegen_adt_construct with no args
    return codegen_adt_construct(ctor_name, {});
  }

  const auto imported_channel = imports_.extern_functions.find(fn_name);
  const bool is_channel_constructor =
      all_args.size() == 1 &&
      (module_fqn == "Std\\Channel" ||
       (imported_channel != imports_.extern_functions.end() &&
        imported_channel->second == "YonaStdChannelChannel"));
  if (is_channel_constructor) {
    if (!type_checker_ || genfn_isolation_depth_ != 0) {
      report_error(node->Range,
                   "channel payload type is unavailable during lowering");
      return {};
    }
    auto *payload_type =
        find_channel_payload_type(*type_checker_, type_checker_->type_of(node));
    const auto payload_hint =
        checked_lambda_argument_hint(*type_checker_, payload_type);
    if (!payload_hint.known) {
      report_error(node->Range,
                   "channel payload type must be concrete at creation");
      return {};
    }

    const bool payload_is_heap = is_heap_type(payload_hint.type);
    const char *descriptor_name = payload_is_heap
                                      ? "YonaRuntimeReferenceTypeDescriptor"
                                      : "YonaRuntimeUnmanagedTypeDescriptor";
    auto *descriptor = module_->getGlobalVariable(descriptor_name, true);
    if (!descriptor) {
      descriptor = new GlobalVariable(*module_, LType::getInt8Ty(*context_),
                                      true, GlobalValue::ExternalLinkage,
                                      nullptr, descriptor_name);
    }

    Value *tuple = builder_->CreateCall(
        rt_.channel_new_, {all_args.front().val, descriptor}, "channel_create");
    SemanticTypeIdentity payload_identity{payload_hint.type,
                                          payload_hint.adt_type_name,
                                          payload_hint.semantic_arguments};
    SemanticTypeIdentity sender{CType::ADT, "Sender", {payload_identity}};
    SemanticTypeIdentity receiver{CType::ADT, "Receiver", {payload_identity}};
    TypedValue result{tuple, CType::TUPLE, {CType::ADT, CType::ADT}};
    result.semantic_subtypes = {{CType::ADT, "Linear", {sender}},
                                {CType::ADT, "Linear", {receiver}}};
    return result;
  }

  // Explicit imports shadow trait-method fallback. This matters for names
  // like `close`, where Std\Channel.close must not resolve to Closeable Int.
  if (auto imported = imports_.extern_functions.find(fn_name);
      imported != imports_.extern_functions.end()) {
    const bool has_source = imports_.imported_sources.count(imported->second);
    const bool active_self_call = [&] {
      auto compiled = compiled_functions_.find(fn_name);
      return compiled != compiled_functions_.end() && compiled->second.fn &&
             builder_->GetInsertBlock() &&
             builder_->GetInsertBlock()->getParent() == compiled->second.fn;
    }();
    if (has_source && !active_self_call)
      return codegen_extern_call(node, fn_name, all_args);
    if (!has_source &&
        compiled_functions_.find(fn_name) == compiled_functions_.end() &&
        deferred_functions_.find(fn_name) == deferred_functions_.end())
      return codegen_extern_call(node, fn_name, all_args);
  }

  // 5. Resolve the function (compiled, deferred, or trait method)
  auto cf_it = resolve_apply_function(fn_name, all_args, node);

  // 6. If not found as compiled function, try higher-order, extern, or raw LLVM
  if (cf_it == compiled_functions_.end()) {
    // Higher-order call (FUNCTION-typed variable)
    auto var_it = named_values_.find(fn_name);
    if (var_it != named_values_.end() &&
        var_it->second.type == CType::FUNCTION && var_it->second.val)
      return codegen_higher_order_call(fn_name, all_args);

    // Imported extern function
    auto ext_it = imports_.extern_functions.find(fn_name);
    if (ext_it != imports_.extern_functions.end())
      return codegen_extern_call(node, fn_name, all_args);

    // Try as an LLVM function in the module
    auto *fn = module_->getFunction(fn_name);
    if (!fn) {
      std::string msg = "undefined function '" + fn_name + "'";
      auto suggestion = suggest_similar(fn_name);
      if (!suggestion.empty())
        msg += "; did you mean '" + suggestion + "'?";
      report_error(node->Range, msg);
      return {};
    }
    std::vector<Value *> vals;
    for (auto &a : all_args)
      vals.push_back(a.val);
    return {builder_->CreateCall(fn, vals), CType::INT};
  }

  auto &cf = cf_it->second;
  size_t func_arity = cf.param_types.size() - cf.capture_names.size();

  // If the callee is a closure (has closure_env) and closure_env is an
  // Argument of a DIFFERENT function than the one we're currently emitting
  // into, we can't emit a direct call — the env Argument isn't in scope.
  // This happens when a recursive let-bound closure `pull` is referenced
  // from a nested lambda (e.g. `\_ -> pull ()`); the lambda captures `pull`
  // as a closure and must call through it via higher_order_call, which
  // loads the closure fn+env from the lambda's own captures.
  if (cf.closure_env) {
    if (auto *env_arg = dyn_cast<Argument>(cf.closure_env)) {
      auto *current_fn = builder_->GetInsertBlock()->getParent();
      if (env_arg->getParent() != current_fn) {
        auto var_it = named_values_.find(fn_name);
        if (var_it != named_values_.end() &&
            var_it->second.type == CType::FUNCTION && var_it->second.val)
          return codegen_higher_order_call(fn_name, all_args);
      }
    }
  }

  // 7. Dispatch based on arg count vs arity.
  // `f ()` on a 0-arity function: drop the synthetic Unit arg — the parser
  // inserts one for every `f()` paren-call form, but 0-arity callees
  // (CAFs like `uuid4`, `now`) don't want it. A 1-arity function whose
  // parameter happens to be unit-typed keeps the arg and receives ().
  if (func_arity == 0 && all_args.size() == 1 &&
      all_args[0].type == CType::UNIT)
    all_args.clear();
  if (all_args.size() < func_arity)
    return codegen_partial_apply(fn_name, cf, all_args);
  if (all_args.size() > func_arity)
    return codegen_curry_apply(fn_name, cf, all_args);

  // 8. Exact arity: emit the direct call
  return emit_direct_call(fn_name, cf, all_args);
}

// ===== Closure wrapping =====

Value *Codegen::wrap_in_closure(Function *fn, CType ret_type) {
  auto ptr_ty = PointerType::get(*context_, 0);
  auto i64_ty = LType::getInt64Ty(*context_);

  // Check if wrapper already exists
  std::string wrapper_name = fn->getName().str() + "_env";
  auto *existing = module_->getFunction(wrapper_name);
  if (!existing) {
    // Build wrapper: fn_env(ptr env, original_args...) -> i64
    std::vector<LType *> wrapper_params = {ptr_ty};
    for (auto &arg : fn->args())
      wrapper_params.push_back(arg.getType());

    auto *wrapper_type = llvm::FunctionType::get(i64_ty, wrapper_params, false);
    existing = Function::Create(wrapper_type, Function::InternalLinkage,
                                wrapper_name, module_);

    auto saved_block = builder_->GetInsertBlock();
    auto saved_point = builder_->GetInsertPoint();

    auto *entry = BasicBlock::Create(*context_, "entry", existing);
    builder_->SetInsertPoint(entry);

    // Forward args (skip env)
    auto arg_it = existing->arg_begin();
    arg_it->setName("env");
    ++arg_it;
    std::vector<Value *> forward_args;
    for (; arg_it != existing->arg_end(); ++arg_it)
      forward_args.push_back(&*arg_it);

    auto *result = builder_->CreateCall(fn, forward_args, "fwd");

    // Coerce return to i64 (universal closure return)
    Value *ret_val = result;
    if (ret_val->getType() != i64_ty) {
      if (ret_val->getType()->isPointerTy())
        ret_val = builder_->CreatePtrToInt(ret_val, i64_ty);
      else if (ret_val->getType()->isDoubleTy())
        ret_val = builder_->CreateBitCast(ret_val, i64_ty);
      else if (ret_val->getType()->isIntegerTy())
        ret_val = builder_->CreateZExtOrTrunc(ret_val, i64_ty);
      else if (ret_val->getType()->isStructTy()) {
        // Non-recursive ADT struct: heap-allocate and return as i64 pointer
        auto *sty = llvm::cast<llvm::StructType>(ret_val->getType());
        unsigned nf = sty->getNumElements();
        auto *tag_val = builder_->CreateExtractValue(ret_val, {0});
        auto *adt_ptr = builder_->CreateCall(
            rt_.adt_alloc_, {tag_val, ConstantInt::get(i64_ty, nf - 1)});
        for (unsigned fi = 1; fi < nf; fi++) {
          auto *fv = builder_->CreateExtractValue(ret_val, {fi});
          builder_->CreateCall(rt_.adt_set_field_,
                               {adt_ptr, ConstantInt::get(i64_ty, fi - 1), fv});
        }
        ret_val = builder_->CreatePtrToInt(adt_ptr, i64_ty);
      }
    }
    builder_->CreateRet(ret_val);

    if (saved_block)
      builder_->SetInsertPoint(saved_block, saved_point);
  }

  // Create trivial closure {wrapper_fn_ptr, ret_tag, arity, <no captures>}
  int64_t arity =
      fn->arg_size(); // user args (wrapper has env + original params)
  auto *closure = builder_->CreateCall(
      rt_.closure_create_,
      {existing, ConstantInt::get(i64_ty, static_cast<int64_t>(ret_type)),
       ConstantInt::get(i64_ty, arity), ConstantInt::get(i64_ty, 0)},
      wrapper_name + "_closure");
  const auto cf = compiled_functions_.find(fn->getName().str());
  if (cf != compiled_functions_.end()) {
    const int64_t parameter_borrow_mask =
        borrow_mask(cf->second.borrowed_params);
    if (parameter_borrow_mask != 0)
      builder_->CreateCall(
          rt_.closure_set_borrow_mask_,
          {closure, ConstantInt::get(i64_ty, parameter_borrow_mask)});
  }
  return closure;
}

TypedValue Codegen::auto_await(TypedValue tv) {
  if (!tv || tv.type != CType::PROMISE)
    return tv;

  // Dispatch: io_uring cookie vs opaque promise pointer (pool or extern native)
  Value *awaited;
  if (tv.promise_await == PromiseAwaitPath::IoUring) {
    awaited = builder_->CreateCall(rt_.io_await_, {tv.val}, "io_await");
  } else {
    llvm::Function *await_fn =
        current_group_ ? rt_.async_await_keep_ : rt_.async_await_;
    awaited = builder_->CreateCall(await_fn, {tv.val}, "await");
  }

  // The awaited value's type is stored in subtypes[0]
  CType inner = (!tv.subtypes.empty()) ? tv.subtypes[0] : CType::INT;
  // The await returns i64 — coerce to the actual type
  Value *result = awaited;
  auto *expected = llvm_type(inner);
  if (expected->isPointerTy())
    result = builder_->CreateIntToPtr(awaited, expected);
  else if (expected == LType::getInt1Ty(*context_))
    result = builder_->CreateTrunc(awaited, expected);
  else if (expected->isDoubleTy())
    result = builder_->CreateBitCast(awaited, expected);
  return {result, inner};
}

// ===== Local static helpers for type annotations =====

static CType yona_type_to_ctype(const types::Type &t) {
  if (std::holds_alternative<types::BuiltinType>(t)) {
    switch (std::get<types::BuiltinType>(t)) {
    case types::SignedInt64:
    case types::SignedInt32:
    case types::SignedInt16:
    case types::SignedInt128:
    case types::UnsignedInt64:
    case types::UnsignedInt32:
    case types::UnsignedInt16:
    case types::UnsignedInt128:
      return CType::INT;
    case types::Float64:
    case types::Float32:
    case types::Float128:
      return CType::FLOAT;
    case types::Bool:
      return CType::BOOL;
    case types::String:
      return CType::STRING;
    case types::Symbol:
      return CType::SYMBOL;
    case types::Unit:
      return CType::UNIT;
    case types::Seq:
      return CType::SEQ;
    case types::Set:
      return CType::SET;
    case types::Dict:
      return CType::DICT;
    default:
      return CType::INT;
    }
  }
  if (std::holds_alternative<std::shared_ptr<types::FunctionType>>(t))
    return CType::FUNCTION;
  if (std::holds_alternative<std::shared_ptr<types::SingleItemCollectionType>>(
          t)) {
    auto &col = std::get<std::shared_ptr<types::SingleItemCollectionType>>(t);
    return (col->kind == types::SingleItemCollectionType::Seq) ? CType::SEQ
                                                               : CType::SET;
  }
  if (std::holds_alternative<std::shared_ptr<types::DictCollectionType>>(t))
    return CType::DICT;
  if (std::holds_alternative<std::shared_ptr<types::ProductType>>(t))
    return CType::TUPLE;
  if (std::holds_alternative<std::shared_ptr<types::NamedType>>(t)) {
    auto &nt = std::get<std::shared_ptr<types::NamedType>>(t);
    if (nt->name == "Channel")
      return CType::CHANNEL;
    if (nt->name == "FloatArray")
      return CType::FLOAT_ARRAY;
    if (nt->name == "IntArray")
      return CType::INT_ARRAY;
    if (nt->name == "ByteArray")
      return CType::BYTE_ARRAY;
    return CType::ADT;
  }
  if (std::holds_alternative<std::shared_ptr<types::PromiseType>>(t))
    return CType::PROMISE;
  if (std::holds_alternative<std::shared_ptr<types::SumType>>(t))
    return CType::SUM;
  if (std::holds_alternative<std::shared_ptr<types::RefinedType>>(t))
    return yona_type_to_ctype(
        std::get<std::shared_ptr<types::RefinedType>>(t)->base_type);
  return CType::INT;
}

static std::pair<std::vector<CType>, CType>
uncurry_type_signature(const types::Type &t) {
  std::vector<CType> params;
  const types::Type *current = &t;
  while (
      std::holds_alternative<std::shared_ptr<types::FunctionType>>(*current)) {
    auto &ft = std::get<std::shared_ptr<types::FunctionType>>(*current);
    params.push_back(yona_type_to_ctype(ft->argumentType));
    current = &ft->returnType;
  }
  return {params, yona_type_to_ctype(*current)};
}

} // namespace yona::compiler::codegen
