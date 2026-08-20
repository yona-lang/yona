#pragma once

/// Seed of typed-core (#7): in-process frontend query types.
///
/// These types are the public surface for editor and tooling queries
/// (hover, definitions, diagnostics). They must not include LLVM headers.
/// `yls` (`include/lsp/Analysis.h`) is the first consumer.

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace yona::typed_core {

struct Position {
    std::size_t line = 0;
    std::size_t character = 0;
};

inline bool operator==(const Position& a, const Position& b) {
    return a.line == b.line && a.character == b.character;
}

inline bool operator<(const Position& a, const Position& b) {
    return a.line < b.line || (a.line == b.line && a.character < b.character);
}

inline bool operator<=(const Position& a, const Position& b) {
    return a < b || a == b;
}

struct Range {
    Position start;
    Position end;

    /// LSP ranges are end-exclusive: [start, end).
    bool contains(Position p) const { return start <= p && p < end; }

    bool overlaps(const Range& other) const {
        return start < other.end && other.start < end;
    }
};

struct Diagnostic {
    Range range;
    int severity = 1;
    std::string code;
    std::string message;
};

struct Hover {
    std::string contents;
    Range range;
};

struct Symbol {
    std::string name;
    std::string kind;
    Range range;
    Range selection;
    std::string type;
    std::string container;
};

} // namespace yona::typed_core
