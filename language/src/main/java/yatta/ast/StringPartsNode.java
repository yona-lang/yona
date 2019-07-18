package yatta.ast;

import com.oracle.truffle.api.CompilerAsserts;
import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.ExplodeLoop;
import yatta.runtime.async.Promise;

import java.util.Arrays;

public final class StringPartsNode extends ExpressionNode {
  @Children private ExpressionNode[] expressionNodes;

  public StringPartsNode(ExpressionNode[] expressionNodes) {
    this.expressionNodes = expressionNodes;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    StringPartsNode that = (StringPartsNode) o;
    return Arrays.equals(expressionNodes, that.expressionNodes);
  }

  @Override
  public int hashCode() {
    return Arrays.hashCode(expressionNodes);
  }

  @Override
  public String toString() {
    return "StringPartsNode{" +
        "expressionNodes=" + Arrays.toString(expressionNodes) +
        '}';
  }

  @Override
  @ExplodeLoop
  public Object executeGeneric(VirtualFrame frame) {
    CompilerAsserts.compilationConstant(expressionNodes.length);
    Object[] evaluatedExpressions = new Object[expressionNodes.length];
    boolean isPromise = false;

    for (int i = 0; i < expressionNodes.length; i++) {
      evaluatedExpressions[i] = expressionNodes[i].executeGeneric(frame);
      if (evaluatedExpressions[i] instanceof Promise) {
        isPromise = true;
      } else {
        assert evaluatedExpressions[i] instanceof String;
      }
    }

    if (!isPromise) {
      StringBuilder sb = new StringBuilder();

      CompilerDirectives.transferToInterpreter();

      for (Object evaluatedExpression : evaluatedExpressions) {
        sb.append(evaluatedExpression);
      }

      return sb.toString();
    } else {
      CompilerDirectives.transferToInterpreter();

      return Promise.all(evaluatedExpressions, this).map(vals -> {
        StringBuilder sb = new StringBuilder();
        for (Object val : (Object[]) vals) {
          sb.append(val);
        }

        return sb.toString();
      }, this);
    }
  }
}
