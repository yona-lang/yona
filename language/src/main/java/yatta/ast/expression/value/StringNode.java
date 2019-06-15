package yatta.ast.expression.value;

import yatta.ast.ExpressionNode;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;

import java.util.Objects;

@NodeInfo
public final class StringNode extends ExpressionNode {
  public final String value;

  public StringNode(String value) {
    this.value = value;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    StringNode that = (StringNode) o;
    return Objects.equals(value, that.value);
  }

  @Override
  public int hashCode() {
    return Objects.hash(value);
  }

  @Override
  public String toString() {
    return "StringNode{" +
        "value='" + value + '\'' +
        '}';
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    return value;
  }

  @Override
  public String executeString(VirtualFrame frame) throws UnexpectedResultException {
    return value;
  }
}
