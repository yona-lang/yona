package abzu.ast.expression;

import com.oracle.truffle.api.frame.VirtualFrame;
import abzu.ast.ExpressionNode;

public abstract class ValueNode<T> extends ExpressionNode {
  @Override
  public Object executeGeneric(VirtualFrame frame) {
    return executeValue(frame);
  }

  public abstract T executeValue(VirtualFrame frame);
}
