package abzu.ast.expression.value;

import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import abzu.ast.expression.ValueNode;
import abzu.runtime.AbzuNone;

@NodeInfo
public final class UnitNode extends ValueNode<Object> {
  public static UnitNode INSTANCE = new UnitNode();

  private UnitNode() {
  }

  @Override
  public Object executeValue(VirtualFrame frame) {
    return AbzuNone.SINGLETON;
  }
}
