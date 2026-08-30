#ifndef YONA_SUPPORT_SOURCEMANAGER_H
#define YONA_SUPPORT_SOURCEMANAGER_H

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <limits>
#include <memory>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <vector>

namespace yona {

/// Manager-local identity of an immutable source buffer.
class SourceId final {
public:
  constexpr SourceId() noexcept = default;
  explicit constexpr SourceId(std::uint32_t Value) noexcept : Value(Value) {}

  [[nodiscard]] constexpr bool isValid() const noexcept {
    return Value != InvalidValue;
  }
  [[nodiscard]] constexpr std::uint32_t value() const noexcept { return Value; }

  friend constexpr bool operator==(SourceId, SourceId) noexcept = default;

private:
  static constexpr std::uint32_t InvalidValue =
      (std::numeric_limits<std::uint32_t>::max)();
  std::uint32_t Value = InvalidValue;
};

/// Half-open byte range in a source buffer.
///
/// Line and Column are one-based display coordinates. Offset and Length are
/// UTF-8 byte counts. A range never owns text; its SourceId is resolved through
/// the SourceManager owned by the surrounding ParsedModule or SemanticModel.
struct SourceRange {
  SourceId Source;
  std::size_t Line = 1;
  std::size_t Column = 1;
  std::size_t Offset = 0;
  std::size_t Length = 0;

  [[nodiscard]] bool isValid() const noexcept {
    return Source.isValid() && Line > 0 && Column > 0;
  }

  [[nodiscard]] static SourceRange unknown() noexcept { return {}; }
  /// Form the smallest range from Start through End. Throws
  /// std::invalid_argument for invalid ranges, different sources, or reversed
  /// offsets.
  [[nodiscard]] static SourceRange span(SourceRange Start, SourceRange End);
};

/// Owns immutable UTF-8 source buffers and their line indexes.
///
/// Buffers are retained until manager destruction; returned string views stay
/// valid for that lifetime. Concurrent reads and additions are safe. File-load
/// failures are returned as owned error messages and never partially register
/// a source.
class SourceManager final {
public:
  SourceManager() = default;
  SourceManager(const SourceManager &) = delete;
  SourceManager &operator=(const SourceManager &) = delete;

  /// Move an owned name and buffer into this manager. The returned identity is
  /// manager-local. Throws std::length_error if the identity space is
  /// exhausted; allocation failures propagate without registering a source.
  [[nodiscard]] SourceId addSource(std::string Name, std::string Text);
  /// Read Path completely and register it only on success. Filesystem and I/O
  /// failures are returned as owned text.
  [[nodiscard]] std::expected<SourceId, std::string>
  loadFile(const std::filesystem::path &Path);

  [[nodiscard]] std::size_t size() const noexcept;
  /// Borrow a name or buffer for this manager's lifetime. Both functions throw
  /// std::out_of_range for an invalid or foreign identity.
  [[nodiscard]] std::string_view name(SourceId Id) const;
  [[nodiscard]] std::string_view text(SourceId Id) const;
  /// Borrow one line without its line terminator. An out-of-range line number
  /// returns an empty view; an invalid or foreign identity throws
  /// std::out_of_range.
  [[nodiscard]] std::string_view line(SourceId Id,
                                      std::size_t LineNumber) const;
  /// Return an owned display location. An invalid range uses the unknown
  /// fallback; a valid range with a foreign identity throws std::out_of_range.
  [[nodiscard]] std::string format(SourceRange Range) const;

private:
  struct SourceBuffer;

  [[nodiscard]] std::shared_ptr<const SourceBuffer> buffer(SourceId Id) const;

  mutable std::shared_mutex Mutex;
  std::vector<std::shared_ptr<const SourceBuffer>> Buffers;
};

} // namespace yona

#endif /* YONA_SUPPORT_SOURCEMANAGER_H */
