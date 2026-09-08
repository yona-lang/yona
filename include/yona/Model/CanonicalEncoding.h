#ifndef YONA_MODEL_CANONICALENCODING_H
#define YONA_MODEL_CANONICALENCODING_H

#include "yona/Model/StructuralType.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace yona::model {

inline constexpr std::uint32_t CanonicalStructuralEncodingVersion = 2;

struct CanonicalBinderRef {
  std::uint32_t BinderIndex;
  std::uint32_t Ordinal;
  friend bool operator==(CanonicalBinderRef, CanonicalBinderRef) = default;
  friend auto operator<=>(CanonicalBinderRef, CanonicalBinderRef) = default;
};

struct CanonicalTypeParameterMapping {
  TypeParameterId Source;
  CanonicalBinderRef Target;
};
struct CanonicalEffectVariableMapping {
  EffectVariableId Source;
  CanonicalBinderRef Target;
};

enum class CanonicalEncodingErrorCode : std::uint8_t {
  InvalidIdentity,
  InvalidBinder,
  UnboundParameter,
  MultiplyOwnedParameter,
  OpenRuntimeDescriptor,
  InvalidReference,
  DuplicateRecord,
  NonCanonicalOrder,
  SizeLimit
};

struct CanonicalEncodingError {
  CanonicalEncodingErrorCode Code;
  std::string Path;
  std::string Message;
};

inline constexpr std::size_t DefaultCanonicalDecodeUnits = 1u << 20u;

class CanonicalDecodeBudget final {
public:
  explicit CanonicalDecodeBudget(
      std::size_t Units = DefaultCanonicalDecodeUnits) noexcept
      : Remaining_(Units) {}

  [[nodiscard]] std::expected<void, CanonicalEncodingError>
  claim(std::uint64_t Units, std::string Path);
  [[nodiscard]] std::size_t remaining() const noexcept { return Remaining_; }

private:
  std::size_t Remaining_;
};

enum class CanonicalRootKind : std::uint8_t {
  Type = 0,
  Effect = 1,
  Operation = 2
};

struct ClosedEffectOperation {
  EffectOperationKey Key;
  std::vector<TypeId> TypeArguments;
  std::vector<EffectRowId> EffectArguments;
  std::vector<FunctionParameter> Parameters;
  TypeId Result;
  ResultOwnership ResultContract;
  EffectRowId Effects;
};

using CanonicalRoot = std::variant<TypeId, EffectRowId, ClosedEffectOperation>;
using CanonicalBytesResult =
    std::expected<std::vector<std::byte>, CanonicalEncodingError>;

[[nodiscard]] CanonicalBytesResult
encodeClosedDescriptorGraph(const TypeTable &Types, const CanonicalRoot &Root);

[[nodiscard]] CanonicalBytesResult encodeOperationDeclarationGraph(
    const TypeTable &Types, const EffectOperationSignature &Signature,
    std::span<const CanonicalBinderDeclaration> Binders);

[[nodiscard]] std::uint64_t
canonicalStructuralFingerprint(std::span<const std::byte> Bytes) noexcept;

struct CanonicalInterfaceTables {
  std::vector<std::byte> TypeSectionPayload;
  std::vector<std::byte> EffectSectionPayload;
  std::vector<std::uint32_t> TypeRemap;
  std::vector<std::uint32_t> EffectRemap;
  std::vector<std::uint32_t> InstanceRemap;
  std::vector<CanonicalTypeParameterMapping> TypeParameterRemap;
  std::vector<CanonicalEffectVariableMapping> EffectVariableRemap;
};

[[nodiscard]]
std::expected<CanonicalInterfaceTables, CanonicalEncodingError>
encodeInterfaceTables(const TypeTable &Types,
                      std::span<const CanonicalBinderDeclaration> Binders);

using DecodedBinderOwnerIdentity =
    std::variant<NominalTypeKey, FunctionDeclarationIdentity, std::uint32_t,
                 EffectOperationKey>;
struct DecodedBinderDeclaration {
  BinderOwnerKind Kind;
  DecodedBinderOwnerIdentity Identity;
  std::vector<TypeParameterId> TypeParameters;
  std::vector<EffectVariableDeclaration> EffectVariables;
};
struct DecodedTypeParameterMapping {
  CanonicalBinderRef Source;
  TypeParameterId Target;
};
struct DecodedEffectVariableMapping {
  CanonicalBinderRef Source;
  EffectVariableId Target;
};
struct DecodedInterfaceTables {
  TypeTable Types;
  std::vector<DecodedBinderDeclaration> Binders;
  std::vector<TypeId> TypeIdsByCanonicalIndex;
  std::vector<EffectRowId> EffectIdsByCanonicalIndex;
  std::vector<DecodedTypeParameterMapping> TypeParametersByCanonicalRef;
  std::vector<DecodedEffectVariableMapping> EffectVariablesByCanonicalRef;
};

[[nodiscard]] std::expected<DecodedInterfaceTables, CanonicalEncodingError>
decodeInterfaceTables(std::span<const std::byte> TypeSectionPayload,
                      std::span<const std::byte> EffectSectionPayload,
                      CanonicalDecodeBudget *Budget = nullptr);

} // namespace yona::model

#endif /* YONA_MODEL_CANONICALENCODING_H */
