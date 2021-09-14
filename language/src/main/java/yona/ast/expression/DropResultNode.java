package yona.ast.expression;

import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.ExplodeLoop;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.ast.ExpressionNode;
import yona.runtime.Unit;

import java.util.Objects;

@NodeInfo(shortName = "dropResult")
public final class DropResultNode extends LexicalScopeNode {
  @Child
  public ExpressionNode expression;

  public DropResultNode(ExpressionNode expression) {
    this.expression = expression;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    DropResultNode that = (DropResultNode) o;
    return Objects.equals(expression, that.expression);
  }

  @Override
  public int hashCode() {
    return Objects.hash(expression);
  }

  @Override
  public String toString() {
    return "DropResultNode{" +
        "expression=" + expression +
        '}';
  }

  @Override
  public void setIsTail(boolean isTail) {
    super.setIsTail(isTail);
    this.expression.setIsTail(isTail);
  }

  @Override
  @ExplodeLoop
  public Object executeGeneric(VirtualFrame frame) {
    expression.executeGeneric(frame);
    return Unit.INSTANCE;
  }

  @Override
  protected String[] requiredIdentifiers() {
    return expression.getRequiredIdentifiers();
  }
}
