package abzu.ast.expression.value;

import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import abzu.ast.expression.ValueNode;

@NodeInfo
public final class BoolNode extends ValueNode<Boolean> {
  public static BoolNode TRUE = new BoolNode(Boolean.TRUE);
  public static BoolNode FALSE = new BoolNode(Boolean.FALSE);

  public final Boolean value;

  BoolNode(Boolean value) {
    this.value = value;
  }

  @Override
  public String toString() {
    return "BoolNode{" +
        "value=" + value +
        '}';
  }

  @Override
  public Boolean executeValue(VirtualFrame frame) {
    return value;
  }
}
