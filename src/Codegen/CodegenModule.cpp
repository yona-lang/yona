//
// Codegen â€” Module system code generation
//
// Interface file I/O, FQN resolution, imports, extern declarations.
//

#include "yona/Codegen/Codegen.h"
#include "yona/Interface/Reader.h"
#include "yona/Interface/Writer.h"
#include "yona/Model/ModuleIdentity.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Type.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <unordered_set>

namespace yona::compiler::codegen {
using llvm::BasicBlock;
using llvm::ConstantFP;
using llvm::ConstantInt;
using llvm::ConstantPointerNull;
using llvm::Function;
using llvm::FunctionType;
using llvm::GlobalValue;
using llvm::PointerType;
using llvm::Value;
using LType = llvm::Type;

// Forward declarations for type annotation support
static CType yona_type_to_ctype(const types::Type &t);
static std::string yona_type_adt_name(const types::Type &t);
static std::pair<std::vector<CType>, CType>
uncurry_type_signature(const types::Type &t);
static std::string ctype_to_string(CType ct);
static std::string constructor_field_descriptor(const ast::FieldType &Field);
static ast::FieldType field_type_from_descriptor(std::string_view Descriptor);

static CType string_to_ctype(const std::string &s);

/// Compact recursive type grammar used by `.yonai` function signatures.
/// Scalar tags and wrappers use one canonical grammar: `NAME(payload)`, e.g.
/// `LINEAR(ADT(FileHandle))`. The C ABI still uses CType separately.
static std::string interface_type(CType type,
                                  const std::string &adt_name = {}) {
  if (type == CType::ADT && !adt_name.empty())
    return "ADT(" + adt_name + ")";
  return ctype_to_string(type);
}

static void parse_interface_type(const std::string &text, CType &type,
                                 std::string &adt_name, bool &linear) {
  linear = false;
  std::string inner = text;
  if (inner.starts_with("LINEAR(") && inner.ends_with(')')) {
    linear = true;
    inner = inner.substr(7, inner.size() - 8);
  }
  if (inner.starts_with("ADT(") && inner.ends_with(')')) {
    type = CType::ADT;
    const auto body = inner.substr(4, inner.size() - 5);
    const auto comma = body.find(',');
    adt_name = body.substr(0, comma);
  } else if (inner.starts_with("Seq(") && inner.ends_with(')')) {
    type = CType::SEQ;
  } else if (inner.starts_with("Set(") && inner.ends_with(')')) {
    type = CType::SET;
  } else if (inner.starts_with("Dict(") && inner.ends_with(')')) {
    type = CType::DICT;
  } else if (inner.starts_with("FUNCTION(") && inner.ends_with(')')) {
    type = CType::FUNCTION;
  } else if (inner.starts_with("TUPLE(") && inner.ends_with(')')) {
    type = CType::TUPLE;
  } else if (inner.starts_with("Promise(") && inner.ends_with(')')) {
    type = CType::PROMISE;
  } else {
    type = string_to_ctype(inner);
  }
}

static std::string trim_trailing_doc_comments(std::string source) {
  while (!source.empty() && (source.back() == '\n' || source.back() == '\r' ||
                             source.back() == ' ' || source.back() == '\t'))
    source.pop_back();

  while (!source.empty()) {
    size_t line_start = source.find_last_of("\r\n");
    line_start = (line_start == std::string::npos) ? 0 : line_start + 1;
    size_t first = source.find_first_not_of(" \t", line_start);
    bool is_doc_line = first != std::string::npos &&
                       first + 1 < source.size() && source[first] == '#' &&
                       source[first + 1] == '#';
    if (!is_doc_line)
      break;

    source.erase(line_start);
    while (!source.empty() && (source.back() == '\n' || source.back() == '\r' ||
                               source.back() == ' ' || source.back() == '\t'))
      source.pop_back();
  }

  return source;
}

static bool contains_identifier(const std::string &source,
                                const std::string &identifier) {
  size_t pos = source.find(identifier);
  while (pos != std::string::npos) {
    const bool left_boundary =
        pos == 0 ||
        (!std::isalnum(static_cast<unsigned char>(source[pos - 1])) &&
         source[pos - 1] != '_');
    const size_t end = pos + identifier.size();
    const bool right_boundary =
        end == source.size() ||
        (!std::isalnum(static_cast<unsigned char>(source[end])) &&
         source[end] != '_');
    if (left_boundary && right_boundary)
      return true;
    pos = source.find(identifier, pos + 1);
  }
  return false;
}

std::string Codegen::ctype_to_type_name(CType ct) {
  switch (ct) {
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
  case CType::SEQ:
    return "Seq";
  case CType::SET:
    return "Set";
  case CType::DICT:
    return "Dict";
  case CType::TUPLE:
    return "Tuple";
  case CType::UNIT:
    return "Unit";
  case CType::FUNCTION:
    return "Function";
  case CType::PROMISE:
    return "Promise";
  case CType::ADT:
    return "ADT";
  case CType::BYTE_ARRAY:
    return "ByteArray";
  case CType::INT_ARRAY:
    return "IntArray";
  case CType::FLOAT_ARRAY:
    return "FloatArray";
  case CType::CHANNEL:
    return "Channel";
  case CType::SUM:
    return "Sum";
  case CType::RECORD:
    return "Record";
  }
  return "Int";
}

std::string Codegen::resolve_trait_method(const std::string &method_name,
                                          CType arg_type,
                                          const std::string &adt_type_name,
                                          const std::string &requested_trait) {
  std::string type_name = ctype_to_type_name(arg_type);

  // Phase 2: For ADT types, use the specific ADT type name instead of generic
  // "ADT"
  if (arg_type == CType::ADT && !adt_type_name.empty()) {
    type_name = adt_type_name;
  }

  auto lookup = [&](const std::string &trait_name) -> std::string {
    const auto instance =
        types_.trait_instances.find(trait_name + ":" + type_name);
    if (instance != types_.trait_instances.end()) {
      const auto method =
          instance->second.method_mangled_names.find(method_name);
      if (method != instance->second.method_mangled_names.end())
        return method->second;
    }

    // Multi-parameter and lifted heads are keyed with all declared head
    // arguments (`Foldable:Seq:element`). Receiver-only application can
    // select them when exactly one visible contract has this concrete
    // first head; ambiguous conversions remain unresolved until their
    // target witness supplies the remaining arguments.
    std::vector<std::string> candidates;
    for (const auto &[_, candidate] : types_.trait_instances) {
      if (candidate.trait_name != trait_name || candidate.type_names.empty() ||
          candidate.type_names.front() != type_name)
        continue;
      const auto method = candidate.method_mangled_names.find(method_name);
      if (method != candidate.method_mangled_names.end())
        candidates.push_back(method->second);
    }
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()),
                     candidates.end());
    return candidates.size() == 1 ? candidates.front() : std::string{};
  };

  if (!requested_trait.empty())
    return lookup(requested_trait);

  // Unqualified method calls are accepted only when exactly one visible
  // trait both declares the method and has an exact receiver instance.
  // Sort names so selection and ambiguity diagnostics never depend on
  // unordered-map iteration order.
  std::vector<std::string> trait_names;
  for (const auto &[trait_name, trait] : types_.traits)
    if (std::find(trait.method_names.begin(), trait.method_names.end(),
                  method_name) != trait.method_names.end())
      trait_names.push_back(trait_name);
  std::sort(trait_names.begin(), trait_names.end());
  std::string selected;
  for (const auto &trait_name : trait_names) {
    auto candidate = lookup(trait_name);
    if (candidate.empty())
      continue;
    if (!selected.empty())
      return {}; // ambiguous; type checking diagnoses it
    selected = std::move(candidate);
  }
  return selected;
}

static std::string ctype_to_string(CType ct) {
  switch (ct) {
  case CType::INT:
    return "INT";
  case CType::FLOAT:
    return "FLOAT";
  case CType::BOOL:
    return "BOOL";
  case CType::STRING:
    return "STRING";
  case CType::SEQ:
    return "SEQ";
  case CType::TUPLE:
    return "TUPLE";
  case CType::UNIT:
    return "UNIT";
  case CType::FUNCTION:
    return "FUNCTION";
  case CType::SYMBOL:
    return "SYMBOL";
  case CType::PROMISE:
    return "PROMISE";
  case CType::SET:
    return "SET";
  case CType::DICT:
    return "DICT";
  case CType::ADT:
    return "ADT";
  case CType::BYTE_ARRAY:
    return "BYTE_ARRAY";
  case CType::INT_ARRAY:
    return "INT_ARRAY";
  case CType::FLOAT_ARRAY:
    return "FLOAT_ARRAY";
  case CType::CHANNEL:
    return "CHANNEL";
  case CType::SUM:
    return "SUM";
  case CType::RECORD:
    return "RECORD";
  }
  return "INT";
}

static CType string_to_ctype(const std::string &s) {
  if (s == "INT")
    return CType::INT;
  if (s == "FLOAT")
    return CType::FLOAT;
  if (s == "BOOL")
    return CType::BOOL;
  if (s == "STRING")
    return CType::STRING;
  if (s == "SEQ")
    return CType::SEQ;
  if (s == "TUPLE")
    return CType::TUPLE;
  if (s == "UNIT")
    return CType::UNIT;
  if (s == "FUNCTION")
    return CType::FUNCTION;
  if (s == "SYMBOL")
    return CType::SYMBOL;
  if (s == "PROMISE")
    return CType::PROMISE;
  if (s == "SET")
    return CType::SET;
  if (s == "DICT")
    return CType::DICT;
  if (s == "ADT")
    return CType::ADT;
  if (s == "BYTE_ARRAY")
    return CType::BYTE_ARRAY;
  if (s == "INT_ARRAY")
    return CType::INT_ARRAY;
  if (s == "FLOAT_ARRAY")
    return CType::FLOAT_ARRAY;
  if (s == "CHANNEL")
    return CType::CHANNEL;
  if (s == "SUM")
    return CType::SUM;
  if (s == "RECORD")
    return CType::RECORD;
  return CType::INT;
}

Codegen::AdtInfo::FieldShape
Codegen::field_shape_from_field_type(const ast::FieldType &field_type) {
  auto ctype_for_name = [](std::string name) {
    auto space = name.find(' ');
    if (space != std::string::npos)
      name.resize(space);
    if (name == "Int" || name == "a" || name == "b" || name == "e" ||
        name == "s")
      return CType::INT;
    if (name == "Float")
      return CType::FLOAT;
    if (name == "String")
      return CType::STRING;
    if (name == "Bool")
      return CType::BOOL;
    if (name == "Symbol")
      return CType::SYMBOL;
    if (name == "Seq")
      return CType::SEQ;
    if (name == "Set")
      return CType::SET;
    if (name == "Dict")
      return CType::DICT;
    if (name == "Channel")
      return CType::CHANNEL;
    if (name == "()" || name == "Unit")
      return CType::UNIT;
    return CType::ADT;
  };

  AdtInfo::FieldShape shape;
  if (field_type.is_tuple_type) {
    shape.type = CType::TUPLE;
    for (const auto &element : field_type.tuple_types)
      shape.tuple_elements.push_back(field_shape_from_field_type(element));
    return shape;
  }
  if (field_type.is_function_type) {
    shape.type = CType::FUNCTION;
    if (!field_type.return_types.empty()) {
      auto result =
          field_shape_from_field_type(field_type.return_types.front());
      shape.function_return_type = result.type;
      if (result.type == CType::ADT)
        shape.function_return_adt_name = field_type.return_types.front().name;
    }
    return shape;
  }
  shape.type = ctype_for_name(field_type.name);
  return shape;
}

Codegen::ModuleFunctionMeta
Codegen::module_meta_from_compiled(const CompiledFunction &cf) const {
  ModuleFunctionMeta meta;
  meta.param_types = cf.param_types;
  for (size_t i = 0; i < cf.param_types.size(); ++i) {
    const std::string adt_name =
        i < cf.param_adt_names.size() ? cf.param_adt_names[i] : "";
    if (i < cf.param_type_descriptors.size() &&
        !cf.param_type_descriptors[i].empty()) {
      meta.param_type_descriptors.push_back(cf.param_type_descriptors[i]);
    } else {
      meta.param_type_descriptors.push_back(
          interface_type(cf.param_types[i], adt_name));
    }
  }
  meta.return_type = cf.return_type;
  meta.return_type_descriptor =
      !cf.return_type_descriptor.empty()
          ? cf.return_type_descriptor
          : interface_type(cf.return_type, cf.return_adt_name);
  meta.extern_promise = cf.extern_promise;
  meta.promise_inner_type = cf.promise_inner_type;
  meta.return_adt_name = cf.return_adt_name;
  meta.borrowed_params = cf.borrowed_params;
  meta.return_linear = cf.return_linear || cf.return_adt_name == "Linear";
  meta.tuple_elem_linear = cf.tuple_elem_linear;
  meta.param_linear = cf.param_linear;
  meta.effect_ops = cf.effect_ops;
  meta.effect_row_known = cf.effect_row_known;
  meta.effect_open_rest = cf.effect_open_rest;
  meta.effect_hof = cf.effect_hof;
  meta.effect_scheme = cf.effect_scheme;
  return meta;
}

interface::Function
Codegen::interfaceFunctionFromMeta(std::string Name,
                                   const ModuleFunctionMeta &Meta) const {
  interface::Function Result;
  Result.Name = std::move(Name);
  switch (Meta.extern_promise) {
  case ast::ExternPromiseKind::Sync:
    Result.Kind = interface::FunctionKind::Synchronous;
    break;
  case ast::ExternPromiseKind::ThreadPool:
    Result.Kind = interface::FunctionKind::ThreadPool;
    break;
  case ast::ExternPromiseKind::IoUring:
    Result.Kind = interface::FunctionKind::Io;
    break;
  case ast::ExternPromiseKind::NativePtr:
    Result.Kind = interface::FunctionKind::Native;
    break;
  }

  Result.ParameterTypes.reserve(Meta.param_types.size());
  for (std::size_t Index = 0; Index < Meta.param_types.size(); ++Index) {
    std::string Descriptor = Index < Meta.param_type_descriptors.size() &&
                                     !Meta.param_type_descriptors[Index].empty()
                                 ? Meta.param_type_descriptors[Index]
                                 : interface_type(Meta.param_types[Index]);
    if (Index < Meta.param_linear.size() && Meta.param_linear[Index] &&
        !Descriptor.starts_with("LINEAR("))
      Descriptor = "LINEAR(" + Descriptor + ")";
    Result.ParameterTypes.push_back(std::move(Descriptor));
  }

  const bool IsPromise = Meta.extern_promise != ast::ExternPromiseKind::Sync;
  const CType ReturnType =
      IsPromise ? Meta.promise_inner_type : Meta.return_type;
  std::string ReturnDescriptor =
      Meta.return_type_descriptor.empty()
          ? interface_type(ReturnType, Meta.return_adt_name)
          : Meta.return_type_descriptor;
  if (Meta.return_linear && !ReturnDescriptor.starts_with("LINEAR("))
    ReturnDescriptor = "LINEAR(" + ReturnDescriptor + ")";
  Result.ReturnType = std::move(ReturnDescriptor);

  Result.BorrowedParameters = Meta.borrowed_params;
  Result.BorrowedParameters.resize(Meta.param_types.size(), false);
  Result.TupleElementLinear.reserve(Meta.tuple_elem_linear.size());
  for (const char Linear : Meta.tuple_elem_linear)
    Result.TupleElementLinear.push_back(Linear != 0);
  Result.Effects.Operations = Meta.effect_ops;
  Result.Effects.Scheme = Meta.effect_scheme;
  Result.Effects.IsKnown = Meta.effect_row_known;
  Result.Effects.IsOpen = Meta.effect_open_rest;
  Result.Effects.IsHigherOrder = Meta.effect_hof;
  return Result;
}

Codegen::ModuleFunctionMeta
Codegen::moduleMetaFromInterface(const interface::Function &Function) const {
  ModuleFunctionMeta Result;
  switch (Function.Kind) {
  case interface::FunctionKind::Synchronous:
    Result.extern_promise = ast::ExternPromiseKind::Sync;
    break;
  case interface::FunctionKind::ThreadPool:
    Result.extern_promise = ast::ExternPromiseKind::ThreadPool;
    break;
  case interface::FunctionKind::Io:
    Result.extern_promise = ast::ExternPromiseKind::IoUring;
    break;
  case interface::FunctionKind::Native:
    Result.extern_promise = ast::ExternPromiseKind::NativePtr;
    break;
  }

  Result.param_linear.assign(Function.ParameterTypes.size(), 0);
  Result.param_type_descriptors = Function.ParameterTypes;
  for (std::size_t Index = 0; Index < Function.ParameterTypes.size(); ++Index) {
    CType Type = CType::INT;
    std::string AdtName;
    bool Linear = false;
    parse_interface_type(Function.ParameterTypes[Index], Type, AdtName, Linear);
    Result.param_types.push_back(Type);
    Result.param_linear[Index] = Linear ? 1 : 0;
  }

  CType ReturnType = CType::INT;
  bool ReturnLinear = false;
  parse_interface_type(Function.ReturnType, ReturnType, Result.return_adt_name,
                       ReturnLinear);
  Result.return_linear = ReturnLinear;
  Result.return_type_descriptor = Function.ReturnType;
  const bool IsPromise = Result.extern_promise != ast::ExternPromiseKind::Sync;
  Result.return_type = IsPromise ? CType::PROMISE : ReturnType;
  Result.promise_inner_type = IsPromise ? ReturnType : CType::INT;
  Result.borrowed_params = Function.BorrowedParameters;
  Result.borrowed_params.resize(Function.ParameterTypes.size(), false);
  Result.tuple_elem_linear.reserve(Function.TupleElementLinear.size());
  for (const bool Linear : Function.TupleElementLinear)
    Result.tuple_elem_linear.push_back(Linear ? 1 : 0);
  Result.effect_ops = Function.Effects.Operations;
  Result.effect_scheme = Function.Effects.Scheme;
  Result.effect_row_known = Function.Effects.IsKnown;
  Result.effect_open_rest = Function.Effects.IsOpen;
  Result.effect_hof = Function.Effects.IsHigherOrder;
  return Result;
}

Codegen::CompiledFunction
Codegen::compiled_function_from_meta(llvm::Function *fn,
                                     const ModuleFunctionMeta &meta,
                                     CType return_type) const {
  CompiledFunction cf;
  cf.fn = fn;
  cf.return_type = return_type;
  cf.param_types = meta.param_types;
  cf.param_type_descriptors = meta.param_type_descriptors;
  cf.return_type_descriptor = meta.return_type_descriptor;
  cf.borrowed_params = meta.borrowed_params;
  cf.extern_promise = meta.extern_promise;
  cf.promise_inner_type = meta.promise_inner_type;
  cf.return_adt_name = meta.return_adt_name;
  cf.return_linear = meta.return_linear || meta.return_adt_name == "Linear";
  cf.tuple_elem_linear = meta.tuple_elem_linear;
  cf.param_linear = meta.param_linear;
  cf.effect_ops = meta.effect_ops;
  cf.effect_row_known = meta.effect_row_known;
  cf.effect_open_rest = meta.effect_open_rest;
  cf.effect_hof = meta.effect_hof;
  cf.effect_scheme = meta.effect_scheme;
  return cf;
}

bool Codegen::emit_interface_file(const std::string &Path) {
  if (current_module_fqn_.empty())
    return false;

  interface::InterfaceModule Module{model::ModuleIdentity(current_module_fqn_)};

  std::map<std::string, std::vector<std::pair<std::string, const AdtInfo *>>>
      Adts;
  for (const auto &[Name, Info] : types_.adt_constructors)
    Adts[Info.type_name].push_back({Name, &Info});

  for (auto &[TypeName, Constructors] : Adts) {
    if (interface_export_filter_active_ &&
        !interface_exported_types_.contains(TypeName))
      continue;
    interface::Adt Value;
    Value.Name = TypeName;
    Value.VariantCount = Constructors.size();
    Value.IsOpaque = interface_opaque_types_.contains(TypeName);
    if (const auto Parameters = types_.adt_type_params.find(TypeName);
        Parameters != types_.adt_type_params.end())
      Value.TypeParameters = Parameters->second;
    std::sort(Constructors.begin(), Constructors.end(),
              [](const auto &Left, const auto &Right) {
                return std::pair(Left.second->tag, Left.first) <
                       std::pair(Right.second->tag, Right.first);
              });
    for (const auto &[Name, Info] : Constructors) {
      Value.MaxArity =
          std::max(Value.MaxArity, static_cast<std::size_t>(Info->arity));
      Value.IsRecursive = Value.IsRecursive || Info->is_recursive;
      if (Value.IsOpaque)
        continue;
      interface::Constructor Constructor;
      Constructor.Name = Name;
      Constructor.Tag = static_cast<std::size_t>(Info->tag);
      Constructor.Arity = static_cast<std::size_t>(Info->arity);
      for (std::size_t Index = 0; Index < Constructor.Arity; ++Index) {
        interface::AdtField Field;
        Field.Name = Index < Info->field_names.size()
                         ? Info->field_names[Index]
                         : "_" + std::to_string(Index);
        if (Index < Info->declared_field_types.size())
          Field.Type =
              constructor_field_descriptor(Info->declared_field_types[Index]);
        else
          Field.Type = interface_type(Index < Info->field_types.size()
                                          ? Info->field_types[Index]
                                          : CType::INT);
        Constructor.Fields.push_back(std::move(Field));
      }
      Value.Constructors.push_back(std::move(Constructor));
    }
    Module.Adts.push_back(std::move(Value));
  }

  for (const auto &[Name, Info] : types_.traits) {
    if (interface_export_filter_active_ &&
        !interface_trait_names_.contains(Name))
      continue;
    interface::Trait Value;
    Value.Name = Name;
    Value.TypeParameters = Info.type_params;
    for (const auto &[Supertype, Parameter] : Info.superclasses)
      Value.Supertypes.push_back({Supertype, Parameter});
    for (const std::string &MethodName : Info.method_names) {
      const auto Descriptor = Info.method_type_descriptors.find(MethodName);
      if (Descriptor == Info.method_type_descriptors.end() ||
          Descriptor->second.empty())
        return false;
      Value.Methods.push_back({MethodName, Descriptor->second});
    }
    Value.MethodCount = Value.Methods.size();
    Module.Traits.push_back(std::move(Value));
  }

  for (const auto &[Key, Info] : types_.trait_instances) {
    if (interface_export_filter_active_ &&
        !interface_instance_keys_.contains(Key))
      continue;
    interface::TraitInstance Value;
    Value.TraitName = Info.trait_name;
    Value.TypeHeads = Info.type_names;
    Value.TypeParameters = Info.type_params;
    for (const auto &[TraitName, Parameter] : Info.constraints)
      Value.Constraints.push_back({TraitName, Parameter});
    for (const auto &[MethodName, GeneratedExport] :
         Info.method_mangled_names) {
      const auto Identity = imports_.export_identities.find(GeneratedExport);
      if (Identity == imports_.export_identities.end())
        return false;
      Value.Implementations.emplace_back(
          MethodName, interface::ExportReference(
                          model::ModuleIdentity(Identity->second.module_fqn),
                          Identity->second.local_name));
    }
    Module.Instances.push_back(std::move(Value));
  }

  for (const auto &[GeneratedExport, Meta] : imports_.meta) {
    if (!imports_.interface_symbols.empty() &&
        !imports_.interface_symbols.contains(GeneratedExport))
      continue;
    const auto Identity = imports_.export_identities.find(GeneratedExport);
    if (Identity == imports_.export_identities.end() ||
        Identity->second.module_fqn != current_module_fqn_)
      continue;
    Module.Functions.push_back(
        interfaceFunctionFromMeta(Identity->second.local_name, Meta));
  }

  for (const auto &[GeneratedExport, SourceInfo] : imports_.function_source) {
    const bool IsExport = imports_.interface_symbols.empty() ||
                          imports_.interface_symbols.contains(GeneratedExport);
    const bool IsPrivate =
        imports_.private_genfn_symbols.contains(GeneratedExport);
    if ((!IsExport && !IsPrivate) ||
        SourceInfo.module_fqn != current_module_fqn_)
      continue;
    const auto Identity = imports_.export_identities.find(GeneratedExport);
    if (Identity == imports_.export_identities.end())
      return false;

    interface::GenericFunction Generic;
    Generic.Name = Identity->second.local_name;
    Generic.SourceName = SourceInfo.local_name;
    Generic.Source = trim_trailing_doc_comments(SourceInfo.source_text);

    for (const auto &[DependencyName, Dependency] :
         imports_.native_dependencies) {
      if (!contains_identifier(SourceInfo.source_text, DependencyName))
        continue;
      interface::Function Contract =
          interfaceFunctionFromMeta(DependencyName, Dependency.meta);
      if (const auto Target =
              imports_.export_identities.find(Dependency.c_symbol);
          Target != imports_.export_identities.end()) {
        Generic.Dependencies.emplace_back(
            std::move(Contract),
            interface::ExportReference(
                model::ModuleIdentity(Target->second.module_fqn),
                Target->second.local_name));
      } else {
        Generic.Dependencies.emplace_back(
            std::move(Contract),
            interface::NativeReference{Dependency.c_symbol});
      }
    }

    for (const auto &[ConstructorName, Info] : types_.adt_constructors) {
      if (!contains_identifier(SourceInfo.source_text, ConstructorName))
        continue;
      interface::GenericConstructor Constructor;
      Constructor.Name = ConstructorName;
      Constructor.TypeName = Info.type_name;
      Constructor.Tag = static_cast<std::size_t>(Info.tag);
      Constructor.Arity = static_cast<std::size_t>(Info.arity);
      Constructor.VariantCount = static_cast<std::size_t>(Info.total_variants);
      Constructor.MaxArity = static_cast<std::size_t>(Info.max_arity);
      Constructor.IsRecursive = Info.is_recursive;
      for (std::size_t Index = 0; Index < Constructor.Arity; ++Index) {
        interface::AdtField Field;
        Field.Name = Index < Info.field_names.size()
                         ? Info.field_names[Index]
                         : "_" + std::to_string(Index);
        if (Index < Info.declared_field_types.size())
          Field.Type =
              constructor_field_descriptor(Info.declared_field_types[Index]);
        else
          Field.Type = interface_type(Index < Info.field_types.size()
                                          ? Info.field_types[Index]
                                          : CType::INT);
        Constructor.Fields.push_back(std::move(Field));
      }
      Generic.Constructors.push_back(std::move(Constructor));
    }
    Module.GenericFunctions.push_back(std::move(Generic));
  }

  const auto Written = interface::writeModule(Path, Module);
  if (!Written)
    Session->diagnostics().error(SourceRange::unknown(),
                                 compiler::ErrorCode::E0400, Written.error());
  return Written.has_value();
}

bool Codegen::load_interface_file(const std::string &Path) {
  auto Parsed = interface::readModule(Path);
  if (!Parsed) {
    for (const auto &Error : Parsed.error()) {
      std::string Message = "invalid interface '" + Path + "'";
      if (Error.Line != 0)
        Message += ":" + std::to_string(Error.Line) + ":" +
                   std::to_string(Error.Column);
      Message += ": " + Error.Message;
      Session->diagnostics().error(SourceRange::unknown(),
                                   compiler::ErrorCode::E0301, Message);
    }
    return false;
  }

  const std::string ModuleFqn = Parsed->Identity.fqn();
  for (const interface::Adt &Value : Parsed->Adts) {
    types_.adt_type_params[Value.Name] = Value.TypeParameters;
    if (Value.IsOpaque)
      continue;
    for (const interface::Constructor &Constructor : Value.Constructors) {
      AdtInfo Info;
      Info.type_name = Value.Name;
      Info.tag = static_cast<int>(Constructor.Tag);
      Info.arity = static_cast<int>(Constructor.Arity);
      Info.total_variants = static_cast<int>(Value.VariantCount);
      Info.max_arity = static_cast<int>(Value.MaxArity);
      Info.is_recursive = Value.IsRecursive;
      for (const interface::AdtField &Field : Constructor.Fields) {
        const ast::FieldType Declared = field_type_from_descriptor(Field.Type);
        const AdtInfo::FieldShape Shape = field_shape_from_field_type(Declared);
        Info.field_names.push_back(Field.Name);
        Info.field_types.push_back(Shape.type);
        Info.field_fn_return_types.push_back(Shape.function_return_type);
        Info.field_fn_return_adt_names.push_back(
            Shape.function_return_adt_name);
        Info.field_shapes.push_back(Shape);
        Info.declared_field_types.push_back(Declared);
      }
      types_.adt_constructors[Constructor.Name] = std::move(Info);
    }
  }

  for (const interface::Trait &Value : Parsed->Traits) {
    TraitInfo Info;
    Info.name = Value.Name;
    Info.type_params = Value.TypeParameters;
    for (const interface::TraitSupertype &Supertype : Value.Supertypes)
      Info.superclasses.emplace_back(Supertype.Name, Supertype.Parameter);
    for (const interface::TraitMethod &Method : Value.Methods) {
      Info.method_names.push_back(Method.Name);
      Info.method_type_descriptors[Method.Name] = Method.Type;
    }
    types_.traits[Value.Name] = std::move(Info);
  }

  for (const interface::Function &Function : Parsed->Functions) {
    const std::string GeneratedExport = Function.exportName(Parsed->Identity);
    imports_.meta[GeneratedExport] = moduleMetaFromInterface(Function);
    imports_.export_identities[GeneratedExport] = {ModuleFqn, Function.Name};
    imports_.module_exports[ModuleFqn][Function.Name] = GeneratedExport;
  }

  for (const interface::TraitInstance &Value : Parsed->Instances) {
    std::string Key = Value.TraitName;
    for (const std::string &Head : Value.TypeHeads)
      Key += ":" + Head;
    TraitInstanceInfo Info;
    Info.trait_name = Value.TraitName;
    Info.type_names = Value.TypeHeads;
    Info.type_params = Value.TypeParameters;
    for (const interface::TraitConstraint &Constraint : Value.Constraints)
      Info.constraints.emplace_back(Constraint.TraitName, Constraint.Parameter);
    for (const interface::TraitImplementation &Implementation :
         Value.Implementations) {
      const std::string GeneratedExport = Implementation.Target.exportName();
      const std::string TargetModule = Implementation.Target.Module.fqn();
      Info.method_mangled_names[Implementation.MethodName] = GeneratedExport;
      imports_.export_identities[GeneratedExport] = {
          TargetModule, Implementation.Target.LocalName};
      imports_.module_exports[TargetModule][Implementation.Target.LocalName] =
          GeneratedExport;
    }
    types_.trait_instances[Key] = std::move(Info);
  }

  for (const interface::GenericFunction &Generic : Parsed->GenericFunctions) {
    const std::string Owner = Parsed->Identity.mangle(Generic.Name);
    imports_.imported_sources[Owner] = {Generic.Source, Generic.SourceName,
                                        ModuleFqn};
    imports_.export_identities[Owner] = {ModuleFqn, Generic.Name};
    imports_.module_exports[ModuleFqn][Generic.Name] = Owner;
    for (const interface::GenericDependency &Dependency :
         Generic.Dependencies) {
      const ModuleFunctionMeta Meta =
          moduleMetaFromInterface(Dependency.Contract);
      std::string Target;
      if (const auto *Native =
              std::get_if<interface::NativeReference>(&Dependency.Target)) {
        Target = Native->Symbol;
      } else {
        const auto &Reference =
            std::get<interface::ExportReference>(Dependency.Target);
        Target = Reference.exportName();
        const std::string TargetModule = Reference.Module.fqn();
        imports_.export_identities[Target] = {TargetModule,
                                              Reference.LocalName};
        imports_.module_exports[TargetModule][Reference.LocalName] = Target;
      }
      imports_.private_genfn_dependencies[Owner].push_back(
          {Dependency.Contract.Name, NativeDependency{Target, Meta}});
      imports_.meta.try_emplace(Target, Meta);
    }
    for (const interface::GenericConstructor &Constructor :
         Generic.Constructors) {
      AdtInfo Info;
      Info.type_name = Constructor.TypeName;
      Info.tag = static_cast<int>(Constructor.Tag);
      Info.arity = static_cast<int>(Constructor.Arity);
      Info.total_variants = static_cast<int>(Constructor.VariantCount);
      Info.max_arity = static_cast<int>(Constructor.MaxArity);
      Info.is_recursive = Constructor.IsRecursive;
      for (const interface::AdtField &Field : Constructor.Fields) {
        const ast::FieldType Declared = field_type_from_descriptor(Field.Type);
        const AdtInfo::FieldShape Shape = field_shape_from_field_type(Declared);
        Info.field_names.push_back(Field.Name);
        Info.field_types.push_back(Shape.type);
        Info.field_fn_return_types.push_back(Shape.function_return_type);
        Info.field_fn_return_adt_names.push_back(
            Shape.function_return_adt_name);
        Info.field_shapes.push_back(Shape);
        Info.declared_field_types.push_back(Declared);
      }
      imports_.private_genfn_ctors[Owner].push_back(
          {Constructor.Name, std::move(Info)});
    }
  }
  return true;
}

// Build FQN string and filesystem path from an FqnExpr
std::pair<std::string, std::filesystem::path>
Codegen::build_fqn_path(FqnExpr *fqn) {
  std::string mod_fqn;
  std::filesystem::path mod_path;
  if (fqn->packageName.has_value()) {
    auto *pkg = fqn->packageName.value();
    for (size_t i = 0; i < pkg->parts.size(); i++) {
      if (i > 0)
        mod_fqn += "\\";
      mod_fqn += pkg->parts[i]->value;
      mod_path /= pkg->parts[i]->value;
    }
    mod_fqn += "\\";
  }
  mod_fqn += fqn->moduleName->value;
  mod_path /= fqn->moduleName->value;
  return {mod_fqn, mod_path};
}

// Load the canonical .yonai interface for a module.
void Codegen::load_module_interface(const std::filesystem::path &mod_path) {
  auto yonai_name = mod_path;
  yonai_name.replace_extension(".yonai");
  for (auto &search_path : ModulePaths) {
    auto candidate = std::filesystem::path(search_path) / yonai_name;
    if (std::filesystem::exists(candidate)) {
      load_interface_file(candidate.string());
      return;
    }
  }
}

void Codegen::load_module_by_fqn(const std::string &mod_fqn) {
  std::filesystem::path p;
  std::string rest = mod_fqn;
  while (!rest.empty()) {
    auto pos = rest.find('\\');
    if (pos == std::string::npos) {
      p /= rest;
      break;
    }
    p /= rest.substr(0, pos);
    rest = rest.substr(pos + 1);
  }
  load_module_interface(p);
}

static std::string constructor_field_descriptor(const ast::FieldType &field) {
  if (field.is_tuple_type) {
    std::string result = "TUPLE(";
    for (size_t i = 0; i < field.tuple_types.size(); ++i) {
      if (i)
        result += ",";
      result += constructor_field_descriptor(field.tuple_types[i]);
    }
    return result + ")";
  }
  if (field.is_function_type) {
    std::string result =
        field.return_types.empty()
            ? "UNIT"
            : constructor_field_descriptor(field.return_types.front());
    for (auto it = field.param_types.rbegin(); it != field.param_types.rend();
         ++it)
      result =
          "FUNCTION(" + constructor_field_descriptor(*it) + "," + result + ")";
    return result;
  }
  if (!field.name.empty() &&
      std::islower(static_cast<unsigned char>(field.name.front())))
    return "VAR(" + field.name + ")";
  auto builtin = [](const std::string &name) -> std::string {
    if (name == "Int" || name == "Byte" || name == "Char")
      return "INT";
    if (name == "Float")
      return "FLOAT";
    if (name == "Bool")
      return "BOOL";
    if (name == "String")
      return "STRING";
    if (name == "Symbol")
      return "SYMBOL";
    if (name == "Unit" || name == "()")
      return "UNIT";
    return {};
  };
  if (const auto scalar = builtin(field.name); !scalar.empty())
    return scalar;
  if (field.name == "Seq" || field.name == "Set") {
    if (field.type_arguments.empty())
      return field.name == "Seq" ? "SEQ" : "SET";
    return field.name + "(" +
           constructor_field_descriptor(field.type_arguments.front()) + ")";
  }
  if (field.name == "Dict") {
    if (field.type_arguments.size() < 2)
      return "DICT";
    return "Dict(" + constructor_field_descriptor(field.type_arguments[0]) +
           "," + constructor_field_descriptor(field.type_arguments[1]) + ")";
  }
  std::string result = "ADT(" + field.name;
  for (const auto &argument : field.type_arguments)
    result += "," + constructor_field_descriptor(argument);
  return result + ")";
}

static std::vector<std::string_view>
split_descriptor_arguments(std::string_view Arguments) {
  std::vector<std::string_view> Parts;
  std::size_t Start = 0;
  int Depth = 0;
  for (std::size_t Index = 0; Index < Arguments.size(); ++Index) {
    if (Arguments[Index] == '(')
      ++Depth;
    else if (Arguments[Index] == ')')
      --Depth;
    else if (Arguments[Index] == ',' && Depth == 0) {
      Parts.push_back(Arguments.substr(Start, Index - Start));
      Start = Index + 1;
    }
  }
  if (Start < Arguments.size() || !Arguments.empty())
    Parts.push_back(Arguments.substr(Start));
  return Parts;
}

static ast::FieldType field_type_from_descriptor(std::string_view Descriptor) {
  auto unwrap = [](std::string_view Value,
                   std::string_view Prefix) -> std::optional<std::string_view> {
    if (!Value.starts_with(Prefix) || Value.size() <= Prefix.size() ||
        Value[Prefix.size()] != '(' || !Value.ends_with(')'))
      return std::nullopt;
    return Value.substr(Prefix.size() + 1, Value.size() - Prefix.size() - 2);
  };

  if (const auto Inner = unwrap(Descriptor, "LINEAR"))
    return field_type_from_descriptor(*Inner);
  if (const auto Inner = unwrap(Descriptor, "VAR"))
    return ast::FieldType::simple(std::string(*Inner));
  if (const auto Inner = unwrap(Descriptor, "TUPLE")) {
    ast::FieldType Result;
    Result.is_tuple_type = true;
    for (const auto Part : split_descriptor_arguments(*Inner))
      Result.tuple_types.push_back(field_type_from_descriptor(Part));
    return Result;
  }
  if (const auto Inner = unwrap(Descriptor, "FUNCTION")) {
    const auto Parts = split_descriptor_arguments(*Inner);
    ast::FieldType Result;
    Result.is_function_type = true;
    if (!Parts.empty())
      Result.param_types.push_back(field_type_from_descriptor(Parts.front()));
    if (Parts.size() > 1) {
      auto Return = field_type_from_descriptor(Parts[1]);
      if (Return.is_function_type) {
        Result.param_types.insert(Result.param_types.end(),
                                  Return.param_types.begin(),
                                  Return.param_types.end());
        Result.return_types = std::move(Return.return_types);
      } else {
        Result.return_types.push_back(std::move(Return));
      }
    }
    return Result;
  }

  auto named = [&](std::string_view Prefix,
                   std::string_view Name) -> std::optional<ast::FieldType> {
    const auto Inner = unwrap(Descriptor, Prefix);
    if (!Inner)
      return std::nullopt;
    ast::FieldType Result = ast::FieldType::simple(std::string(Name));
    for (const auto Part : split_descriptor_arguments(*Inner))
      Result.type_arguments.push_back(field_type_from_descriptor(Part));
    return Result;
  };
  if (const auto Result = named("Seq", "Seq"))
    return *Result;
  if (const auto Result = named("Set", "Set"))
    return *Result;
  if (const auto Result = named("Dict", "Dict"))
    return *Result;
  if (const auto Result = named("Promise", "Promise"))
    return *Result;
  if (const auto Inner = unwrap(Descriptor, "ADT")) {
    const auto Parts = split_descriptor_arguments(*Inner);
    ast::FieldType Result = ast::FieldType::simple(
        Parts.empty() ? std::string{} : std::string(Parts.front()));
    for (std::size_t Index = 1; Index < Parts.size(); ++Index)
      Result.type_arguments.push_back(field_type_from_descriptor(Parts[Index]));
    return Result;
  }

  static const std::map<std::string_view, std::string_view> Builtins = {
      {"INT", "Int"},
      {"FLOAT", "Float"},
      {"BOOL", "Bool"},
      {"STRING", "String"},
      {"SYMBOL", "Symbol"},
      {"UNIT", "Unit"},
      {"SEQ", "Seq"},
      {"SET", "Set"},
      {"DICT", "Dict"},
      {"PROMISE", "Promise"},
      {"BYTE_ARRAY", "ByteArray"},
      {"INT_ARRAY", "IntArray"},
      {"FLOAT_ARRAY", "FloatArray"},
      {"CHANNEL", "Channel"},
      {"SUM", "Sum"},
      {"RECORD", "Record"},
  };
  if (const auto Found = Builtins.find(Descriptor); Found != Builtins.end())
    return ast::FieldType::simple(std::string(Found->second));
  return ast::FieldType::simple(std::string(Descriptor));
}

Codegen::GenfnNameIsolation::GenfnNameIsolation(Codegen &cg,
                                                std::string mangled)
    : cg(cg) {
  ++cg.genfn_isolation_depth_;
  saved_externs = cg.imports_.extern_functions;
  saved_compiled_functions = cg.compiled_functions_;
  saved_deferred_functions = cg.deferred_functions_;
  saved_named_values = cg.named_values_;
  saved_adt_constructors = cg.types_.adt_constructors;
  cg.active_genfn_isolations_.push_back(this);
  cg.imports_.extern_functions.clear();
  std::string module_fqn;
  if (const auto source = cg.imports_.imported_sources.find(mangled);
      source != cg.imports_.imported_sources.end())
    module_fqn = source->second.module_fqn;
  if (module_fqn.empty()) {
    if (const auto identity = cg.imports_.export_identities.find(mangled);
        identity != cg.imports_.export_identities.end())
      module_fqn = identity->second.module_fqn;
  }
  if (const auto module = cg.imports_.module_exports.find(module_fqn);
      module != cg.imports_.module_exports.end()) {
    for (const auto &[dep_name, dep_mangled] : module->second) {
      const auto metadata = cg.imports_.meta.find(dep_mangled);
      if (metadata == cg.imports_.meta.end())
        continue;
      const auto &dep_meta = metadata->second;
      // The root GENFN is compiled under its local source name. Adding
      // its exported symbol as an external dependency makes an
      // unqualified trait call in the body (for example `hash head`
      // inside Hash (Seq a)) recursively call the sequence instance
      // instead of dispatching on the element type.
      if (dep_mangled == mangled)
        continue;
      cg.imports_.extern_functions[dep_name] = dep_mangled;
      if (dep_meta.param_types.empty() &&
          cg.compiled_functions_.find(dep_name) ==
              cg.compiled_functions_.end()) {
        auto *ret_ty = cg.llvm_type(dep_meta.return_type);
        auto *fn_type = llvm::FunctionType::get(ret_ty, {}, false);
        auto *fn = cg.module_->getFunction(dep_mangled);
        if (!fn)
          fn = Function::Create(fn_type, Function::ExternalLinkage, dep_mangled,
                                cg.module_);
        cg.compiled_functions_[dep_name] =
            cg.compiled_function_from_meta(fn, dep_meta, dep_meta.return_type);
        scoped_cafs.push_back(dep_name);
      }
    }
  }
  if (const auto dependencies =
          cg.imports_.private_genfn_dependencies.find(mangled);
      dependencies != cg.imports_.private_genfn_dependencies.end()) {
    for (const auto &[local_name, dependency] : dependencies->second) {
      cg.compiled_functions_.erase(local_name);
      cg.deferred_functions_.erase(local_name);
      cg.named_values_.erase(local_name);
      cg.imports_.extern_functions[local_name] = dependency.c_symbol;
      scoped_dependency_names.push_back(local_name);
    }
  }
}

void Codegen::GenfnNameIsolation::restore() {
  if (restored)
    return;
  restored = true;
  const auto active = std::find(cg.active_genfn_isolations_.begin(),
                                cg.active_genfn_isolations_.end(), this);
  if (active != cg.active_genfn_isolations_.end())
    cg.active_genfn_isolations_.erase(active);
  --cg.genfn_isolation_depth_;
  cg.compiled_functions_ = std::move(saved_compiled_functions);
  for (auto it = cg.deferred_functions_.begin();
       it != cg.deferred_functions_.end();) {
    if (!saved_deferred_functions.count(it->first))
      it = cg.deferred_functions_.erase(it);
    else
      ++it;
  }
  for (const auto &[name, deferred] : saved_deferred_functions) {
    auto current = cg.deferred_functions_.find(name);
    if (current == cg.deferred_functions_.end())
      cg.deferred_functions_.emplace(name, deferred);
    else if (current->second.ast != deferred.ast)
      current->second = deferred;
  }
  cg.named_values_ = std::move(saved_named_values);
  cg.imports_.extern_functions = std::move(saved_externs);
  cg.types_.adt_constructors = std::move(saved_adt_constructors);
}

Codegen::ActiveNamedValueSnapshot::ActiveNamedValueSnapshot(
    Codegen &cg, NamedValueBindings &bindings)
    : cg_(cg), bindings_(bindings) {
  cg_.active_named_value_snapshots_.push_back(&bindings_);
}

Codegen::ActiveNamedValueSnapshot::~ActiveNamedValueSnapshot() {
  const auto active =
      std::find(cg_.active_named_value_snapshots_.begin(),
                cg_.active_named_value_snapshots_.end(), &bindings_);
  if (active != cg_.active_named_value_snapshots_.end())
    cg_.active_named_value_snapshots_.erase(active);
}

Codegen::ActiveTypedValueSnapshot::ActiveTypedValueSnapshot(Codegen &cg,
                                                            TypedValue &value)
    : cg_(cg), value_(value) {
  cg_.active_typed_value_snapshots_.push_back(&value_);
}

Codegen::ActiveTypedValueSnapshot::~ActiveTypedValueSnapshot() {
  const auto active =
      std::find(cg_.active_typed_value_snapshots_.begin(),
                cg_.active_typed_value_snapshots_.end(), &value_);
  if (active != cg_.active_typed_value_snapshots_.end())
    cg_.active_typed_value_snapshots_.erase(active);
}

void Codegen::migrate_function_references(Function *obsolete,
                                          Function *replacement) {
  if (!obsolete || !replacement || obsolete == replacement)
    return;

  auto migrate_bindings = [&](auto &bindings) {
    for (auto &[_, value] : bindings)
      if (value.val == obsolete)
        value.val = replacement;
  };
  auto migrate_functions = [&](auto &functions) {
    for (auto &[_, compiled] : functions)
      if (compiled.fn == obsolete)
        compiled.fn = replacement;
  };

  migrate_bindings(named_values_);
  migrate_functions(compiled_functions_);
  for (auto *bindings : active_named_value_snapshots_) {
    if (bindings)
      migrate_bindings(*bindings);
  }
  for (auto *value : active_typed_value_snapshots_) {
    if (value && value->val == obsolete)
      value->val = replacement;
  }
  for (auto *isolation : active_genfn_isolations_) {
    if (!isolation)
      continue;
    migrate_bindings(isolation->saved_named_values);
    migrate_functions(isolation->saved_compiled_functions);
  }
}

void Codegen::install_private_genfn_ctors(const std::string &mangled) {
  auto it = imports_.private_genfn_ctors.find(mangled);
  if (it == imports_.private_genfn_ctors.end())
    return;
  for (const auto &[name, source_info] : it->second) {
    auto info = source_info;
    if (const auto existing = types_.adt_constructors.find(name);
        existing != types_.adt_constructors.end()) {
      if (info.declared_field_types.empty())
        info.declared_field_types = existing->second.declared_field_types;
      if (info.field_shapes.empty())
        info.field_shapes = existing->second.field_shapes;
    }
    types_.adt_constructors[name] = std::move(info);
  }
}

void Codegen::register_sibling_genfns(const std::string &mangled) {
  const auto root_source_it = imports_.imported_sources.find(mangled);
  if (root_source_it == imports_.imported_sources.end() ||
      root_source_it->second.module_fqn.empty())
    return;
  const std::string &module_fqn = root_source_it->second.module_fqn;
  // Imported GENFNs are module-level. Analyze them without the caller's
  // locals â€” otherwise a parameter named `rest` looks like a free var of
  // `sortBy`'s `[pivot|rest]` clause and the sibling is compiled as a
  // dummy-INT closure (`undefined function 'cmp'`).
  auto saved_nv = named_values_;
  ActiveNamedValueSnapshot saved_nv_snapshot(*this, saved_nv);
  named_values_.clear();
  std::vector<std::string> reachable_sources;
  reachable_sources.push_back(root_source_it->second.source_text);
  const std::string &root_local_name = root_source_it->second.local_name;
  std::unordered_set<std::string> registered_dependencies;
  bool discovered_dependency = true;
  while (discovered_dependency) {
    discovered_dependency = false;
    for (const auto &[dep_mangled, ifs] : imports_.imported_sources) {
      if (dep_mangled == mangled)
        continue;
      if (ifs.module_fqn != module_fqn)
        continue;
      if (ifs.local_name == root_local_name ||
          registered_dependencies.count(dep_mangled))
        continue;
      bool referenced = false;
      for (const auto &source : reachable_sources) {
        if (contains_identifier(source, ifs.local_name)) {
          referenced = true;
          break;
        }
      }
      if (!referenced)
        continue;
      install_private_genfn_ctors(dep_mangled);
      auto reparsed = reparse_genfn(ifs.local_name, ifs.source_text);
      if (!reparsed || reparsed->functions.empty())
        continue;
      auto *func_ast = reparsed->functions[0];
      reparsed->functions.clear();
      imports_.imported_ast_nodes.push_back(
          std::unique_ptr<FunctionExpr>(func_ast));
      compiled_functions_.erase(ifs.local_name);
      deferred_functions_.erase(ifs.local_name);
      named_values_.erase(ifs.local_name);
      codegen_function_def(func_ast, ifs.local_name);
      registered_dependencies.insert(dep_mangled);
      reachable_sources.push_back(ifs.source_text);
      discovered_dependency = true;
    }
  }
  named_values_ = std::move(saved_nv);
}

TypedValue Codegen::dummy_typed_value(CType ct) {
  auto *i64_ty = LType::getInt64Ty(*context_);
  auto *ptr_ty = PointerType::get(*context_, 0);
  switch (ct) {
  case CType::FLOAT:
    return {ConstantFP::get(LType::getDoubleTy(*context_), 0.0), ct};
  case CType::BOOL:
    return {ConstantInt::get(LType::getInt1Ty(*context_), 0), ct};
  case CType::STRING:
  case CType::SEQ:
  case CType::FUNCTION:
  case CType::SET:
  case CType::DICT:
  case CType::BYTE_ARRAY:
  case CType::INT_ARRAY:
  case CType::FLOAT_ARRAY:
  case CType::PROMISE:
  case CType::CHANNEL:
    return {ConstantPointerNull::get(ptr_ty), ct};
  default:
    return {ConstantInt::get(i64_ty, 0), ct};
  }
}

TypedValue
Codegen::materialize_imported_function_value(const std::string &name) {
  auto ext_it = imports_.extern_functions.find(name);
  if (ext_it == imports_.extern_functions.end())
    return {};
  const std::string mangled = ext_it->second;

  auto wrap_existing = [&](Function *fn, CType ret) -> TypedValue {
    if (!fn || !builder_ || !builder_->GetInsertBlock())
      return {};
    Value *clo = wrap_in_closure(fn, ret);
    TypedValue tv{clo, CType::FUNCTION, {ret}};
    named_values_[name] = tv;
    return tv;
  };

  auto cf_it = compiled_functions_.find(name);
  if (cf_it != compiled_functions_.end() && cf_it->second.fn) {
    size_t user_arity =
        cf_it->second.param_types.size() - cf_it->second.capture_names.size();
    if (user_arity > 0)
      return wrap_existing(cf_it->second.fn, cf_it->second.return_type);
  }

  auto genfn_it = imports_.imported_sources.find(mangled);
  auto meta_it = imports_.meta.find(mangled);
  if (genfn_it != imports_.imported_sources.end() &&
      meta_it != imports_.meta.end() && !meta_it->second.param_types.empty()) {
    std::vector<TypedValue> dummy_args;
    for (auto ct : meta_it->second.param_types)
      dummy_args.push_back(dummy_typed_value(ct));
    int errors_before = Session->errorCount();
    // The parser needs private constructor metadata before it sees the
    // exported source. Keep it inside the same isolation scope as the
    // later compilation so it cannot leak into the importing module.
    GenfnNameIsolation iso(*this, mangled);
    install_private_genfn_ctors(mangled);
    auto reparsed = reparse_genfn(genfn_it->second.local_name,
                                  genfn_it->second.source_text);
    if (reparsed && !reparsed->functions.empty()) {
      auto *func_ast = reparsed->functions[0];
      reparsed->functions.clear();
      imports_.imported_ast_nodes.push_back(
          std::unique_ptr<FunctionExpr>(func_ast));
      register_sibling_genfns(mangled);
      codegen_function_def(func_ast, name);
      auto def_it = deferred_functions_.find(name);
      if (def_it != deferred_functions_.end()) {
        compile_function(name, def_it->second, dummy_args);
        auto cf2 = compiled_functions_.find(name);
        std::optional<CompiledFunction> materialized;
        if (cf2 != compiled_functions_.end() && cf2->second.fn &&
            Session->errorCount() == errors_before)
          materialized = cf2->second;
        iso.restore();
        if (materialized) {
          compiled_functions_[name] = std::move(*materialized);
          imports_.extern_functions.erase(name);
          auto &compiled = compiled_functions_.at(name);
          return wrap_existing(compiled.fn, compiled.return_type);
        }
      } else {
        iso.restore();
      }
    }
  }

  if (meta_it == imports_.meta.end())
    return {};
  auto &meta = meta_it->second;
  std::vector<LType *> arg_types;
  for (auto ct : meta.param_types)
    arg_types.push_back(llvm_type(ct));
  auto *fn_type =
      llvm::FunctionType::get(llvm_type(meta.return_type), arg_types, false);
  auto *ext_fn = module_->getFunction(mangled);
  if (!ext_fn)
    ext_fn =
        Function::Create(fn_type, Function::ExternalLinkage, mangled, module_);
  compiled_functions_[name] =
      compiled_function_from_meta(ext_fn, meta, meta.return_type);
  return wrap_existing(ext_fn, meta.return_type);
}

std::unique_ptr<ast::ModuleDecl>
Codegen::reparse_genfn(const std::string &local_name,
                       const std::string &source_text) {
  std::vector<semantics::GenericConstructorMetadata> Constructors;
  Constructors.reserve(types_.adt_constructors.size());
  for (const auto &[Name, Info] : types_.adt_constructors) {
    Constructors.push_back(
        {Name, Info.type_name, Info.tag, Info.arity, Info.field_names});
  }
  semantics::GenericFunctionSourceService SourceService;
  auto Parsed =
      SourceService.parseGenericModule(local_name, source_text, Constructors);
  if (!Parsed)
    return nullptr;
  imports_.imported_source_managers.push_back(std::move(Parsed->Sources));
  auto mod = std::move(Parsed->Module);
  // Callers steal FunctionExpr* and destroy this wrapper module. The
  // function's parent still pointed at ModuleDecl, so later parent
  // walks (accelerator import resolution on `Yield` / other applies)
  // followed a dangling pointer into freed memory â€” SIGSEGV on
  // Stream.map's lazy ADT path after HOF closure materialization.
  for (auto *fn : mod->functions) {
    if (fn)
      fn->parent = nullptr;
  }
  return mod;
}

// Every ADT has one stable opaque-pointer ABI. Constructor shape remains
// compile-time metadata used for field extraction and ownership masks.
LType *Codegen::adt_llvm_type(const std::string &type_name) {
  (void)type_name;
  return PointerType::get(*context_, 0);
}

// Register trait instance methods as extern function declarations
// so that re-parsed GENFN bodies can call them via trait dispatch.
void Codegen::register_trait_externs() {
  for (auto &[key, inst] : types_.trait_instances) {
    for (auto &[method_name, mangled] : inst.method_mangled_names) {
      if (compiled_functions_.count(mangled) > 0)
        continue;
      auto meta_it = imports_.meta.find(mangled);
      if (meta_it != imports_.meta.end()) {
        auto &meta = meta_it->second;
        std::vector<LType *> param_types;
        for (size_t i = 0; i < meta.param_types.size(); i++) {
          if (meta.param_types[i] == CType::ADT) {
            param_types.push_back(adt_llvm_type(inst.type_names.empty()
                                                    ? std::string{}
                                                    : inst.type_names.front()));
          } else {
            param_types.push_back(llvm_type(meta.param_types[i]));
          }
        }
        auto *ret_llvm =
            (meta.return_type == CType::ADT)
                ? adt_llvm_type(meta.return_adt_name.empty()
                                    ? (inst.type_names.empty()
                                           ? std::string{}
                                           : inst.type_names.front())
                                    : meta.return_adt_name)
                : llvm_type(meta.return_type);
        auto *fn_type = llvm::FunctionType::get(ret_llvm, param_types, false);
        auto *fn = module_->getFunction(mangled);
        if (!fn)
          fn = Function::Create(fn_type, Function::ExternalLinkage, mangled,
                                module_);
        auto compiled = compiled_function_from_meta(fn, meta, meta.return_type);
        compiled_functions_[mangled] = std::move(compiled);
      }
    }
  }
}

llvm::Function *
Codegen::declare_import_extern_fn(const std::string &mangled,
                                  const ModuleFunctionMeta &meta) {
  auto i64_ty = LType::getInt64Ty(*context_);
  auto ptr_ty = PointerType::get(*context_, 0);
  std::vector<LType *> param_types;
  for (auto ct : meta.param_types)
    param_types.push_back(llvm_type(ct));
  if (meta.extern_promise == ast::ExternPromiseKind::NativePtr)
    param_types.push_back(ptr_ty);

  llvm::Type *ret_ty = nullptr;
  switch (meta.extern_promise) {
  case ast::ExternPromiseKind::Sync:
    return nullptr;
  case ast::ExternPromiseKind::IoUring:
    ret_ty = i64_ty;
    break;
  case ast::ExternPromiseKind::NativePtr:
    ret_ty = ptr_ty;
    break;
  case ast::ExternPromiseKind::ThreadPool:
    ret_ty = llvm_type(meta.promise_inner_type);
    break;
  }
  auto *fn_type = llvm::FunctionType::get(ret_ty, param_types, false);
  llvm::Function *fn = module_->getFunction(mangled);
  if (!fn)
    fn = Function::Create(fn_type, Function::ExternalLinkage, mangled, module_);
  return fn;
}

void Codegen::bind_imported_promise_cf(const std::string &logical_name,
                                       llvm::Function *fn,
                                       const ModuleFunctionMeta &meta) {
  CompiledFunction cf;
  cf.fn = fn;
  cf.return_type = CType::PROMISE;
  cf.param_types = meta.param_types;
  cf.borrowed_params = meta.borrowed_params;
  cf.extern_promise = meta.extern_promise;
  cf.promise_inner_type = meta.promise_inner_type;
  compiled_functions_[logical_name] = cf;
  named_values_[logical_name] = {
      fn, CType::FUNCTION, {meta.promise_inner_type}};
}

// Register a single imported function/constructor by name
void Codegen::register_import(const std::string &mod_fqn,
                              const std::string &func_name,
                              const std::string &import_name) {
  // Check if it's an ADT constructor
  auto ctor_it = types_.adt_constructors.find(func_name);
  if (ctor_it != types_.adt_constructors.end()) {
    if (ctor_it->second.arity > 0)
      named_values_[import_name] = {nullptr, CType::FUNCTION};
    if (import_name != func_name)
      types_.adt_constructors[import_name] = ctor_it->second;
    return;
  }

  std::string mangled = mangle_name(mod_fqn, func_name);

  // Register as extern â€” the pre-compiled version from the module is the
  // default. GENFN source (if available) is stored in imports_.imported_sources
  // for on-demand monomorphization when call-site types differ from the
  // module's.
  auto meta_it = imports_.meta.find(mangled);
  if (meta_it != imports_.meta.end() &&
      meta_it->second.extern_promise != ast::ExternPromiseKind::Sync) {
    llvm::Function *fn = declare_import_extern_fn(mangled, meta_it->second);
    bind_imported_promise_cf(import_name, fn, meta_it->second);
  } else if (meta_it != imports_.meta.end() &&
             meta_it->second.param_types.empty()) {
    // Zero-arity function: create extern declaration so it can be called.
    // Don't set named_values_ â€” let codegen_identifier find it in
    // compiled_functions_ and return it as a callable function reference.
    auto &meta = meta_it->second;
    auto *ret_ty = llvm_type(meta.return_type);
    auto *fn_type = llvm::FunctionType::get(ret_ty, {}, false);
    auto *fn = module_->getFunction(mangled);
    if (!fn)
      fn = Function::Create(fn_type, Function::ExternalLinkage, mangled,
                            module_);
    compiled_functions_[import_name] =
        compiled_function_from_meta(fn, meta, meta.return_type);
    imports_.extern_functions[import_name] = mangled;
  } else {
    named_values_[import_name] = {nullptr, CType::FUNCTION};
    imports_.extern_functions[import_name] = mangled;
  }
}

// Register ALL exports from a loaded .yonai (wildcard import)
void Codegen::register_all_imports(const std::string &mod_fqn) {
  // Register all functions from the module's structural export table.
  const auto module = imports_.module_exports.find(mod_fqn);
  if (module != imports_.module_exports.end()) {
    for (const auto &[func_name, mangled] : module->second) {
      const auto metadata = imports_.meta.find(mangled);
      if (metadata == imports_.meta.end())
        continue;
      const auto &meta = metadata->second;
      if (meta.extern_promise != ast::ExternPromiseKind::Sync) {
        llvm::Function *fn = declare_import_extern_fn(mangled, meta);
        bind_imported_promise_cf(func_name, fn, meta);
      } else {
        named_values_[func_name] = {nullptr, CType::FUNCTION};
        imports_.extern_functions[func_name] = mangled;
      }
    }
  }
  // Register all constructors
  for (auto &[name, info] : types_.adt_constructors) {
    if (info.type_name.find(mod_fqn) != std::string::npos ||
        imports_.meta.count(mangle_name(mod_fqn, name)) > 0) {
      // Already registered by load_interface_file
    }
  }
}

TypedValue Codegen::codegen_import(ImportExpr *node) {
  for (auto *clause : node->clauses) {
    if (clause->get_type() == ast::AST_FUNCTIONS_IMPORT) {
      auto *fi = static_cast<FunctionsImport *>(clause);
      auto [mod_fqn, mod_path] = build_fqn_path(fi->fromFqn);
      load_module_interface(mod_path);

      for (auto *alias : fi->aliases) {
        std::string func_name = alias->name->value;
        std::string import_name =
            alias->alias ? alias->alias->value : func_name;
        register_import(mod_fqn, func_name, import_name);
      }
      register_trait_externs();
    } else if (clause->get_type() == ast::AST_MODULE_IMPORT) {
      // Wildcard import: import Std\List in expr
      auto *mi = static_cast<ModuleImport *>(clause);
      auto [mod_fqn, mod_path] = build_fqn_path(mi->fqn);
      load_module_interface(mod_path);
      register_all_imports(mod_fqn);
      register_trait_externs();
    }
  }

  return codegen(node->expr);
}

TypedValue Codegen::codegen_extern_decl(ExternDeclExpr *node) {
  // Extract parameter types and return type from the declared type
  std::vector<LType *> param_types;
  std::vector<CType> param_ctypes;
  std::vector<std::string> param_descriptors;
  CType ret_ctype = CType::INT;

  auto current_type = node->declared_type;

  // Uncurry: A -> B -> C becomes params=[A, B], ret=C
  while (std::holds_alternative<std::shared_ptr<types::FunctionType>>(
      current_type)) {
    auto ft = std::get<std::shared_ptr<types::FunctionType>>(current_type);
    const auto *named_param =
        std::get_if<std::shared_ptr<types::NamedType>>(&ft->argumentType);
    const bool param_is_variable =
        named_param && *named_param && !(*named_param)->name.empty() &&
        std::islower(static_cast<unsigned char>((*named_param)->name.front()));
    CType param_ct =
        param_is_variable ? CType::INT : yona_type_to_ctype(ft->argumentType);
    param_ctypes.push_back(param_ct);
    param_descriptors.push_back(source_type_descriptor(ft->argumentType));
    param_types.push_back(llvm_type(param_ct));
    current_type = ft->returnType;
  }
  const auto *named_return =
      std::get_if<std::shared_ptr<types::NamedType>>(&current_type);
  const bool return_is_variable =
      named_return && *named_return && !(*named_return)->name.empty() &&
      std::islower(static_cast<unsigned char>((*named_return)->name.front()));
  ret_ctype =
      return_is_variable ? CType::INT : yona_type_to_ctype(current_type);
  std::string ret_adt_name =
      (ret_ctype == CType::ADT) ? yona_type_adt_name(current_type) : "";
  // For ADT returns, externs always come from the C runtime returning a
  // heap-allocated ADT pointer (i64 cast). Use ptr in the function signature.
  // For `extern io` (io_uring submit-and-return), the C function returns
  // an i64 uring user_data ID; the codegen sees a Promise and auto-awaits.
  // For `extern native`, C returns opaque YonaTask* (LLVM i8* / ptr).
  auto i64_ty = LType::getInt64Ty(*context_);
  auto ptr_ty = PointerType::get(*context_, 0);
  auto ret_llvm =
      node->extern_promise == ast::ExternPromiseKind::IoUring
          ? static_cast<LType *>(i64_ty)
          : (node->extern_promise == ast::ExternPromiseKind::NativePtr
                 ? static_cast<LType *>(ptr_ty)
                 : ((ret_ctype == CType::ADT) ? static_cast<LType *>(ptr_ty)
                                              : llvm_type(ret_ctype)));

  // The C ABI symbol may differ from the local Yona name (e.g.
  // `extern channel_new : Int -> Channel = "YonaStdChannelChannel"`).
  // The LLVM function takes the ABI symbol; we register it locally under
  // the Yona name so call sites resolve naturally.
  const std::string &c_sym =
      node->c_symbol.empty() ? node->name : node->c_symbol;

  auto abi_param_types = param_types;
  // Channel construction is a type-directed intrinsic. Its native ABI has
  // one hidden descriptor argument selected from the concrete result type
  // at each call site; the Yona signature intentionally exposes only the
  // capacity.
  if (c_sym == "YonaStdChannelChannel")
    abi_param_types.push_back(ptr_ty);
  if (node->extern_promise == ast::ExternPromiseKind::NativePtr)
    abi_param_types.push_back(ptr_ty);
  auto fn_type = llvm::FunctionType::get(ret_llvm, abi_param_types, false);
  auto *fn = module_->getFunction(c_sym);
  // Source declarations are authoritative. A previously loaded interface
  // can contain an older or less precise ABI (notably generic collection
  // descriptors); retaining that declaration makes LLVM silently bitcast
  // pointer arguments to integers and corrupts the runtime heap.
  if (fn && fn->isDeclaration() && fn->getFunctionType() != fn_type) {
    compiled_functions_.erase(node->name);
    if (!fn->use_empty()) {
      report_error(node->Range,
                   "extern ABI mismatch for '" + c_sym +
                       "': an earlier declaration already has live callers; "
                       "make the source and interface signatures agree");
      return {};
    }
    fn->eraseFromParent();
    fn = nullptr;
  }
  if (!fn) {
    fn = Function::Create(fn_type, Function::ExternalLinkage, c_sym, module_);
  }

  // Register as a compiled function. `extern_promise` selects call/await
  // lowering.
  CompiledFunction cf;
  cf.fn = fn;
  const bool is_promise_extern =
      node->extern_promise != ast::ExternPromiseKind::Sync;
  cf.return_type = is_promise_extern ? CType::PROMISE : ret_ctype;
  cf.param_types = param_ctypes;
  cf.param_type_descriptors = std::move(param_descriptors);
  cf.return_type_descriptor = source_type_descriptor(current_type);
  cf.return_adt_name = ret_adt_name;
  cf.extern_promise = node->extern_promise;
  // Prelude's Array primitives are observational runtime intrinsics. They
  // never consume the collection/string/array they inspect; recording that
  // contract here lets wrapper methods infer and export the same borrow
  // mask. Mutable/consuming runtime APIs remain on the normal callee-owns
  // path.
  cf.borrowed_params.assign(param_ctypes.size(), false);
  const bool is_array_observer = node->name.starts_with("primitiveSize") ||
                                 node->name.starts_with("primitiveGet");
  if (is_array_observer && !cf.borrowed_params.empty())
    cf.borrowed_params[0] = true;
  if (is_promise_extern)
    cf.promise_inner_type = ret_ctype;
  compiled_functions_[node->name] = cf;
  imports_.native_dependencies[node->name] = {c_sym,
                                              module_meta_from_compiled(cf)};
  named_values_[node->name] = {fn, CType::FUNCTION,
                               is_promise_extern
                                   ? std::vector<CType>{cf.promise_inner_type}
                                   : std::vector<CType>{}};

  // Compile the body (nullptr for module-level externs)
  if (node->body)
    return codegen(node->body);
  return {};
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
    // Bare collection annotations (`Seq`, `Set`, `Dict`) parse as named
    // types, while bracketed collection annotations use collection nodes.
    // Both spellings must retain their collection C ABI in `.yonai`.
    if (nt->name == "Seq")
      return CType::SEQ;
    if (nt->name == "Set")
      return CType::SET;
    if (nt->name == "Dict")
      return CType::DICT;
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

// Extract the ADT type name from a Yona type, if present.
// Returns the name (e.g., "Option", "Result") for NamedType, empty otherwise.
// Channel maps to CType::CHANNEL â€” not an ADT â€” so we exclude it.
static std::string yona_type_adt_name(const types::Type &t) {
  if (std::holds_alternative<std::shared_ptr<types::NamedType>>(t)) {
    auto &nt = std::get<std::shared_ptr<types::NamedType>>(t);
    if (nt->name == "Channel")
      return "";
    return nt->name;
  }
  if (std::holds_alternative<std::shared_ptr<types::RefinedType>>(t))
    return yona_type_adt_name(
        std::get<std::shared_ptr<types::RefinedType>>(t)->base_type);
  return "";
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

void Codegen::populate_interface_effect_rows(ast::ModuleDecl *mod,
                                             typechecker::TypeChecker &tc) {
  if (!mod || !mod->fqn)
    return;
  std::string fqn;
  if (mod->fqn->packageName.has_value()) {
    auto *pkg = mod->fqn->packageName.value();
    for (size_t i = 0; i < pkg->parts.size(); i++) {
      if (i > 0)
        fqn += "\\";
      fqn += pkg->parts[i]->value;
    }
    fqn += "\\";
  }
  fqn += mod->fqn->moduleName->value;

  // Sibling-aware: private helpers must be in scope while inferring exports
  // (same path compile_module uses for .yonai FN rows). Per-function check()
  // cannot see unexported names and reports a spurious E0104.
  tc.check_module(mod);

  std::unordered_set<std::string> export_set(mod->exports.begin(),
                                             mod->exports.end());
  for (auto *func : mod->functions) {
    if (!func || export_set.count(func->name) == 0)
      continue;
    auto *ty = tc.type_of(func);
    if (!ty)
      continue;
    auto row = tc.effect_row_info(tc.zonk(ty));
    std::string mangled = mangle_name(fqn, func->name);
    auto it = imports_.meta.find(mangled);
    if (it != imports_.meta.end()) {
      it->second.effect_ops = row.ops;
      it->second.effect_row_known = true;
      it->second.effect_open_rest = row.open_rest;
      it->second.effect_hof = row.hof;
      it->second.effect_scheme = tc.serialize_effect_scheme(tc.zonk(ty));
    }
    auto cf_it = compiled_functions_.find(func->name);
    if (cf_it != compiled_functions_.end()) {
      cf_it->second.effect_ops = row.ops;
      cf_it->second.effect_row_known = true;
      cf_it->second.effect_open_rest = row.open_rest;
      cf_it->second.effect_hof = row.hof;
      cf_it->second.effect_scheme = tc.serialize_effect_scheme(tc.zonk(ty));
    }
  }
}

} // namespace yona::compiler::codegen
