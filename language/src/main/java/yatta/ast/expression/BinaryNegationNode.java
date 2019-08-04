package yatta.ast.expression;

import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import yatta.YattaException;
import yatta.ast.ExpressionNode;
import yatta.runtime.async.Promise;

import java.util.Objects;

@NodeInfo(shortName = "~")
public final class BinaryNegationNode extends ExpressionNode {
  @Child
  public ExpressionNode expression;

  public BinaryNegationNode(ExpressionNode expression) {
    this.expression = expression;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    BinaryNegationNode that = (BinaryNegationNode) o;
    return Objects.equals(expression, that.expression);
  }

  @Override
  public int hashCode() {
    return Objects.hash(expression);
  }

  @Override
  public String toString() {
    return "BinaryNegationNode{" +
        "expression=" + expression +
        '}';
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    Object result = expression.executeGeneric(frame);

    if (result instanceof Promise) {
      Promise promise = (Promise) result;
      return promise.map((res) -> {
        if (res instanceof Long) {
          return ~(long) res;
        } else {
          throw YattaException.typeError(this, res);
        }
      }, this);
    } else if (result instanceof Long) {
      return ~(long) result;
    } else {
      throw YattaException.typeError(this, result);
    }
  }
}
