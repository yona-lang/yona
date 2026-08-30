#include "yona/Runtime/Codecs/Regex.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace {

struct RegexInput {
  std::string Pattern;
  std::string Text;
  std::string Replacement;
};

RegexInput splitInput(std::string_view Input) {
  const auto First = Input.find('\n');
  const auto Pattern = Input.substr(0, First);
  if (First == std::string_view::npos)
    return {std::string(Pattern), {}, {}};

  const auto Remainder = Input.substr(First + 1);
  const auto Second = Remainder.find('\n');
  if (Second == std::string_view::npos)
    return {std::string(Pattern), std::string(Remainder), {}};
  return {std::string(Pattern), std::string(Remainder.substr(0, Second)),
          std::string(Remainder.substr(Second + 1))};
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *Data,
                                      std::size_t Size) {
  constexpr std::size_t MaxInputBytes = 16 * 1024;
  if (Size > MaxInputBytes)
    return 0;

  const char *Bytes = Size == 0 ? "" : reinterpret_cast<const char *>(Data);
  auto Input = splitInput(std::string_view(Bytes, Size));
  Input.Pattern.resize(std::min<std::size_t>(Input.Pattern.size(), 4096));

  YonaRegexRef Regex = YonaStdRegexCompile(Input.Pattern.c_str());
  if (Regex == nullptr)
    return 0;

  (void)YonaStdRegexMatches(Regex, Input.Text.c_str());

  int64_t *First = YonaStdRegexFind(Regex, Input.Text.c_str());
  YonaRuntimeRegexRelease(First);
  int64_t *All = YonaStdRegexFindAll(Regex, Input.Text.c_str());
  YonaRuntimeRegexRelease(All);
  int64_t *Parts = YonaStdRegexSplit(Regex, Input.Text.c_str());
  YonaRuntimeRegexRelease(Parts);

  char *Replaced =
      YonaStdRegexReplace(Regex, Input.Text.c_str(), Input.Replacement.c_str());
  YonaRuntimeRegexRelease(Replaced);
  char *ReplacedAll = YonaStdRegexReplaceAll(Regex, Input.Text.c_str(),
                                             Input.Replacement.c_str());
  YonaRuntimeRegexRelease(ReplacedAll);

  YonaRuntimeRegexRelease(Regex);
  return 0;
}
