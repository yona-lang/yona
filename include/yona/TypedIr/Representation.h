#ifndef YONA_TYPEDIR_REPRESENTATION_H
#define YONA_TYPEDIR_REPRESENTATION_H
#include "yona/TypedIr/Ids.h"

#include <cstdint>
#include <optional>
#include <string_view>
namespace yona::typed_ir {
enum class OwnershipKind { Trivial, Borrowed, Owned };
enum class BorrowProvenanceKind : std::uint8_t {
  BorrowParameter = 0,
  StaticLifetime = 1,
  OwnedValue = 2,
  ScopedUniqueLoan = 3,
  ScopedUniqueLoanParameter = 4
};
struct BorrowProvenance {
  BorrowProvenanceKind Kind;
  std::optional<ValueId> Root;
  friend bool operator==(const BorrowProvenance &,
                         const BorrowProvenance &) = default;
};
enum class PhysicalRepresentation {
  Unselected,
  NoCarrier,
  I1,
  I8,
  I32,
  I64,
  F64,
  ManagedPointer,
  UnmanagedPointer,
  AbiWord,
  AbiValue
};
[[nodiscard]] std::string_view
valueOwnershipKindName(OwnershipKind Kind) noexcept;
} // namespace yona::typed_ir
#endif
