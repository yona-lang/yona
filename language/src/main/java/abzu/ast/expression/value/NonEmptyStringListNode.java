package abzu.ast.expression.value;

import abzu.ast.ExpressionNode;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;

import java.util.Objects;

@NodeInfo
public final class NonEmptyStringListNode extends ExpressionNode {
  public final String[] strings;

  public NonEmptyStringListNode(String[] strings) {
    this.strings = strings;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    NonEmptyStringListNode listNode = (NonEmptyStringListNode) o;
    return Objects.equals(strings, listNode.strings);
  }

  @Override
  public int hashCode() {
    return Objects.hash(strings);
  }

  @Override
  public String toString() {
    return "NonEmptyStringListNode{" +
        "strings=" + strings +
        '}';
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    return null;
  }
}
