package yatta.ast.expression;

import yatta.ast.ExpressionNode;
import com.oracle.truffle.api.frame.VirtualFrame;

import java.util.Objects;

public class ThrowNode extends ExpressionNode {
  public final RuntimeException exception;

  public ThrowNode(RuntimeException exception) {
    this.exception = exception;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    ThrowNode throwNode = (ThrowNode) o;
    return Objects.equals(exception, throwNode.exception);
  }

  @Override
  public int hashCode() {
    return Objects.hash(exception);
  }

  @Override
  public String toString() {
    return "ThrowNode{" +
        "exception=" + exception +
        '}';
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    throw exception;
  }
}
