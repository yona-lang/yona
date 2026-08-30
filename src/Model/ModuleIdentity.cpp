#include "yona/Model/ModuleIdentity.h"

#include <cctype>
#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace yona::model {
namespace {

bool isIdentifierBody(std::string_view Value) {
  if (Value.empty())
    return false;
  for (const unsigned char Character : Value) {
    if (!std::isalnum(Character))
      return false;
  }
  return true;
}

std::string_view canonicalAcronym(std::string_view Value) {
  if (Value == "ABI")
    return "Abi";
  if (Value == "CFFI")
    return "Cffi";
  if (Value == "GPU")
    return "Gpu";
  if (Value == "IO")
    return "Io";
  if (Value == "JSON")
    return "Json";
  if (Value == "LLVM")
    return "Llvm";
  if (Value == "LSP")
    return "Lsp";
  if (Value == "RPC")
    return "Rpc";
  if (Value == "UTF8")
    return "Utf8";
  if (Value == "UTF16")
    return "Utf16";
  if (Value == "UTF32")
    return "Utf32";
  return Value;
}

std::string exportWords(std::string_view Value) {
  std::string Result;
  std::size_t Start = 0;
  while (Start < Value.size()) {
    while (Start < Value.size() && Value[Start] == '_')
      ++Start;
    if (Start == Value.size())
      break;
    const std::size_t End = Value.find('_', Start);
    const std::string_view Word =
        Value.substr(Start, End == std::string_view::npos ? Value.size() - Start
                                                          : End - Start);
    if (!isIdentifierBody(Word))
      throw std::invalid_argument("invalid Yona export symbol segment");
    const std::string_view CanonicalWord = canonicalAcronym(Word);
    Result.push_back(static_cast<char>(
        std::toupper(static_cast<unsigned char>(CanonicalWord[0]))));
    Result.append(CanonicalWord.substr(1));
    Start = End == std::string_view::npos ? Value.size() : End + 1;
  }
  if (Result.empty())
    throw std::invalid_argument("Yona export symbol must not be empty");
  return Result;
}

} // namespace

ModuleIdentity::ModuleIdentity(std::string_view Name) {
  std::size_t Start = 0;
  while (Start <= Name.size()) {
    const std::size_t End = Name.find_first_of("\\/", Start);
    const std::string_view Segment =
        Name.substr(Start, End == std::string_view::npos ? Name.size() - Start
                                                         : End - Start);
    if (!isIdentifierBody(Segment) || canonicalAcronym(Segment) != Segment ||
        !std::isupper(static_cast<unsigned char>(Segment[0]))) {
      throw std::invalid_argument(
          "Yona module segments must be non-empty PascalCase identifiers");
    }
    Segments.emplace_back(Segment);
    if (End == std::string_view::npos)
      break;
    Start = End + 1;
  }
}

std::string ModuleIdentity::fqn() const {
  std::string Result;
  for (const std::string &Segment : Segments) {
    if (!Result.empty())
      Result.push_back('\\');
    Result += Segment;
  }
  return Result;
}

std::filesystem::path ModuleIdentity::relativePath() const {
  std::filesystem::path Result;
  for (const std::string &Segment : Segments)
    Result /= Segment;
  return Result;
}

std::string ModuleIdentity::mangle(std::string_view Symbol) const {
  std::string Result = "Yona";
  for (const std::string &Segment : Segments)
    Result += Segment;
  Result += exportWords(Symbol);
  return Result;
}

std::string mangleExport(std::string_view ModuleName, std::string_view Symbol) {
  return ModuleIdentity(ModuleName).mangle(Symbol);
}

} // namespace yona::model
