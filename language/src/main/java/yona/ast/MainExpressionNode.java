package yona.ast;

import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.YonaException;
import yona.runtime.Function;
import yona.runtime.async.Promise;

@NodeInfo(shortName = "main")
public final class MainExpressionNode extends ExpressionNode {
  @Child
  public ExpressionNode expressionNode;

  public MainExpressionNode(ExpressionNode expressionNode) {
    this.expressionNode = expressionNode;
  }

  @Override
  public String toString() {
    return "MainExpressionNode{" +
        "expressionNode=" + expressionNode +
        '}';
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    Object result = expressionNode.executeGeneric(frame);
    if (result instanceof Promise) {
      Promise promise = (Promise) result;
      try {
        result = Promise.await(promise);
      } catch (YonaException e) {
        throw e;
      } catch (Throwable e) {
        throw new YonaException(e, this);
      }
    }

    if (result instanceof Function) {
      Function function = (Function) result;
      if (function.getCardinality() == 0) {
        return function.getCallTarget().getRootNode().execute(frame);
      }
    }

    return result;
  }

  @Override
  protected String[] requiredIdentifiers() {
    return expressionNode.requiredIdentifiers();
  }
}
