#ifndef YONA_TYPEDIR_IDS_H
#define YONA_TYPEDIR_IDS_H

#include "yona/Model/StructuralType.h"

#include <variant>

namespace yona::typed_ir {
template <class Tag> using StrongId = model::StrongId<Tag>;
using StrongIdHasher = model::StrongIdHasher;
using FunctionId = StrongId<struct FunctionIdTag>;
using BlockId = StrongId<struct BlockIdTag>;
using ValueId = StrongId<struct ValueIdTag>;
using EdgeId = StrongId<struct EdgeIdTag>;
using GlobalId = StrongId<struct GlobalIdTag>;
using NominalTypeId = StrongId<struct NominalTypeIdTag>;
using ConstructorId = StrongId<struct ConstructorIdTag>;
using PatternId = StrongId<struct PatternIdTag>;
using MatchPlanId = StrongId<struct MatchPlanIdTag>;
using DecisionNodeId = StrongId<struct DecisionNodeIdTag>;
using CleanupRegionId = StrongId<struct CleanupRegionIdTag>;
using CallableDescriptorId = StrongId<struct CallableDescriptorIdTag>;
using RecursiveClosureGroupDescriptorId =
    StrongId<struct RecursiveClosureGroupDescriptorIdTag>;
using OperationId = StrongId<struct OperationIdTag>;
using OperationInstanceId = StrongId<struct OperationInstanceIdTag>;
using RuntimeEffectRowId = StrongId<struct RuntimeEffectRowIdTag>;
using HandlerBoundaryDescriptorId =
    StrongId<struct HandlerBoundaryDescriptorIdTag>;
using TryBoundaryDescriptorId = StrongId<struct TryBoundaryDescriptorIdTag>;
using ContinuationBoundaryDescriptorId =
    std::variant<HandlerBoundaryDescriptorId, TryBoundaryDescriptorId>;
using BoundaryContextContractId = StrongId<struct BoundaryContextContractIdTag>;
using BoundaryRequirementSetId = StrongId<struct BoundaryRequirementSetIdTag>;

class ModuleMutationDomain final {
  friend class Builder;
  explicit ModuleMutationDomain(model::IdDomain Identity)
      : Identity_(Identity) {}

public:
  [[nodiscard]] model::IdDomain identity() const noexcept { return Identity_; }

private:
  model::IdDomain Identity_;
};
class FunctionLocalDomain final {
  friend class Builder;
  explicit FunctionLocalDomain(model::IdDomain Identity)
      : Identity_(Identity) {}

public:
  [[nodiscard]] model::IdDomain identity() const noexcept { return Identity_; }

private:
  model::IdDomain Identity_;
};

using ControlRegionId = StrongId<struct ControlRegionIdTag>;
using GeneratorId = StrongId<struct GeneratorIdTag>;
using ContinuationSegmentId = StrongId<struct ContinuationSegmentIdTag>;
using ContinuationTransitionTableId =
    StrongId<struct ContinuationTransitionTableIdTag>;
} // namespace yona::typed_ir

#endif
