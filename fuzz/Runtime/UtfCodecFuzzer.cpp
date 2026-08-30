#include "yona/Lsp/Utf16.h"
#include "yona/Runtime/Codecs/Utf16.h"

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
  const std::string_view Input(Bytes, Size);
  const std::size_t Offset =
      Size == 0 ? 0 : static_cast<std::size_t>(Data[0]) % (Size + 1);
  const std::size_t Line = Size > 1 ? Data[1] : 0;
  const std::size_t Character = Size > 2 ? Data[2] : 0;

  const auto Position = yona::lsp::offset_to_position(Input, Offset);
  (void)yona::lsp::position_to_offset(Input, Position);
  (void)yona::lsp::position_to_offset(Input,
                                      yona::lsp::Position{Line, Character});

  int64_t RuntimeLine = 0;
  int64_t RuntimeCharacter = 0;
  YonaRuntimeUtf8OffsetToUtf16(Bytes, Size, Offset, &RuntimeLine,
                            &RuntimeCharacter);
  (void)YonaRuntimeUtf16PositionToUtf8(Bytes, Size, RuntimeLine, RuntimeCharacter);
  (void)YonaRuntimeUtf16PositionToUtf8(Bytes, Size, static_cast<int64_t>(Line),
                                    static_cast<int64_t>(Character));

  const auto Uri = yona::lsp::file_uri(Input);
  (void)yona::lsp::uri_to_path(Uri);
  return 0;
}
