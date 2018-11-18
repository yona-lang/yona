package abzu.ast.expression.value;

import abzu.ast.ExpressionNode;
import abzu.runtime.Unit;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;

@NodeInfo
public final class UnitNode extends ExpressionNode {
  public static UnitNode INSTANCE = new UnitNode();

  private UnitNode() {
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
