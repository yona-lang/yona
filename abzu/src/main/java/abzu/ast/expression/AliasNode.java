package abzu.ast.expression;

import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.Node;
import abzu.ast.AbzuExpressionNode;

import java.util.Objects;

public final class AliasNode extends AbzuExpressionNode {
  public final String name;
  @Node.Child
  public AbzuExpressionNode expression;

  public AliasNode(String name, AbzuExpressionNode expression) {
    this.name = name;
    this.expression = expression;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    AliasNode aliasNode = (AliasNode) o;
    return Objects.equals(name, aliasNode.name) &&
        Objects.equals(expression, aliasNode.expression);
  }

  @Override
  public int hashCode() {
    return Objects.hash(name, expression);
  }

  @Override
  public String toString() {
    return "AliasNode{" +
        "name='" + name + '\'' +
        ", expression=" + expression +
        '}';
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    return null;
  }
}
