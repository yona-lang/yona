#include "yona/TypedIr/Instruction.h"

#include <doctest/doctest.h>

TEST_CASE("Typed IR core visitors enumerate every operand and successor") {
  using namespace yona::typed_ir;
  const yona::model::IdDomain Domain = 7;
  const auto V0 = ValueId(Domain, 0);
  const auto V1 = ValueId(Domain, 1);
  std::vector<ValueId> Operands;
  forEachOperand(InstructionPayload{BinaryInst{BinaryOpcode::AddInt, V0, V1}},
                 [&](ValueId Id) { Operands.push_back(Id); });
  CHECK(Operands == std::vector<ValueId>{V0, V1});
  std::vector<SuccessorView> Successors;
  const Terminator End =
      CondBranch{V0,
                 {EdgeId(Domain, 0), BlockId(Domain, 1), {V1}},
                 {EdgeId(Domain, 1), BlockId(Domain, 1), {V0}}};
  forEachSuccessor(End,
                   [&](SuccessorView View) { Successors.push_back(View); });
  REQUIRE(Successors.size() == 2);
  CHECK(Successors[0].Ordinal == 0);
  CHECK(Successors[1].Ordinal == 1);
  CHECK(Successors[0].Edge != Successors[1].Edge);
  CHECK(Successors[0].Block == Successors[1].Block);
}
