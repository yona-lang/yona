package abzu.ast;

import abzu.runtime.async.Promise;
import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.frame.VirtualFrame;

public final class MainExpressionNode extends ExpressionNode {
  @Child
  public ExpressionNode expressionNode;

  public MainExpressionNode(ExpressionNode expressionNode) {
    this.expressionNode = expressionNode;
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    Object result = expressionNode.executeGeneric(frame);
    if (result instanceof Promise) {
      Promise promise = (Promise) result;
      CompilerDirectives.transferToInterpreter();
      result = Promise.await(promise);
      System.err.println(promise);
    }

    return result;
  }
}
