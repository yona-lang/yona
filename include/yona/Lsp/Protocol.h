#ifndef YONA_LSP_PROTOCOL_H
#define YONA_LSP_PROTOCOL_H

#include <cstddef>
#include <string>

namespace yona::lsp {

/// LSP protocol value contracts. Positions use zero-based UTF-16 code-unit
/// coordinates.
///
/// Ownership:
/// - Values own all contained strings and may be copied freely.
///
/// Failure:
/// - Range comparison helpers perform no ordering or bounds validation.
///
/// Thread safety:
/// - Values have no shared mutable state. Separate values and immutable access
///   to one value are safe concurrently.
struct Position {
  std::size_t line = 0;
  std::size_t character = 0;
};

inline bool operator==(const Position &Left, const Position &Right) {
  return Left.line == Right.line && Left.character == Right.character;
}

inline bool operator<(const Position &Left, const Position &Right) {
  return Left.line < Right.line ||
         (Left.line == Right.line && Left.character < Right.character);
}

inline bool operator<=(const Position &Left, const Position &Right) {
  return Left < Right || Left == Right;
}

struct Range {
  Position start;
  Position end;

  /// LSP ranges are end-exclusive: [start, end).
  [[nodiscard]] bool contains(Position Value) const {
    return start <= Value && Value < end;
  }

  [[nodiscard]] bool overlaps(const Range &Other) const {
    return start < Other.end && Other.start < end;
  }
};

struct LspDiagnostic {
  Range range;
  int severity = 1;
  std::string code;
  std::string message;
};

struct HoverInfo {
  std::string contents;
  Range range;
};

struct Location {
  std::string uri;
  Range range;
};

struct DocumentHighlight {
  Range range;
  int kind = 2; // LSP: 1 Text, 2 Read, 3 Write
};

struct SymbolInfo {
  std::string name;
  std::string kind;
  Range range;
  Range selection;
  std::string type;
  std::string container;
  std::string detail;
};

} // namespace yona::lsp

#endif /* YONA_LSP_PROTOCOL_H */
