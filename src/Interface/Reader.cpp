#include "yona/Interface/Reader.h"

#include "yona/Interface/Module.h"
#include "yona/Model/ModuleIdentity.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace yona::interface {
namespace {

using Tokens = std::vector<std::string_view>;

Tokens split(std::string_view Line) {
  Tokens Result;
  std::size_t Position = 0;
  while (Position < Line.size()) {
    while (Position < Line.size() &&
           std::isspace(static_cast<unsigned char>(Line[Position])))
      ++Position;
    if (Position == Line.size())
      break;
    const std::size_t Start = Position;
    while (Position < Line.size() &&
           !std::isspace(static_cast<unsigned char>(Line[Position])))
      ++Position;
    Result.push_back(Line.substr(Start, Position - Start));
  }
  return Result;
}

std::vector<std::string_view> lines(std::string_view Input) {
  std::vector<std::string_view> Result;
  std::size_t Start = 0;
  while (Start <= Input.size()) {
    const std::size_t End = Input.find('\n', Start);
    std::string_view Line =
        Input.substr(Start, End == std::string_view::npos ? Input.size() - Start
                                                          : End - Start);
    if (!Line.empty() && Line.back() == '\r')
      Line.remove_suffix(1);
    Result.push_back(Line);
    if (End == std::string_view::npos)
      break;
    Start = End + 1;
  }
  return Result;
}

bool isIdentifier(std::string_view Value) {
  if (Value.empty() || !std::isalpha(static_cast<unsigned char>(Value.front())))
    return false;
  return std::all_of(Value.begin() + 1, Value.end(), [](char Character) {
    return std::isalnum(static_cast<unsigned char>(Character));
  });
}

bool isPascalName(std::string_view Value) {
  return isIdentifier(Value) &&
         std::isupper(static_cast<unsigned char>(Value.front()));
}

bool isTypeParameter(std::string_view Value) {
  return isIdentifier(Value) &&
         std::islower(static_cast<unsigned char>(Value.front()));
}

bool isLocalName(std::string_view Value) {
  if (Value.empty() || Value.starts_with("Yona") ||
      !std::isalpha(static_cast<unsigned char>(Value.front())))
    return false;
  const bool HasUnderscore = Value.contains('_');
  if (std::isupper(static_cast<unsigned char>(Value.front())) && !HasUnderscore)
    return false;
  if (std::islower(static_cast<unsigned char>(Value.front())) && HasUnderscore)
    return false;
  return std::all_of(Value.begin() + 1, Value.end(), [](char Character) {
    return std::isalnum(static_cast<unsigned char>(Character)) ||
           Character == '_';
  });
}

bool isFieldName(std::string_view Value) {
  if (Value.empty())
    return false;
  if (!std::islower(static_cast<unsigned char>(Value.front())) &&
      Value.front() != '_')
    return false;
  return std::all_of(Value.begin() + 1, Value.end(), [](char Character) {
    return std::isalnum(static_cast<unsigned char>(Character));
  });
}

bool isNativeSymbol(std::string_view Value) {
  if (!Value.starts_with("Yona") || Value.size() == 4 ||
      !std::isupper(static_cast<unsigned char>(Value[4])))
    return false;
  return std::all_of(Value.begin() + 4, Value.end(), [](char Character) {
    return std::isalnum(static_cast<unsigned char>(Character));
  });
}

bool isTypeDescriptor(std::string_view Value) {
  if (Value.empty() || Value == "->")
    return false;
  int Depth = 0;
  for (const unsigned char Character : Value) {
    if (std::isspace(Character) || std::iscntrl(Character))
      return false;
    if (Character == '(')
      ++Depth;
    else if (Character == ')' && --Depth < 0)
      return false;
  }
  return Depth == 0;
}

bool isEffectOperation(std::string_view Value) {
  if (Value.empty())
    return false;
  std::size_t Start = 0;
  while (Start <= Value.size()) {
    const std::size_t End = Value.find('.', Start);
    const std::string_view Part =
        Value.substr(Start, End == std::string_view::npos ? Value.size() - Start
                                                          : End - Start);
    if (!isIdentifier(Part))
      return false;
    if (End == std::string_view::npos)
      break;
    Start = End + 1;
  }
  return true;
}

bool parseSize(std::string_view Value, std::size_t &Result) {
  if (Value.empty())
    return false;
  const char *Begin = Value.data();
  const char *End = Begin + Value.size();
  const auto Parsed = std::from_chars(Begin, End, Result);
  return Parsed.ec == std::errc{} && Parsed.ptr == End;
}

bool parseMask(std::string_view Value, std::size_t Expected,
               std::vector<bool> &Result) {
  if (Value.size() != Expected)
    return false;
  Result.clear();
  Result.reserve(Expected);
  for (const char Character : Value) {
    if (Character != '0' && Character != '1')
      return false;
    Result.push_back(Character == '1');
  }
  return true;
}

std::optional<FunctionKind> functionKind(std::string_view Value) {
  if (Value == "FN")
    return FunctionKind::Synchronous;
  if (Value == "AFN")
    return FunctionKind::ThreadPool;
  if (Value == "IO")
    return FunctionKind::Io;
  if (Value == "NAT")
    return FunctionKind::Native;
  return std::nullopt;
}

template <typename T, typename Name>
bool containsNamed(const std::vector<T> &Values, const Name &Value) {
  return std::any_of(Values.begin(), Values.end(),
                     [&](const T &Item) { return Item.Name == Value; });
}

class InterfaceParser final {
public:
  explicit InterfaceParser(std::string_view Input) : Lines(lines(Input)) {}

  ParseResult parse() {
    for (std::size_t Index = 0; Index < Lines.size(); ++Index) {
      const Tokens LineTokens = split(Lines[Index]);
      if (LineTokens.empty())
        continue;
      const std::size_t LineNumber = Index + 1;
      const std::string_view Keyword = LineTokens.front();
      if (!SawRecord) {
        SawRecord = true;
        if (Keyword != "MODULE")
          error(LineNumber, "MODULE must be the first record");
      }
      if (Keyword == "MODULE") {
        parseModuleRecord(LineTokens, LineNumber);
        clearContexts();
        continue;
      }
      if (!Module) {
        error(LineNumber, "record appears before a valid MODULE record");
        continue;
      }

      if (Keyword != "CTOR")
        CurrentAdt.reset();
      if (Keyword != "SUPER" && Keyword != "METHOD")
        CurrentTrait.reset();
      if (Keyword != "PARAM" && Keyword != "CONSTRAINT" && Keyword != "IMPL")
        CurrentInstance.reset();

      if (Keyword == "ADT")
        parseAdt(LineTokens, LineNumber);
      else if (Keyword == "CTOR")
        parseConstructor(LineTokens, LineNumber);
      else if (Keyword == "TRAIT")
        parseTrait(LineTokens, LineNumber);
      else if (Keyword == "SUPER")
        parseSupertype(LineTokens, LineNumber);
      else if (Keyword == "METHOD")
        parseMethod(LineTokens, LineNumber);
      else if (Keyword == "INSTANCE")
        parseInstance(LineTokens, LineNumber);
      else if (Keyword == "PARAM")
        parseInstanceParameter(LineTokens, LineNumber);
      else if (Keyword == "CONSTRAINT")
        parseConstraint(LineTokens, LineNumber);
      else if (Keyword == "IMPL")
        parseImplementation(LineTokens, LineNumber);
      else if (functionKind(Keyword))
        parseFunction(LineTokens, LineNumber);
      else if (Keyword == "GENFN_DEP")
        parseGenericDependency(LineTokens, LineNumber);
      else if (Keyword == "GENFN_CTOR")
        parseGenericConstructor(LineTokens, LineNumber);
      else if (Keyword == "GENFN_BEGIN")
        parseGenericSource(LineTokens, LineNumber, Index);
      else if (Keyword == "GENFN_END")
        error(LineNumber, "GENFN_END does not have a matching GENFN_BEGIN");
      else
        error(LineNumber,
              "unknown interface record '" + std::string(Keyword) + "'");
    }

    validateStructure();
    if (!Errors.empty())
      return std::unexpected(std::move(Errors));
    return std::move(*Module);
  }

private:
  void error(std::size_t Line, std::string Message) {
    Errors.push_back(ParseError{Line, 1, std::move(Message)});
  }

  std::optional<model::ModuleIdentity> identity(std::string_view Value,
                                                std::size_t Line) {
    try {
      model::ModuleIdentity Result(Value);
      if (Result.fqn() != Value) {
        error(Line, "module identity is not in canonical FQN form");
        return std::nullopt;
      }
      return Result;
    } catch (const std::invalid_argument &Exception) {
      error(Line, std::string("invalid module identity: ") + Exception.what());
      return std::nullopt;
    }
  }

  void clearContexts() {
    CurrentAdt.reset();
    CurrentTrait.reset();
    CurrentInstance.reset();
  }

  void parseModuleRecord(const Tokens &LineTokens, std::size_t Line) {
    if (SawModule) {
      error(Line, "duplicate MODULE record");
      return;
    }
    SawModule = true;
    if (LineTokens.size() != 2) {
      error(Line, "MODULE requires exactly one module FQN");
      return;
    }
    auto Identity = identity(LineTokens[1], Line);
    if (Identity)
      Module.emplace(std::move(*Identity));
  }

  void parseAdt(const Tokens &LineTokens, std::size_t Line) {
    if (LineTokens.size() < 4) {
      error(Line, "ADT requires a name, variant count, and maximum arity");
      return;
    }
    Adt Value;
    Value.Name = LineTokens[1];
    if (!isPascalName(Value.Name))
      error(Line, "ADT name must be PascalCase");
    if (!parseSize(LineTokens[2], Value.VariantCount) ||
        Value.VariantCount == 0)
      error(Line, "ADT variant count must be a positive integer");
    if (!parseSize(LineTokens[3], Value.MaxArity))
      error(Line, "ADT maximum arity must be a non-negative integer");
    bool SawRecursive = false;
    bool SawOpaque = false;
    bool ReadingParameters = false;
    for (std::size_t Index = 4; Index < LineTokens.size(); ++Index) {
      const std::string_view Token = LineTokens[Index];
      if (!ReadingParameters && Token == "recursive") {
        if (SawRecursive)
          error(Line, "duplicate ADT recursive flag");
        SawRecursive = Value.IsRecursive = true;
      } else if (!ReadingParameters && Token == "opaque") {
        if (SawOpaque)
          error(Line, "duplicate ADT opaque flag");
        SawOpaque = Value.IsOpaque = true;
      } else if (!ReadingParameters && Token == "params") {
        ReadingParameters = true;
      } else if (ReadingParameters && isTypeParameter(Token)) {
        const std::string Parameter(Token);
        if (std::find(Value.TypeParameters.begin(), Value.TypeParameters.end(),
                      Parameter) != Value.TypeParameters.end())
          error(Line, "duplicate ADT type parameter");
        else
          Value.TypeParameters.push_back(Parameter);
      } else {
        error(Line, "invalid ADT attribute '" + std::string(Token) + "'");
      }
    }
    if (ReadingParameters && Value.TypeParameters.empty())
      error(Line, "ADT params must contain at least one type parameter");
    if (containsNamed(Module->Adts, Value.Name)) {
      error(Line, "duplicate ADT '" + Value.Name + "'");
      return;
    }
    Module->Adts.push_back(std::move(Value));
    CurrentAdt = Module->Adts.size() - 1;
  }

  bool parseFields(const Tokens &LineTokens, std::size_t Start,
                   std::size_t Arity, std::vector<AdtField> &Fields,
                   std::size_t Line) {
    if (Arity == 0) {
      if (Start != LineTokens.size()) {
        error(Line, "zero-arity constructor cannot declare fields");
        return false;
      }
      return true;
    }
    if (Start >= LineTokens.size() || LineTokens[Start] != "fields") {
      error(Line, "constructor fields must be declared explicitly");
      return false;
    }
    ++Start;
    for (; Start < LineTokens.size(); ++Start) {
      const std::string_view Token = LineTokens[Start];
      const std::size_t Separator = Token.find(':');
      if (Separator == std::string_view::npos) {
        error(Line, "constructor field must use name:type");
        continue;
      }
      AdtField Field{std::string(Token.substr(0, Separator)),
                     std::string(Token.substr(Separator + 1))};
      if (!isFieldName(Field.Name))
        error(Line, "constructor field name must be camelCase");
      if (!isTypeDescriptor(Field.Type))
        error(Line, "invalid constructor field type descriptor");
      if (containsNamed(Fields, Field.Name))
        error(Line, "duplicate constructor field '" + Field.Name + "'");
      else
        Fields.push_back(std::move(Field));
    }
    if (Fields.size() != Arity) {
      error(Line, "constructor field count does not match its arity");
      return false;
    }
    return true;
  }

  void parseConstructor(const Tokens &LineTokens, std::size_t Line) {
    if (!CurrentAdt) {
      error(Line, "CTOR must immediately follow its ADT");
      return;
    }
    if (LineTokens.size() < 4) {
      error(Line, "CTOR requires a name, tag, and arity");
      return;
    }
    Constructor Value;
    Value.Name = LineTokens[1];
    if (!isPascalName(Value.Name))
      error(Line, "constructor name must be PascalCase");
    if (!parseSize(LineTokens[2], Value.Tag))
      error(Line, "constructor tag must be a non-negative integer");
    if (!parseSize(LineTokens[3], Value.Arity))
      error(Line, "constructor arity must be a non-negative integer");
    parseFields(LineTokens, 4, Value.Arity, Value.Fields, Line);
    Adt &Owner = Module->Adts[*CurrentAdt];
    if (Owner.IsOpaque)
      error(Line, "opaque ADT cannot expose constructors");
    if (containsNamed(Owner.Constructors, Value.Name) ||
        std::any_of(Module->Adts.begin(), Module->Adts.end(),
                    [&](const Adt &Other) {
                      return containsNamed(Other.Constructors, Value.Name);
                    })) {
      error(Line, "duplicate constructor '" + Value.Name + "'");
      return;
    }
    Owner.Constructors.push_back(std::move(Value));
  }

  void parseTrait(const Tokens &LineTokens, std::size_t Line) {
    if (LineTokens.size() < 3) {
      error(Line, "TRAIT requires a name and method count");
      return;
    }
    Trait Value;
    Value.Name = LineTokens[1];
    if (!isPascalName(Value.Name))
      error(Line, "trait name must be PascalCase");
    if (!parseSize(LineTokens.back(), Value.MethodCount))
      error(Line, "trait method count must be a non-negative integer");
    for (std::size_t Index = 2; Index + 1 < LineTokens.size(); ++Index) {
      if (!isTypeParameter(LineTokens[Index]))
        error(Line, "trait type parameters must be camelCase identifiers");
      const std::string Parameter(LineTokens[Index]);
      if (std::find(Value.TypeParameters.begin(), Value.TypeParameters.end(),
                    Parameter) != Value.TypeParameters.end())
        error(Line, "duplicate trait type parameter");
      else
        Value.TypeParameters.push_back(Parameter);
    }
    if (containsNamed(Module->Traits, Value.Name)) {
      error(Line, "duplicate trait '" + Value.Name + "'");
      return;
    }
    Module->Traits.push_back(std::move(Value));
    CurrentTrait = Module->Traits.size() - 1;
  }

  void parseSupertype(const Tokens &LineTokens, std::size_t Line) {
    if (!CurrentTrait) {
      error(Line, "SUPER must immediately follow its TRAIT");
      return;
    }
    if (LineTokens.size() != 3 || !isPascalName(LineTokens[1]) ||
        !isTypeParameter(LineTokens[2])) {
      error(Line, "SUPER requires a PascalCase trait and type parameter");
      return;
    }
    Trait &Owner = Module->Traits[*CurrentTrait];
    const TraitSupertype Value{std::string(LineTokens[1]),
                               std::string(LineTokens[2])};
    if (std::any_of(Owner.Supertypes.begin(), Owner.Supertypes.end(),
                    [&](const TraitSupertype &Other) {
                      return Other.Name == Value.Name &&
                             Other.Parameter == Value.Parameter;
                    }))
      error(Line, "duplicate SUPER record");
    else
      Owner.Supertypes.push_back(Value);
  }

  void parseMethod(const Tokens &LineTokens, std::size_t Line) {
    if (!CurrentTrait) {
      error(Line, "METHOD must immediately follow its TRAIT");
      return;
    }
    if (LineTokens.size() != 3 || !isLocalName(LineTokens[1]) ||
        !isTypeDescriptor(LineTokens[2])) {
      error(Line, "METHOD requires a local name and type descriptor");
      return;
    }
    Trait &Owner = Module->Traits[*CurrentTrait];
    TraitMethod Value{std::string(LineTokens[1]), std::string(LineTokens[2])};
    if (containsNamed(Owner.Methods, Value.Name))
      error(Line, "duplicate trait method '" + Value.Name + "'");
    else
      Owner.Methods.push_back(std::move(Value));
  }

  void parseInstance(const Tokens &LineTokens, std::size_t Line) {
    if (LineTokens.size() < 3 || !isPascalName(LineTokens[1])) {
      error(Line, "INSTANCE requires a trait and at least one type head");
      return;
    }
    TraitInstance Value;
    Value.TraitName = LineTokens[1];
    for (std::size_t Index = 2; Index < LineTokens.size(); ++Index) {
      if (!isTypeDescriptor(LineTokens[Index]))
        error(Line, "invalid INSTANCE type descriptor");
      Value.TypeHeads.emplace_back(LineTokens[Index]);
    }
    const auto Duplicate =
        std::find_if(Module->Instances.begin(), Module->Instances.end(),
                     [&](const TraitInstance &Other) {
                       return Other.TraitName == Value.TraitName &&
                              Other.TypeHeads == Value.TypeHeads;
                     });
    if (Duplicate != Module->Instances.end()) {
      error(Line, "duplicate trait INSTANCE");
      return;
    }
    Module->Instances.push_back(std::move(Value));
    CurrentInstance = Module->Instances.size() - 1;
  }

  void parseInstanceParameter(const Tokens &LineTokens, std::size_t Line) {
    if (!CurrentInstance) {
      error(Line, "PARAM must immediately follow its INSTANCE");
      return;
    }
    if (LineTokens.size() != 2 || !isTypeParameter(LineTokens[1])) {
      error(Line, "PARAM requires one type parameter");
      return;
    }
    auto &Parameters = Module->Instances[*CurrentInstance].TypeParameters;
    const std::string Parameter(LineTokens[1]);
    if (std::find(Parameters.begin(), Parameters.end(), Parameter) !=
        Parameters.end())
      error(Line, "duplicate instance PARAM");
    else
      Parameters.push_back(Parameter);
  }

  void parseConstraint(const Tokens &LineTokens, std::size_t Line) {
    if (!CurrentInstance) {
      error(Line, "CONSTRAINT must immediately follow its INSTANCE");
      return;
    }
    if (LineTokens.size() != 3 || !isPascalName(LineTokens[1]) ||
        !isTypeParameter(LineTokens[2])) {
      error(Line, "CONSTRAINT requires a trait and type parameter");
      return;
    }
    TraitConstraint Value{std::string(LineTokens[1]),
                          std::string(LineTokens[2])};
    auto &Constraints = Module->Instances[*CurrentInstance].Constraints;
    if (std::any_of(Constraints.begin(), Constraints.end(),
                    [&](const TraitConstraint &Other) {
                      return Other.TraitName == Value.TraitName &&
                             Other.Parameter == Value.Parameter;
                    }))
      error(Line, "duplicate instance CONSTRAINT");
    else
      Constraints.push_back(std::move(Value));
  }

  void parseImplementation(const Tokens &LineTokens, std::size_t Line) {
    if (!CurrentInstance) {
      error(Line, "IMPL must immediately follow its INSTANCE");
      return;
    }
    if (LineTokens.size() != 4 || !isLocalName(LineTokens[1]) ||
        !isLocalName(LineTokens[3])) {
      error(Line, "IMPL requires a method, module FQN, and local target key");
      return;
    }
    auto TargetModule = identity(LineTokens[2], Line);
    if (!TargetModule)
      return;
    auto &Implementations = Module->Instances[*CurrentInstance].Implementations;
    const std::string MethodName(LineTokens[1]);
    if (std::any_of(Implementations.begin(), Implementations.end(),
                    [&](const TraitImplementation &Implementation) {
                      return Implementation.MethodName == MethodName;
                    })) {
      error(Line, "duplicate implementation for method '" + MethodName + "'");
      return;
    }
    Implementations.emplace_back(
        MethodName,
        ExportReference(std::move(*TargetModule), std::string(LineTokens[3])));
  }

  bool parseEffects(std::string_view Token, EffectRow &Effects,
                    std::size_t Line) {
    Effects.IsKnown = true;
    if (Token == "-")
      return true;
    if (Token == "|") {
      Effects.IsOpen = true;
      return true;
    }
    if (Token.ends_with('|')) {
      Effects.IsOpen = true;
      Token.remove_suffix(1);
    }
    if (Token.empty()) {
      error(Line, "effect row contains an empty operation");
      return false;
    }
    std::size_t Start = 0;
    while (Start <= Token.size()) {
      const std::size_t End = Token.find(',', Start);
      const std::string_view Operation = Token.substr(
          Start,
          End == std::string_view::npos ? Token.size() - Start : End - Start);
      if (!isEffectOperation(Operation))
        error(Line,
              "invalid effect operation '" + std::string(Operation) + "'");
      const std::string Owned(Operation);
      if (std::find(Effects.Operations.begin(), Effects.Operations.end(),
                    Owned) != Effects.Operations.end())
        error(Line, "duplicate effect operation '" + Owned + "'");
      else
        Effects.Operations.push_back(Owned);
      if (End == std::string_view::npos)
        break;
      Start = End + 1;
    }
    return true;
  }

  bool parseFunctionTail(const Tokens &LineTokens, std::size_t ArityIndex,
                         Function &Value, std::size_t Line) {
    if (ArityIndex >= LineTokens.size()) {
      error(Line, "function contract is missing its arity");
      return false;
    }
    std::size_t Arity = 0;
    if (!parseSize(LineTokens[ArityIndex], Arity)) {
      error(Line, "function arity must be a non-negative integer");
      return false;
    }
    std::size_t Index = ArityIndex + 1;
    if (Index + Arity + 1 >= LineTokens.size()) {
      error(Line, "function contract has fewer types than its arity");
      return false;
    }
    for (std::size_t Parameter = 0; Parameter < Arity; ++Parameter, ++Index) {
      if (!isTypeDescriptor(LineTokens[Index]))
        error(Line, "invalid function parameter type descriptor");
      Value.ParameterTypes.emplace_back(LineTokens[Index]);
    }
    if (LineTokens[Index] != "->") {
      error(Line, "function contract is missing '->'");
      return false;
    }
    ++Index;
    if (Index >= LineTokens.size() || !isTypeDescriptor(LineTokens[Index])) {
      error(Line, "function contract has an invalid return type");
      return false;
    }
    Value.ReturnType = LineTokens[Index++];
    Value.BorrowedParameters.assign(Arity, false);
    bool SawBorrow = false;
    bool SawTuple = false;
    bool SawEffects = false;
    bool SawHigherOrder = false;
    bool SawScheme = false;
    while (Index < LineTokens.size()) {
      const std::string_view Attribute = LineTokens[Index++];
      if (Attribute == "borrow") {
        if (SawBorrow || Index >= LineTokens.size() ||
            !parseMask(LineTokens[Index], Arity, Value.BorrowedParameters)) {
          error(Line, "borrow requires one unique mask matching the arity");
          return false;
        }
        SawBorrow = true;
        ++Index;
      } else if (Attribute == "tuple") {
        if (SawTuple || Index >= LineTokens.size() ||
            !parseMask(LineTokens[Index], LineTokens[Index].size(),
                       Value.TupleElementLinear) ||
            Value.TupleElementLinear.empty()) {
          error(Line, "tuple requires one non-empty 0/1 mask");
          return false;
        }
        SawTuple = true;
        ++Index;
      } else if (Attribute == "effects") {
        if (SawEffects || Index >= LineTokens.size()) {
          error(Line, "effects requires one unique effect row");
          return false;
        }
        SawEffects = true;
        parseEffects(LineTokens[Index++], Value.Effects, Line);
      } else if (Attribute == "hof") {
        if (SawHigherOrder) {
          error(Line, "duplicate hof attribute");
          return false;
        }
        SawHigherOrder = Value.Effects.IsHigherOrder = true;
      } else if (Attribute == "effectscheme") {
        if (SawScheme || Index >= LineTokens.size() ||
            !isTypeDescriptor(LineTokens[Index])) {
          error(Line, "effectscheme requires one unique token");
          return false;
        }
        SawScheme = true;
        Value.Effects.Scheme = LineTokens[Index++];
      } else {
        error(Line,
              "unknown function attribute '" + std::string(Attribute) + "'");
        return false;
      }
    }
    if (SawBorrow && (Value.Kind == FunctionKind::ThreadPool ||
                      Value.Kind == FunctionKind::Native)) {
      error(Line,
            "task-backed function contracts cannot borrow parameters without "
            "task-owned lifetime pins");
      return false;
    }
    if (Value.Effects.IsHigherOrder && !Value.Effects.IsKnown)
      error(Line, "hof requires an explicit effects row");
    return true;
  }

  void parseFunction(const Tokens &LineTokens, std::size_t Line) {
    if (LineTokens.size() < 5) {
      error(Line, "function row is incomplete");
      return;
    }
    Function Value;
    const auto Kind = functionKind(LineTokens[0]);
    if (!Kind) {
      error(Line, "function row has an invalid function kind");
      return;
    }
    Value.Kind = *Kind;
    Value.Name = LineTokens[1];
    if (!isLocalName(Value.Name))
      error(Line, "function row must be keyed by a local Yona symbol");
    if (!parseFunctionTail(LineTokens, 2, Value, Line))
      return;
    if (containsNamed(Module->Functions, Value.Name)) {
      error(Line, "duplicate function '" + Value.Name + "'");
      return;
    }
    Module->Functions.push_back(std::move(Value));
  }

  GenericFunction &generic(std::string_view Owner) {
    auto [Iterator, Inserted] = Generics.try_emplace(std::string(Owner));
    if (Inserted)
      Iterator->second.Name = Owner;
    return Iterator->second;
  }

  void parseGenericDependency(const Tokens &LineTokens, std::size_t Line) {
    if (LineTokens.size() < 8 || !isLocalName(LineTokens[1]) ||
        !isLocalName(LineTokens[2])) {
      error(Line, "GENFN_DEP requires local owner and binding keys");
      return;
    }
    Function Contract;
    Contract.Name = LineTokens[2];
    std::size_t KindIndex = 0;
    std::optional<DependencyTarget> Target;
    if (LineTokens[3] == "NATIVE") {
      if (!isNativeSymbol(LineTokens[4])) {
        error(Line,
              "GENFN_DEP NATIVE target must be a canonical Yona ABI symbol");
        return;
      }
      Target.emplace(NativeReference{std::string(LineTokens[4])});
      KindIndex = 5;
    } else if (LineTokens[3] == "YONA") {
      if (LineTokens.size() < 9 || !isLocalName(LineTokens[5])) {
        error(Line, "GENFN_DEP YONA requires a module and local key");
        return;
      }
      auto TargetModule = identity(LineTokens[4], Line);
      if (!TargetModule)
        return;
      Target.emplace(ExportReference(std::move(*TargetModule),
                                     std::string(LineTokens[5])));
      KindIndex = 6;
    } else {
      error(Line, "GENFN_DEP target kind must be NATIVE or YONA");
      return;
    }
    const auto Kind = KindIndex < LineTokens.size()
                          ? functionKind(LineTokens[KindIndex])
                          : std::nullopt;
    if (!Kind) {
      error(Line, "GENFN_DEP is missing a function kind");
      return;
    }
    Contract.Kind = *Kind;
    if (!parseFunctionTail(LineTokens, KindIndex + 1, Contract, Line))
      return;
    GenericFunction &Owner = generic(LineTokens[1]);
    if (std::any_of(Owner.Dependencies.begin(), Owner.Dependencies.end(),
                    [&](const GenericDependency &Dependency) {
                      return Dependency.Contract.Name == Contract.Name;
                    })) {
      error(Line, "duplicate GENFN_DEP binding '" + Contract.Name + "'");
      return;
    }
    Owner.Dependencies.emplace_back(std::move(Contract), std::move(*Target));
  }

  void parseGenericConstructor(const Tokens &LineTokens, std::size_t Line) {
    if (LineTokens.size() < 8 || !isLocalName(LineTokens[1]) ||
        !isPascalName(LineTokens[2]) || !isPascalName(LineTokens[3])) {
      error(Line, "GENFN_CTOR has an invalid owner, constructor, or type");
      return;
    }
    GenericConstructor Value;
    Value.Name = LineTokens[2];
    Value.TypeName = LineTokens[3];
    if (!parseSize(LineTokens[4], Value.Tag) ||
        !parseSize(LineTokens[5], Value.Arity) ||
        !parseSize(LineTokens[6], Value.VariantCount) ||
        Value.VariantCount == 0 || !parseSize(LineTokens[7], Value.MaxArity)) {
      error(Line, "GENFN_CTOR numeric metadata is invalid");
      return;
    }
    std::size_t FieldStart = 8;
    if (FieldStart < LineTokens.size() &&
        LineTokens[FieldStart] == "recursive") {
      Value.IsRecursive = true;
      ++FieldStart;
    }
    parseFields(LineTokens, FieldStart, Value.Arity, Value.Fields, Line);
    GenericFunction &Owner = generic(LineTokens[1]);
    if (containsNamed(Owner.Constructors, Value.Name)) {
      error(Line, "duplicate GENFN_CTOR '" + Value.Name + "'");
      return;
    }
    Owner.Constructors.push_back(std::move(Value));
  }

  void parseGenericSource(const Tokens &LineTokens, std::size_t Line,
                          std::size_t &Index) {
    if (LineTokens.size() != 3 || !isLocalName(LineTokens[1]) ||
        !isLocalName(LineTokens[2])) {
      error(Line, "GENFN_BEGIN requires owner and source local keys");
      return;
    }
    GenericFunction &Owner = generic(LineTokens[1]);
    if (!Owner.SourceName.empty()) {
      error(Line, "duplicate GENFN_BEGIN for '" + Owner.Name + "'");
      return;
    }
    Owner.SourceName = LineTokens[2];
    bool FoundEnd = false;
    std::string Source;
    for (++Index; Index < Lines.size(); ++Index) {
      if (Lines[Index] == "GENFN_END") {
        FoundEnd = true;
        break;
      }
      if (!Source.empty())
        Source.push_back('\n');
      Source.append(Lines[Index]);
    }
    if (!FoundEnd) {
      error(Line, "GENFN_BEGIN is not terminated by GENFN_END");
      Index = Lines.size();
      return;
    }
    Owner.Source = std::move(Source);
  }

  void validateStructure() {
    if (!SawModule)
      error(1, "missing MODULE record");
    if (!Module)
      return;
    for (const Adt &Value : Module->Adts) {
      if (Value.IsOpaque) {
        if (!Value.Constructors.empty())
          error(1, "opaque ADT '" + Value.Name + "' contains constructors");
        continue;
      }
      if (Value.Constructors.size() != Value.VariantCount) {
        error(1, "ADT '" + Value.Name +
                     "' constructor count does not match variant count");
        continue;
      }
      std::set<std::size_t> Tags;
      std::size_t MaxArity = 0;
      for (const Constructor &Constructor : Value.Constructors) {
        Tags.insert(Constructor.Tag);
        MaxArity = std::max(MaxArity, Constructor.Arity);
      }
      if (Tags.size() != Value.Constructors.size() ||
          (!Tags.empty() && *Tags.rbegin() >= Value.VariantCount))
        error(1, "ADT '" + Value.Name + "' has invalid constructor tags");
      if (MaxArity != Value.MaxArity)
        error(1, "ADT '" + Value.Name +
                     "' maximum arity does not match its constructors");
    }
    for (const Trait &Value : Module->Traits) {
      if (Value.Methods.size() != Value.MethodCount)
        error(1, "TRAIT '" + Value.Name +
                     "' method count does not match METHOD records");
    }
    for (auto &[Name, Generic] : Generics) {
      if (Generic.SourceName.empty()) {
        error(1, "generic function '" + Name +
                     "' has metadata but no GENFN_BEGIN");
        continue;
      }
      Module->GenericFunctions.push_back(std::move(Generic));
    }
  }

  std::vector<std::string_view> Lines;
  std::vector<ParseError> Errors;
  std::optional<InterfaceModule> Module;
  std::optional<std::size_t> CurrentAdt;
  std::optional<std::size_t> CurrentTrait;
  std::optional<std::size_t> CurrentInstance;
  std::map<std::string, GenericFunction> Generics;
  bool SawRecord = false;
  bool SawModule = false;
};

} // namespace

ParseResult parseModule(std::string_view Input) {
  return InterfaceParser(Input).parse();
}

ParseResult readModule(const std::filesystem::path &Path) {
  std::ifstream Input(Path, std::ios::binary);
  if (!Input)
    return std::unexpected(std::vector<ParseError>{
        {0, 0, "unable to open interface file '" + Path.string() + "'"}});
  std::string Contents((std::istreambuf_iterator<char>(Input)),
                       std::istreambuf_iterator<char>());
  if (Input.bad())
    return std::unexpected(std::vector<ParseError>{
        {0, 0, "unable to read interface file '" + Path.string() + "'"}});
  return parseModule(Contents);
}

SearchResult readModuleFromSearchPaths(std::span<const std::string> Roots,
                                       const model::ModuleIdentity &Identity) {
  std::filesystem::path Relative = Identity.relativePath();
  Relative += ".yonai";
  for (const std::string &Root : Roots) {
    const std::filesystem::path Candidate =
        std::filesystem::path(Root) / Relative;
    std::error_code Error;
    const bool Exists = std::filesystem::exists(Candidate, Error);
    if (Error)
      return std::unexpected(std::vector<ParseError>{
          {0, 0,
           "unable to inspect interface path '" + Candidate.string() +
               "': " + Error.message()}});
    if (!Exists)
      continue;
    ParseResult Parsed = readModule(Candidate);
    if (!Parsed)
      return std::unexpected(std::move(Parsed.error()));
    if (Parsed->Identity.fqn() != Identity.fqn())
      return std::unexpected(std::vector<ParseError>{
          {1, 1,
           "MODULE identity does not match requested module '" +
               Identity.fqn() + "'"}});
    return std::optional<InterfaceModule>(std::move(*Parsed));
  }
  return std::optional<InterfaceModule>{};
}

const Function *findFunction(const InterfaceModule &Module,
                             std::string_view LocalName) {
  const auto Found = std::find_if(
      Module.Functions.begin(), Module.Functions.end(),
      [&](const Function &Function) { return Function.Name == LocalName; });
  return Found == Module.Functions.end() ? nullptr : &*Found;
}

} // namespace yona::interface
