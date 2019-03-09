package abzu.ast.expression;

import abzu.ast.ExpressionNode;
import abzu.ast.pattern.MatchException;
import abzu.ast.pattern.MatchNode;
import abzu.ast.pattern.MatchResult;
import com.oracle.truffle.api.frame.VirtualFrame;

import java.util.Objects;

public final class PatternAliasNode extends ExpressionNode {
  @Child
  public MatchNode matchNode;
  @Child
  public ExpressionNode expression;

  public PatternAliasNode(MatchNode matchNode, ExpressionNode expression) {
    this.matchNode = matchNode;
    this.expression = expression;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    PatternAliasNode aliasNode = (PatternAliasNode) o;
    return Objects.equals(matchNode, aliasNode.matchNode) &&
        Objects.equals(expression, aliasNode.expression);
  }

  @Override
  public int hashCode() {
    return Objects.hash(matchNode, expression);
  }

  @Override
  public String toString() {
    return "PatternAliasNode{" +
        "matchNode='" + matchNode + '\'' +
        ", expression=" + expression +
        '}';
  }

  @Override
  public void setIsTail() {
    super.setIsTail();
    this.expression.setIsTail();
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    Object value = expression.executeGeneric(frame);
    MatchResult matchResult = matchNode.match(value, frame);
    if (matchResult.isMatches()) {
      for (AliasNode aliasNode : matchResult.getAliases()) {
        aliasNode.executeGeneric(frame);
      }

      return null;
    } else {
      throw MatchException.INSTANCE;
    }
  }
}
