package abzu.ast.expression;

import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.Node;
import abzu.ast.AbzuExpressionNode;

import java.util.Objects;

public final class NegationNode extends AbzuExpressionNode {
  @Node.Child
  public AbzuExpressionNode expression;

  public NegationNode(AbzuExpressionNode expression) {
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
    return expression.executeGeneric(frame);
  }
}
