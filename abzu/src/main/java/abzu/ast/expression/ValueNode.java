package abzu.ast.expression;

import com.oracle.truffle.api.frame.VirtualFrame;
import abzu.ast.AbzuExpressionNode;

public abstract class ValueNode<T> extends AbzuExpressionNode {
  @Override
  public Object executeGeneric(VirtualFrame frame) {
    return executeValue(frame);
  }

  public abstract T executeValue(VirtualFrame frame);
}
