package abzu.ast.expression.value;

import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import abzu.ast.expression.ValueNode;

@NodeInfo
public final class BooleanNode extends ValueNode<Boolean> {
  public static BooleanNode TRUE = new BooleanNode(Boolean.TRUE);
  public static BooleanNode FALSE = new BooleanNode(Boolean.FALSE);

  public final Boolean value;

  BooleanNode(Boolean value) {
    this.value = value;
  }

  @Override
  public String toString() {
    return "BooleanNode{" +
        "value=" + value +
        '}';
  }

  @Override
  public Boolean executeValue(VirtualFrame frame) {
    return value;
  }
}
