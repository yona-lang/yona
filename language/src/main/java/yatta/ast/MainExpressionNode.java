package yatta.ast;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.frame.VirtualFrame;
import yatta.YattaException;
import yatta.runtime.Context;
import yatta.runtime.Function;
import yatta.runtime.async.Promise;

public final class MainExpressionNode extends ExpressionNode {
  @Child
  public ExpressionNode expressionNode;

  public MainExpressionNode(ExpressionNode expressionNode) {
    this.expressionNode = expressionNode;
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    try {
      Object result = expressionNode.executeGeneric(frame);
      if (result instanceof Promise) {
        Promise promise = (Promise) result;
        CompilerDirectives.transferToInterpreterAndInvalidate();
        try {
          result = Promise.await(promise);
        } catch (YattaException e) {
          throw e;
        } catch (Throwable e) {
          throw new YattaException(e, this);
        }
      }

      return executeIfFunction(result, frame);
    } finally {
      Context.getCurrent().getThreading().dispose();
    }
  }

  private Object executeIfFunction(Object result, VirtualFrame frame) {
    if (result instanceof Function) {
      Function function = (Function) result;
      if (function.getCardinality() == 0) {
        return function.getCallTarget().getRootNode().execute(frame);
      }
    }

    return result;
  }
}
