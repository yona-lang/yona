package abzu.ast.expression.value;

import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.nodes.NodeInfo;
import abzu.ast.AbzuExpressionNode;
import abzu.ast.expression.ValueNode;

import java.util.Objects;

@NodeInfo
public final class ListNode extends ValueNode {
  @Node.Children
  public final AbzuExpressionNode[] expressions;

  public ListNode(AbzuExpressionNode[] expressions) {
    this.expressions = expressions;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    ListNode listNode = (ListNode) o;
    return Objects.equals(expressions, listNode.expressions);
  }

  @Override
  public int hashCode() {
    return Objects.hash(expressions);
  }

  @Override
  public String toString() {
    return "ListNode{" +
        "expressions=" + expressions +
        '}';
  }

  @Override
  public Object executeValue(VirtualFrame frame) {
    return null;
  }
}
