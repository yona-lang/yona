package abzu.ast;

import abzu.runtime.async.AbzuFuture;
import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.frame.VirtualFrame;

import java.util.concurrent.ExecutionException;

public final class MainExpressionNode extends ExpressionNode {
  @Child
  public ExpressionNode expressionNode;

  public MainExpressionNode(ExpressionNode expressionNode) {
    this.expressionNode = expressionNode;
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    Object result = expressionNode.executeGeneric(frame);
    if (result instanceof AbzuFuture) {
      AbzuFuture future = (AbzuFuture) result;
      CompilerDirectives.transferToInterpreter();
      try {
        result = future.completableFuture.get();
      } catch (InterruptedException e) {
        e.printStackTrace();
      } catch (ExecutionException e) {
        e.printStackTrace();
      }
    }

    return result;
  }
}
