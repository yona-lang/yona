package yatta.ast.expression.value;

import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import yatta.runtime.Unit;

@NodeInfo
public final class UnitNode extends LiteralValueNode {
  public static final UnitNode INSTANCE = new UnitNode();

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
  public Unit executeUnit(VirtualFrame frame) {
    return Unit.INSTANCE;
  }

  @Override
  public boolean isAdoptable() {
    return false;
  }
}
