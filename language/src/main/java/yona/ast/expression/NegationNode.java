package yona.ast.expression;

import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.YonaException;
import yona.ast.ExpressionNode;
import yona.runtime.async.Promise;

import java.util.Objects;

@NodeInfo(shortName = "!")
public final class NegationNode extends ExpressionNode {
  @Node.Child
  public ExpressionNode expression;

  public NegationNode(ExpressionNode expression) {
    this.expression = expression;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    NegationNode that = (NegationNode) o;
    return Objects.equals(expression, that.expression);
  }

  @Override
  public int hashCode() {
    return Objects.hash(expression);
  }

  @Override
  public String toString() {
    return "NegationNode{" +
        "expression=" + expression +
        '}';
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    Object result = expression.executeGeneric(frame);

    if (result instanceof Promise) {
      Promise promise = (Promise) result;
      return promise.map((res) -> {
        if (res instanceof Boolean) {
          return !(boolean) res;
        } else {
          throw YonaException.typeError(this, res);
        }
      }, this);
    } else if (result instanceof Boolean) {
      return !(boolean) result;
    } else {
      throw YonaException.typeError(this, result);
    }
  }

  @Override
  protected String[] requiredIdentifiers() {
    return expression.getRequiredIdentifiers();
  }
}
