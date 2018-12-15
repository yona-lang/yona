package abzu.ast.expression.value;

import abzu.ast.ExpressionNode;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;

import java.util.Objects;

@NodeInfo
public final class ValueLiteralNode extends ExpressionNode {
  private final Object value;

  public ValueLiteralNode(Object value) {
    this.value = value;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    ValueLiteralNode that = (ValueLiteralNode) o;
    return Objects.equals(value, that.value);
  }

  @Override
  public int hashCode() {
    return Objects.hash(value);
  }

  @Override
  public String toString() {
    return "ValueLiteralNode{" +
           "value=" + value +
           '}';
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    return value;
  }
}
