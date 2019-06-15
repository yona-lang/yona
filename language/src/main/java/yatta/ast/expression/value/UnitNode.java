package yatta.ast.expression.value;

import yatta.ast.ExpressionNode;
import yatta.runtime.Unit;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;

@NodeInfo
public final class UnitNode extends ExpressionNode {
  public static UnitNode INSTANCE = new UnitNode();

  private UnitNode() {
  }

  @Override
  public String toString() {
    return "UnitNode{}";
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    return Unit.INSTANCE;
  }

  @Override
  public Unit executeUnit(VirtualFrame frame) throws UnexpectedResultException {
    return Unit.INSTANCE;
  }
}
