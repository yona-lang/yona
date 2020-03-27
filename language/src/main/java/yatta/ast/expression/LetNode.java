package yatta.ast.expression;

import yatta.ast.ExpressionNode;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.ExplodeLoop;
import com.oracle.truffle.api.nodes.Node;

import java.util.Arrays;
import java.util.Objects;

public final class LetNode extends LexicalScopeNode {
  @Node.Children
  public NameAliasNode[] aliases;
  @Node.Child
  public ExpressionNode expression;

  public LetNode(NameAliasNode[] aliases, ExpressionNode expression) {
    this.aliases = aliases;
    this.expression = expression;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    LetNode letNode = (LetNode) o;
    return Arrays.equals(aliases, letNode.aliases) &&
        Objects.equals(expression, letNode.expression);
  }

  @Override
  public int hashCode() {
    return Objects.hash(Arrays.hashCode(aliases), expression);
  }

  @Override
  public String toString() {
    return "LetNode{" +
        "aliases=" + Arrays.toString(aliases) +
        ", expression=" + expression +
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
    for (NameAliasNode alias : aliases) {
      alias.executeGeneric(frame);
    }
    return expression.executeGeneric(frame);
  }
}
