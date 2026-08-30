#include "yona/Interface/Writer.h"

#include "yona/Interface/Module.h"
#include "yona/Interface/Reader.h"

#include <algorithm>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace yona::interface {
namespace {

std::string_view functionKindName(FunctionKind Kind) {
  switch (Kind) {
  case FunctionKind::Synchronous:
    return "FN";
  case FunctionKind::ThreadPool:
    return "AFN";
  case FunctionKind::Io:
    return "IO";
  case FunctionKind::Native:
    return "NAT";
  }
  return "FN";
}

std::string mask(const std::vector<bool> &Values) {
  std::string Result;
  Result.reserve(Values.size());
  for (const bool Value : Values)
    Result.push_back(Value ? '1' : '0');
  return Result;
}

bool hasTrue(const std::vector<bool> &Values) {
  return std::find(Values.begin(), Values.end(), true) != Values.end();
}

void writeEffectRow(std::ostream &Output, const EffectRow &Effects) {
  if (Effects.IsKnown) {
    Output << " effects ";
    std::vector<std::string> Operations = Effects.Operations;
    std::sort(Operations.begin(), Operations.end());
    if (Operations.empty() && !Effects.IsOpen) {
      Output << '-';
    } else {
      for (std::size_t Index = 0; Index < Operations.size(); ++Index) {
        if (Index != 0)
          Output << ',';
        Output << Operations[Index];
      }
      if (Effects.IsOpen)
        Output << '|';
    }
  }
  if (Effects.IsHigherOrder)
    Output << " hof";
  if (!Effects.Scheme.empty())
    Output << " effectscheme " << Effects.Scheme;
}

void writeFunctionContract(std::ostream &Output, const Function &Function) {
  Output << functionKindName(Function.Kind) << ' '
         << Function.ParameterTypes.size();
  for (const std::string &Parameter : Function.ParameterTypes)
    Output << ' ' << Parameter;
  Output << " -> " << Function.ReturnType;
  if (hasTrue(Function.BorrowedParameters))
    Output << " borrow " << mask(Function.BorrowedParameters);
  if (!Function.TupleElementLinear.empty())
    Output << " tuple " << mask(Function.TupleElementLinear);
  writeEffectRow(Output, Function.Effects);
}

void writeFunction(std::ostream &Output, const Function &Function) {
  Output << functionKindName(Function.Kind) << ' ' << Function.Name << ' ';
  Output << Function.ParameterTypes.size();
  for (const std::string &Parameter : Function.ParameterTypes)
    Output << ' ' << Parameter;
  Output << " -> " << Function.ReturnType;
  if (hasTrue(Function.BorrowedParameters))
    Output << " borrow " << mask(Function.BorrowedParameters);
  if (!Function.TupleElementLinear.empty())
    Output << " tuple " << mask(Function.TupleElementLinear);
  writeEffectRow(Output, Function.Effects);
}

template <typename Value, typename Key>
std::vector<const Value *> sortedPointers(const std::vector<Value> &Values,
                                          Key KeyFunction) {
  std::vector<const Value *> Result;
  Result.reserve(Values.size());
  for (const Value &Item : Values)
    Result.push_back(&Item);
  std::sort(Result.begin(), Result.end(),
            [&](const Value *Left, const Value *Right) {
              return KeyFunction(*Left) < KeyFunction(*Right);
            });
  return Result;
}

void writeFields(std::ostream &Output, const std::vector<AdtField> &Fields) {
  if (Fields.empty())
    return;
  Output << " fields";
  for (const AdtField &Field : Fields)
    Output << ' ' << Field.Name << ':' << Field.Type;
}

std::string instanceKey(const TraitInstance &Instance) {
  std::string Result = Instance.TraitName;
  for (const std::string &Head : Instance.TypeHeads)
    Result += '\0' + Head;
  return Result;
}

std::string errorsToString(const std::vector<ParseError> &Errors) {
  std::ostringstream Result;
  Result << "invalid interface model";
  for (const ParseError &Error : Errors) {
    Result << "; ";
    if (Error.Line != 0)
      Result << "line " << Error.Line << ": ";
    Result << Error.Message;
  }
  return Result.str();
}

} // namespace

std::expected<std::string, std::string>
serializeModule(const InterfaceModule &Module) {
  std::ostringstream Output;
  Output << "MODULE " << Module.Identity.fqn() << '\n';

  const auto Adts =
      sortedPointers(Module.Adts, [](const Adt &Value) { return Value.Name; });
  for (const Adt *Value : Adts) {
    Output << "ADT " << Value->Name << ' ' << Value->VariantCount << ' '
           << Value->MaxArity;
    if (Value->IsRecursive)
      Output << " recursive";
    if (Value->IsOpaque)
      Output << " opaque";
    if (!Value->TypeParameters.empty()) {
      Output << " params";
      for (const std::string &Parameter : Value->TypeParameters)
        Output << ' ' << Parameter;
    }
    Output << '\n';
    const auto Constructors =
        sortedPointers(Value->Constructors, [](const Constructor &Constructor) {
          return std::pair(Constructor.Tag, Constructor.Name);
        });
    for (const Constructor *Constructor : Constructors) {
      Output << "  CTOR " << Constructor->Name << ' ' << Constructor->Tag << ' '
             << Constructor->Arity;
      writeFields(Output, Constructor->Fields);
      Output << '\n';
    }
  }

  const auto Traits = sortedPointers(
      Module.Traits, [](const Trait &Value) { return Value.Name; });
  for (const Trait *Value : Traits) {
    Output << "TRAIT " << Value->Name;
    for (const std::string &Parameter : Value->TypeParameters)
      Output << ' ' << Parameter;
    Output << ' ' << Value->MethodCount << '\n';
    const auto Supertypes =
        sortedPointers(Value->Supertypes, [](const TraitSupertype &Supertype) {
          return std::pair(Supertype.Name, Supertype.Parameter);
        });
    for (const TraitSupertype *Supertype : Supertypes)
      Output << "  SUPER " << Supertype->Name << ' ' << Supertype->Parameter
             << '\n';
    const auto Methods = sortedPointers(
        Value->Methods, [](const TraitMethod &Method) { return Method.Name; });
    for (const TraitMethod *Method : Methods)
      Output << "  METHOD " << Method->Name << ' ' << Method->Type << '\n';
  }

  const auto Instances =
      sortedPointers(Module.Instances, [](const TraitInstance &Instance) {
        return instanceKey(Instance);
      });
  for (const TraitInstance *Instance : Instances) {
    Output << "INSTANCE " << Instance->TraitName;
    for (const std::string &Head : Instance->TypeHeads)
      Output << ' ' << Head;
    Output << '\n';
    std::vector<std::string> Parameters = Instance->TypeParameters;
    std::sort(Parameters.begin(), Parameters.end());
    for (const std::string &Parameter : Parameters)
      Output << "  PARAM " << Parameter << '\n';
    const auto Constraints = sortedPointers(
        Instance->Constraints, [](const TraitConstraint &Constraint) {
          return std::pair(Constraint.TraitName, Constraint.Parameter);
        });
    for (const TraitConstraint *Constraint : Constraints)
      Output << "  CONSTRAINT " << Constraint->TraitName << ' '
             << Constraint->Parameter << '\n';
    const auto Implementations =
        sortedPointers(Instance->Implementations,
                       [](const TraitImplementation &Implementation) {
                         return Implementation.MethodName;
                       });
    for (const TraitImplementation *Implementation : Implementations) {
      Output << "  IMPL " << Implementation->MethodName << ' '
             << Implementation->Target.Module.fqn() << ' '
             << Implementation->Target.LocalName << '\n';
    }
  }

  const auto Functions = sortedPointers(
      Module.Functions, [](const Function &Function) { return Function.Name; });
  for (const Function *Function : Functions) {
    writeFunction(Output, *Function);
    Output << '\n';
  }

  const auto GenericFunctions = sortedPointers(
      Module.GenericFunctions,
      [](const GenericFunction &Function) { return Function.Name; });
  for (const GenericFunction *Generic : GenericFunctions) {
    const auto Dependencies = sortedPointers(
        Generic->Dependencies, [](const GenericDependency &Dependency) {
          return Dependency.Contract.Name;
        });
    for (const GenericDependency *Dependency : Dependencies) {
      Output << "GENFN_DEP " << Generic->Name << ' '
             << Dependency->Contract.Name << ' ';
      if (const auto *Native =
              std::get_if<NativeReference>(&Dependency->Target)) {
        Output << "NATIVE " << Native->Symbol << ' ';
      } else {
        const auto &Target = std::get<ExportReference>(Dependency->Target);
        Output << "YONA " << Target.Module.fqn() << ' ' << Target.LocalName
               << ' ';
      }
      writeFunctionContract(Output, Dependency->Contract);
      Output << '\n';
    }
    const auto Constructors = sortedPointers(
        Generic->Constructors, [](const GenericConstructor &Constructor) {
          return std::pair(Constructor.Tag, Constructor.Name);
        });
    for (const GenericConstructor *Constructor : Constructors) {
      Output << "GENFN_CTOR " << Generic->Name << ' ' << Constructor->Name
             << ' ' << Constructor->TypeName << ' ' << Constructor->Tag << ' '
             << Constructor->Arity << ' ' << Constructor->VariantCount << ' '
             << Constructor->MaxArity;
      if (Constructor->IsRecursive)
        Output << " recursive";
      writeFields(Output, Constructor->Fields);
      Output << '\n';
    }
    Output << "GENFN_BEGIN " << Generic->Name << ' ' << Generic->SourceName
           << '\n';
    Output << Generic->Source;
    if (!Generic->Source.empty() && Generic->Source.back() != '\n')
      Output << '\n';
    Output << "GENFN_END\n";
  }

  std::string Result = std::move(Output).str();
  ParseResult Validation = parseModule(Result);
  if (!Validation)
    return std::unexpected(errorsToString(Validation.error()));
  return Result;
}

std::expected<void, std::string> writeModule(const std::filesystem::path &Path,
                                             const InterfaceModule &Module) {
  auto Serialized = serializeModule(Module);
  if (!Serialized)
    return std::unexpected(std::move(Serialized.error()));
  std::ofstream Output(Path, std::ios::binary | std::ios::trunc);
  if (!Output)
    return std::unexpected("unable to open interface output '" + Path.string() +
                           "'");
  Output.write(Serialized->data(),
               static_cast<std::streamsize>(Serialized->size()));
  if (!Output)
    return std::unexpected("unable to write interface output '" +
                           Path.string() + "'");
  return {};
}

} // namespace yona::interface
