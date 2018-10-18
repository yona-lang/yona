package abzu.ast.expression;

import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.Node;
import abzu.ast.AbzuExpressionNode;

import java.util.List;
import java.util.Objects;

public final class LetNode extends LexicalScopeNode {
  @Node.Children
  public List<AliasNode> aliases;
  @Node.Child
  public AbzuExpressionNode expression;

  public LetNode(List<AliasNode> aliases, AbzuExpressionNode expression) {
    this.aliases = aliases;
    this.expression = expression;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    LetNode letNode = (LetNode) o;
    return Objects.equals(aliases, letNode.aliases) &&
        Objects.equals(expression, letNode.expression);
  }

  @Override
  public int hashCode() {
    return Objects.hash(aliases, expression);
  }

  @Override
  public String toString() {
    return "LetNode{" +
        "aliases=" + aliases +
        ", expression=" + expression +
        '}';
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    return expression.executeGeneric(frame);
  }
}
