#include "yona/Semantics/InterfaceCatalog.h"

#include "yona/Interface/Module.h"
#include "yona/Model/ModuleIdentity.h"
#include "yona/Syntax/Ast.h"
#include "yona/Syntax/Parser.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace yona::semantics {
namespace {

std::vector<std::string_view>
splitDescriptorArguments(std::string_view Arguments) {
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

std::optional<std::string_view> unwrapDescriptor(std::string_view Value,
                                                 std::string_view Prefix) {
  if (!Value.starts_with(Prefix) || Value.size() <= Prefix.size() ||
      Value[Prefix.size()] != '(' || !Value.ends_with(')'))
    return std::nullopt;
  return Value.substr(Prefix.size() + 1, Value.size() - Prefix.size() - 2);
}

ast::FieldType fieldTypeFromDescriptor(std::string_view Descriptor) {
  if (const auto Inner = unwrapDescriptor(Descriptor, "LINEAR")) {
    auto Result = ast::FieldType::simple("Linear");
    Result.type_arguments.push_back(fieldTypeFromDescriptor(*Inner));
    return Result;
  }
  if (const auto Inner = unwrapDescriptor(Descriptor, "VAR"))
    return ast::FieldType::simple(std::string(*Inner));
  if (const auto Inner = unwrapDescriptor(Descriptor, "TUPLE")) {
    ast::FieldType Result;
    Result.is_tuple_type = true;
    for (const auto Part : splitDescriptorArguments(*Inner))
      Result.tuple_types.push_back(fieldTypeFromDescriptor(Part));
    return Result;
  }
  if (const auto Inner = unwrapDescriptor(Descriptor, "FUNCTION")) {
    const auto Parts = splitDescriptorArguments(*Inner);
    if (Parts.size() != 2)
      throw std::invalid_argument("FUNCTION descriptor requires two parts");
    ast::FieldType Result;
    Result.is_function_type = true;
    Result.param_types.push_back(fieldTypeFromDescriptor(Parts[0]));
    auto Return = fieldTypeFromDescriptor(Parts[1]);
    if (Return.is_function_type) {
      Result.param_types.insert(Result.param_types.end(),
                                Return.param_types.begin(),
                                Return.param_types.end());
      Result.return_types = std::move(Return.return_types);
    } else {
      Result.return_types.push_back(std::move(Return));
    }
    return Result;
  }

  auto Named = [&](std::string_view Prefix,
                   std::string_view Name) -> std::optional<ast::FieldType> {
    const auto Inner = unwrapDescriptor(Descriptor, Prefix);
    if (!Inner)
      return std::nullopt;
    auto Result = ast::FieldType::simple(std::string(Name));
    for (const auto Part : splitDescriptorArguments(*Inner))
      Result.type_arguments.push_back(fieldTypeFromDescriptor(Part));
    return Result;
  };
  if (const auto Result = Named("Seq", "Seq"))
    return *Result;
  if (const auto Result = Named("Set", "Set"))
    return *Result;
  if (const auto Result = Named("Dict", "Dict"))
    return *Result;
  if (const auto Result = Named("Promise", "Promise"))
    return *Result;
  if (const auto Result = Named("Channel", "Channel"))
    return *Result;
  if (const auto Inner = unwrapDescriptor(Descriptor, "ADT")) {
    const auto Parts = splitDescriptorArguments(*Inner);
    if (Parts.empty())
      throw std::invalid_argument("ADT descriptor requires a type name");
    auto Result = ast::FieldType::simple(std::string(Parts.front()));
    for (std::size_t Index = 1; Index < Parts.size(); ++Index)
      Result.type_arguments.push_back(fieldTypeFromDescriptor(Parts[Index]));
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
  throw std::invalid_argument("unknown canonical field descriptor: " +
                              std::string(Descriptor));
}

compiler::typechecker::ImportedFnSig
functionSignature(const interface::Function &Function) {
  compiler::typechecker::ImportedFnSig Result;
  Result.param_descriptors = Function.ParameterTypes;
  Result.return_descriptor = Function.ReturnType;
  Result.effect_scheme = Function.Effects.Scheme;
  return Result;
}

compiler::typechecker::ImportedFnSig
constructorSignature(const interface::Adt &Adt,
                     const interface::Constructor &Constructor) {
  compiler::typechecker::ImportedFnSig Result;
  Result.param_descriptors.reserve(Constructor.Fields.size());
  for (const auto &Field : Constructor.Fields)
    Result.param_descriptors.push_back(Field.Type);
  Result.return_descriptor = "ADT(" + Adt.Name;
  for (const auto &Parameter : Adt.TypeParameters)
    Result.return_descriptor += ",VAR(" + Parameter + ")";
  Result.return_descriptor += ")";
  return Result;
}

} // namespace

InterfaceCatalog::InterfaceCatalog(std::vector<std::string> SearchPathsValue)
    : SearchPaths(std::move(SearchPathsValue)) {}

void InterfaceCatalog::setSearchPaths(
    std::vector<std::string> SearchPathsValue) {
  SearchPaths = std::move(SearchPathsValue);
  Modules.clear();
}

void InterfaceCatalog::addSearchPath(std::string SearchPath) {
  if (SearchPath.empty() || std::find(SearchPaths.begin(), SearchPaths.end(),
                                      SearchPath) != SearchPaths.end())
    return;
  SearchPaths.push_back(std::move(SearchPath));
  Modules.clear();
}

void InterfaceCatalog::appendEnvironmentSearchPaths() {
  const char *Environment = std::getenv("YONA_PATH");
  if (!Environment || !*Environment)
    return;
#ifdef _WIN32
  constexpr char Separator = ';';
#else
  constexpr char Separator = ':';
#endif
  std::string Current;
  auto Flush = [&]() {
    if (Current.empty())
      return;
    std::error_code Error;
    const std::filesystem::path Path(Current);
    if (std::filesystem::is_directory(Path, Error)) {
      const auto Canonical = std::filesystem::weakly_canonical(Path, Error);
      if (!Error)
        addSearchPath(Canonical.string());
    }
    Current.clear();
  };
  for (const char Character : std::string_view(Environment)) {
    if (Character == Separator)
      Flush();
    else
      Current.push_back(Character);
  }
  Flush();
}

InterfaceCatalog::LoadResult
InterfaceCatalog::loadModule(std::string_view ModuleFqn) {
  const std::string Key(ModuleFqn);
  if (const auto Found = Modules.find(Key); Found != Modules.end())
    return Found->second ? &*Found->second : nullptr;

  std::optional<model::ModuleIdentity> Identity;
  try {
    Identity.emplace(Key);
  } catch (const std::invalid_argument &Error) {
    return std::unexpected(std::vector<interface::ParseError>{
        {0, 0, "invalid module identity: " + std::string(Error.what())}});
  }
  auto Loaded = interface::readModuleFromSearchPaths(SearchPaths, *Identity);
  if (!Loaded)
    return std::unexpected(std::move(Loaded.error()));
  if (!Loaded->has_value()) {
    Modules.emplace(Key, std::nullopt);
    return nullptr;
  }
  const auto Entry = Modules.emplace(Key, std::move(Loaded->value())).first;
  return &*Entry->second;
}

InterfaceCatalog::PreludeResult InterfaceCatalog::installPrelude(
    parser::Parser &Parser, compiler::typechecker::TypeChecker &TypeChecker) {
  auto Loaded = loadModule("Prelude");
  if (!Loaded)
    return std::unexpected(std::move(Loaded.error()));
  if (!*Loaded)
    return false;

  const auto &Prelude = **Loaded;
  for (const auto &Adt : Prelude.Adts) {
    std::vector<std::pair<std::string, int>> Constructors;
    std::vector<std::vector<ast::FieldType>> FieldTypes;
    std::vector<std::vector<std::string>> FieldNames;
    Constructors.reserve(Adt.Constructors.size());
    FieldTypes.reserve(Adt.Constructors.size());
    FieldNames.reserve(Adt.Constructors.size());
    for (const auto &Constructor : Adt.Constructors) {
      Constructors.emplace_back(Constructor.Name,
                                static_cast<int>(Constructor.Arity));
      std::vector<ast::FieldType> ConstructorTypes;
      std::vector<std::string> ConstructorNames;
      ConstructorTypes.reserve(Constructor.Fields.size());
      ConstructorNames.reserve(Constructor.Fields.size());
      for (const auto &Field : Constructor.Fields) {
        ConstructorTypes.push_back(fieldTypeFromDescriptor(Field.Type));
        ConstructorNames.push_back(Field.Name);
      }
      FieldTypes.push_back(std::move(ConstructorTypes));
      FieldNames.push_back(std::move(ConstructorNames));
      Parser.register_constructor(
          Constructor.Name, Adt.Name, static_cast<int>(Constructor.Tag),
          static_cast<int>(Constructor.Arity), FieldNames.back());
    }
    TypeChecker.register_adt(Adt.Name, Adt.TypeParameters, Constructors,
                             FieldTypes, FieldNames);
  }

  for (const auto &Function : Prelude.Functions)
    TypeChecker.register_interface_function(Function);

  for (const auto &Trait : Prelude.Traits) {
    TypeChecker.register_trait(Trait.Name, Trait.TypeParameters);
    for (const auto &Supertype : Trait.Supertypes)
      TypeChecker.register_trait_superclass(Trait.Name, Supertype.Name);
    for (const auto &Method : Trait.Methods)
      TypeChecker.register_trait_method_descriptor(Trait.Name, Method.Name,
                                                   Method.Type);
  }

  for (const auto &Instance : Prelude.Instances) {
    std::vector<std::pair<std::string, std::string>> Constraints;
    Constraints.reserve(Instance.Constraints.size());
    for (const auto &Constraint : Instance.Constraints)
      Constraints.emplace_back(Constraint.TraitName, Constraint.Parameter);
    TypeChecker.register_instance(Instance.TraitName, Instance.TypeHeads,
                                  Instance.TypeParameters,
                                  std::move(Constraints));
  }

  auto &Arena = TypeChecker.arena();
  auto *Argument = Arena.fresh_var(0);
  TypeChecker.register_builtin_function(
      "typeOf", Arena.make_arrow(Argument, Arena.make_app("Type", {})));

  auto *Unit = Arena.make_con(compiler::typechecker::TyCon::Unit);
  auto *Integer = Arena.make_con(compiler::typechecker::TyCon::Int);
  TypeChecker.register_effect("Gpu", "",
                              {{"oom", {Unit}, Unit},
                               {"deviceLost", {Unit}, Unit},
                               {"fail", {Integer}, Unit}});
  return true;
}

std::optional<compiler::typechecker::ImportedFnSig>
InterfaceCatalog::imported_function_sig(const std::string &ModuleFqn,
                                        const std::string &Name) {
  auto Loaded = loadModule(ModuleFqn);
  if (!Loaded || !*Loaded)
    return std::nullopt;
  if (const auto *Function = interface::findFunction(**Loaded, Name))
    return functionSignature(*Function);
  for (const auto &Adt : (**Loaded).Adts) {
    if (Adt.IsOpaque)
      continue;
    for (const auto &Constructor : Adt.Constructors)
      if (Constructor.Name == Name)
        return constructorSignature(Adt, Constructor);
  }
  return std::nullopt;
}

std::vector<std::string>
InterfaceCatalog::imported_module_exports(const std::string &ModuleFqn) {
  auto Loaded = loadModule(ModuleFqn);
  if (!Loaded || !*Loaded)
    return {};
  std::vector<std::string> Result;
  Result.reserve((**Loaded).Functions.size());
  for (const auto &Function : (**Loaded).Functions)
    Result.push_back(Function.Name);
  for (const auto &Adt : (**Loaded).Adts) {
    if (Adt.IsOpaque)
      continue;
    for (const auto &Constructor : Adt.Constructors)
      Result.push_back(Constructor.Name);
  }
  return Result;
}

std::vector<compiler::typechecker::ImportedInstanceSig>
InterfaceCatalog::imported_instances(const std::string &ModuleFqn) {
  auto Loaded = loadModule(ModuleFqn);
  if (!Loaded || !*Loaded)
    return {};
  std::vector<compiler::typechecker::ImportedInstanceSig> Result;
  Result.reserve((**Loaded).Instances.size());
  for (const auto &Source : (**Loaded).Instances) {
    compiler::typechecker::ImportedInstanceSig Instance;
    Instance.trait_name = Source.TraitName;
    Instance.type_names = Source.TypeHeads;
    Instance.type_params = Source.TypeParameters;
    for (const auto &Constraint : Source.Constraints)
      Instance.constraints.emplace_back(Constraint.TraitName,
                                        Constraint.Parameter);
    Result.push_back(std::move(Instance));
  }
  return Result;
}

} // namespace yona::semantics
