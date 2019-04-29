package abzu.ast;

import abzu.runtime.async.Promise;
import com.oracle.truffle.api.frame.VirtualFrame;

public final class MainExpressionNode extends ExpressionNode {
  @Child public ExpressionNode expressionNode;

  public MainExpressionNode(ExpressionNode expressionNode) {
    this.expressionNode = expressionNode;
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    Object result = expressionNode.executeGeneric(frame);
    if (result instanceof Promise) {
      try {
        result = Promise.await((Promise) result);
      } catch (InterruptedException e) {
        throw new RuntimeException(e);
      }
    }
    return result;
  }
}
