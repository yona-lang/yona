#ifndef YONA_TEST_SUPPORT_STRUCTURALTYPE_TESTACCESS_H
#define YONA_TEST_SUPPORT_STRUCTURALTYPE_TESTACCESS_H

#include "yona/Model/StructuralType.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace yona::model::test {

/// Scoped access to deterministic fault injection used by the structural
/// arena tests. The production API deliberately exposes none of these knobs.
class StructuralTypeTestAccess final {
public:
  struct Configuration {
    std::optional<std::size_t> FailAfter;
    std::optional<std::size_t> ConstantHash;
    std::optional<std::uint64_t> ConstantFingerprint;
  };

  struct Snapshot {
    IdDomain Domain;
    std::size_t TypeCount;
    std::size_t EffectCount;
    std::size_t TypeIndexCapacity;
    std::size_t EffectIndexCapacity;
    std::size_t TypeParameterCount;
    std::size_t EffectVariableCount;
    std::uint64_t Generation;

    friend bool operator==(const Snapshot &, const Snapshot &) = default;
  };

  explicit StructuralTypeTestAccess(Configuration Config);
  ~StructuralTypeTestAccess();

  StructuralTypeTestAccess(const StructuralTypeTestAccess &) = delete;
  StructuralTypeTestAccess &
  operator=(const StructuralTypeTestAccess &) = delete;

  [[nodiscard]] static Snapshot snapshot(const TypeTable &Types) noexcept;

private:
  Configuration Previous_;
};

inline StructuralTypeTestAccess::StructuralTypeTestAccess(Configuration Config)
    : Previous_{TypeTable::TestHooks_.FailAfter,
                TypeTable::TestHooks_.ConstantHash,
                TypeTable::TestHooks_.ConstantFingerprint} {
  TypeTable::TestHooks_.FailAfter = Config.FailAfter;
  TypeTable::TestHooks_.ConstantHash = Config.ConstantHash;
  TypeTable::TestHooks_.ConstantFingerprint = Config.ConstantFingerprint;
}

inline StructuralTypeTestAccess::~StructuralTypeTestAccess() {
  TypeTable::TestHooks_.FailAfter = Previous_.FailAfter;
  TypeTable::TestHooks_.ConstantHash = Previous_.ConstantHash;
  TypeTable::TestHooks_.ConstantFingerprint = Previous_.ConstantFingerprint;
}

inline StructuralTypeTestAccess::Snapshot
StructuralTypeTestAccess::snapshot(const TypeTable &Types) noexcept {
  return {Types.Domain_,
          Types.Types_.size(),
          Types.Effects_.size(),
          Types.Types_.capacity(),
          Types.Effects_.capacity(),
          Types.TypeParameterCount_,
          Types.EffectVariableCount_,
          Types.Generation_};
}

} // namespace yona::model::test

#endif /* YONA_TEST_SUPPORT_STRUCTURALTYPE_TESTACCESS_H */
