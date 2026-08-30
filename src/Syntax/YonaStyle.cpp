#include "yona/Syntax/YonaStyle.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace yona::syntax {
namespace {

bool isAsciiLower(char Character) {
  return Character >= 'a' && Character <= 'z';
}

bool isAsciiUpper(char Character) {
  return Character >= 'A' && Character <= 'Z';
}

bool isAsciiDigit(char Character) {
  return Character >= '0' && Character <= '9';
}

bool hasValidContinuation(std::string_view Name) {
  for (const unsigned char Character : Name) {
    if (isAsciiLower(static_cast<char>(Character)) ||
        isAsciiUpper(static_cast<char>(Character)) ||
        isAsciiDigit(static_cast<char>(Character)) || Character >= 0x80)
      continue;
    return false;
  }
  return true;
}

bool hasUppercaseRun(std::string_view Name) {
  std::size_t RunLength = 0;
  for (const char Character : Name) {
    if (isAsciiUpper(Character)) {
      ++RunLength;
      if (RunLength >= 2)
        return true;
    } else {
      RunLength = 0;
    }
  }
  return false;
}

bool isLowerCamel(std::string_view Name) {
  if (Name.empty() || !isAsciiLower(Name.front()) ||
      !hasValidContinuation(Name))
    return false;
  return !hasUppercaseRun(Name);
}

bool isUpperCamel(std::string_view Name) {
  if (Name.empty() || !isAsciiUpper(Name.front()) ||
      !hasValidContinuation(Name))
    return false;
  return !hasUppercaseRun(Name);
}

bool beginsUppercaseDeclaration(lexer::TokenType Type) {
  using lexer::TokenType;
  return Type == TokenType::YMODULE || Type == TokenType::YTYPE ||
         Type == TokenType::YTRAIT || Type == TokenType::YEFFECT ||
         Type == TokenType::YINSTANCE || Type == TokenType::YRECORD;
}

} // namespace

std::expected<std::vector<YonaStyleDiagnostic>, std::vector<lexer::LexError>>
checkYonaStyle(std::string_view Source, std::string_view Filename) {
  auto Sources = std::make_shared<SourceManager>();
  const SourceId SourceId =
      Sources->addSource(std::string(Filename), std::string(Source));
  lexer::Lexer Lexer(Sources, SourceId);
  auto Tokens = Lexer.tokenize();
  if (!Tokens)
    return std::unexpected(std::move(Tokens.error()));

  std::vector<YonaStyleDiagnostic> Diagnostics;
  lexer::TokenType Previous = lexer::TokenType::YEOF_TOKEN;
  bool InModuleName = false;
  for (const lexer::Token &Token : *Tokens) {
    if (Token.type == lexer::TokenType::YMODULE) {
      InModuleName = true;
      Previous = Token.type;
      continue;
    }
    if (Token.type == lexer::TokenType::YNEWLINE ||
        Token.type == lexer::TokenType::YEOF_TOKEN) {
      InModuleName = false;
      Previous = Token.type;
      continue;
    }
    if (Token.type != lexer::TokenType::YIDENTIFIER) {
      Previous = Token.type;
      continue;
    }

    const std::string_view Name = Token.lexeme;
    const bool RequiresUpper = InModuleName ||
                               beginsUppercaseDeclaration(Previous) ||
                               isAsciiUpper(Name.empty() ? '\0' : Name.front());
    const bool RequiresLower = Previous == lexer::TokenType::YDOT;
    const bool Valid = RequiresLower   ? isLowerCamel(Name)
                       : RequiresUpper ? isUpperCamel(Name)
                                       : isLowerCamel(Name);
    if (!Valid) {
      Diagnostics.push_back(
          {Token.Range,
           std::string(RequiresLower || !RequiresUpper ? "value name '"
                                                       : "type name '") +
               std::string(Name) +
               (RequiresLower || !RequiresUpper
                    ? "' must use camelCase with acronyms treated as words"
                    : "' must use PascalCase with acronyms treated as words"),
           Sources});
    }
    Previous = Token.type;
  }
  return Diagnostics;
}

} // namespace yona::syntax
