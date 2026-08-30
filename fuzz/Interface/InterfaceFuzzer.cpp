#include "yona/Interface/Reader.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *Data,
                                      std::size_t Size) {
  constexpr std::size_t MaxInputBytes = 1024 * 1024;
  if (Size > MaxInputBytes)
    return 0;

  const std::string_view Input(reinterpret_cast<const char *>(Data), Size);
  (void)yona::interface::parseModule(Input);
  return 0;
}
