#include "yona/Syntax/Parser.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *Data,
                                      std::size_t Size) {
  constexpr std::size_t MaxInputBytes = 1024 * 1024;
  if (Size > MaxInputBytes)
    return 0;

  const char *Bytes = Size == 0 ? "" : reinterpret_cast<const char *>(Data);
  const std::string_view Source(Bytes, Size);

  yona::parser::Parser ExpressionParser;
  const auto Expression =
      ExpressionParser.parseExpression(std::string(Source), "FuzzInput.yona");
  (void)Expression;

  yona::parser::Parser ModuleParser;
  const auto Module =
      ModuleParser.parseModule(std::string(Source), "FuzzInput.yona");
  (void)Module;
  return 0;
}
