#include "yona/Lsp/Json.h"
#include "yona/Lsp/JsonRpc.h"

#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *Data,
                                      std::size_t Size) {
  constexpr std::size_t MaxInputBytes = 1024 * 1024;
  if (Size > MaxInputBytes)
    return 0;

  const char *Bytes = Size == 0 ? "" : reinterpret_cast<const char *>(Data);
  const std::string_view Input(Bytes, Size);

  std::string Error;
  const auto Json = yona::lsp::Json::parse(Input, &Error);
  const auto Dumped = Json.dump();
  if (Error.empty()) {
    std::string RoundTripError;
    (void)yona::lsp::Json::parse(Dumped, &RoundTripError);
  }

  (void)yona::lsp::JsonRpc::parse_message(Input);

  const auto Encoded = yona::lsp::JsonRpc::encode(Json);
  std::istringstream EncodedStream(Encoded);
  const auto EncodedBody =
      yona::lsp::JsonRpc::read_body(EncodedStream, MaxInputBytes * 2);
  if (EncodedBody)
    (void)yona::lsp::JsonRpc::parse_message(*EncodedBody);

  std::istringstream InputStream{std::string(Input)};
  const auto FramedBody =
      yona::lsp::JsonRpc::read_body(InputStream, MaxInputBytes);
  if (FramedBody)
    (void)yona::lsp::JsonRpc::parse_message(*FramedBody);
  return 0;
}
