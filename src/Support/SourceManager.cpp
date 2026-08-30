#include "yona/Support/SourceManager.h"

#include <format>
#include <fstream>
#include <iterator>
#include <mutex>
#include <stdexcept>

namespace yona {

struct SourceManager::SourceBuffer final {
  std::string Name;
  std::string Text;
  std::vector<std::size_t> LineOffsets;
};

SourceRange SourceRange::span(SourceRange Start, SourceRange End) {
  if (!Start.isValid() || !End.isValid() || Start.Source != End.Source ||
      End.Offset < Start.Offset) {
    throw std::invalid_argument("source ranges do not form a valid span");
  }
  Start.Length = End.Offset + End.Length - Start.Offset;
  return Start;
}

SourceId SourceManager::addSource(std::string Name, std::string Text) {
  auto Buffer = std::make_shared<SourceBuffer>();
  Buffer->Name = std::move(Name);
  Buffer->Text = std::move(Text);
  Buffer->LineOffsets.push_back(0);
  for (std::size_t Index = 0; Index < Buffer->Text.size(); ++Index) {
    if (Buffer->Text[Index] == '\n')
      Buffer->LineOffsets.push_back(Index + 1);
  }

  std::unique_lock Lock(Mutex);
  if (Buffers.size() >= std::numeric_limits<std::uint32_t>::max())
    throw std::length_error("source manager exhausted SourceId values");
  const SourceId Id(static_cast<std::uint32_t>(Buffers.size()));
  Buffers.push_back(std::move(Buffer));
  return Id;
}

std::expected<SourceId, std::string>
SourceManager::loadFile(const std::filesystem::path &Path) {
  std::ifstream Input(Path, std::ios::binary);
  if (!Input)
    return std::unexpected("could not open source file: " + Path.string());

  std::string Text((std::istreambuf_iterator<char>(Input)),
                   std::istreambuf_iterator<char>());
  if (Input.bad())
    return std::unexpected("could not read source file: " + Path.string());
  return addSource(Path.string(), std::move(Text));
}

std::size_t SourceManager::size() const noexcept {
  std::shared_lock Lock(Mutex);
  return Buffers.size();
}

std::shared_ptr<const SourceManager::SourceBuffer>
SourceManager::buffer(SourceId Id) const {
  std::shared_lock Lock(Mutex);
  if (!Id.isValid() || Id.value() >= Buffers.size())
    throw std::out_of_range("SourceId does not belong to this SourceManager");
  return Buffers[Id.value()];
}

std::string_view SourceManager::name(SourceId Id) const {
  return buffer(Id)->Name;
}

std::string_view SourceManager::text(SourceId Id) const {
  return buffer(Id)->Text;
}

std::string_view SourceManager::line(SourceId Id,
                                     std::size_t LineNumber) const {
  const auto Buffer = buffer(Id);
  if (LineNumber == 0 || LineNumber > Buffer->LineOffsets.size())
    return {};
  const std::size_t Start = Buffer->LineOffsets[LineNumber - 1];
  std::size_t End = Buffer->Text.find('\n', Start);
  if (End == std::string::npos)
    End = Buffer->Text.size();
  if (End > Start && Buffer->Text[End - 1] == '\r')
    --End;
  return std::string_view(Buffer->Text).substr(Start, End - Start);
}

std::string SourceManager::format(SourceRange Range) const {
  if (!Range.isValid())
    return "<unknown>:0:0";
  return std::format("{}:{}:{}", name(Range.Source), Range.Line, Range.Column);
}

} // namespace yona
