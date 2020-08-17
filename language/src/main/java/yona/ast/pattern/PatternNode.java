package yona.ast.pattern;

import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.ast.AliasNode;
import yona.ast.ExpressionNode;
import yona.runtime.DependencyUtils;

import java.util.Objects;

@NodeInfo(shortName = "patternNode")
public class PatternNode extends PatternMatchable {
  @Node.Child
  public MatchNode matchExpression;

  @Node.Child
  public ExpressionNode valueExpression;

  public PatternNode(MatchNode matchExpression, ExpressionNode valueExpression) {
    this.matchExpression = matchExpression;
    this.valueExpression = valueExpression;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    PatternNode that = (PatternNode) o;
    return Objects.equals(matchExpression, that.matchExpression) &&
        Objects.equals(valueExpression, that.valueExpression);
  }

  @Override
  public int hashCode() {
    return Objects.hash(matchExpression, valueExpression);
  }

  @Override
  public String toString() {
    return "PatternNode{" +
        "matchExpression=" + matchExpression +
        ", valueExpression=" + valueExpression +
        '}';
  }

  @Override
  public void setIsTail(boolean isTail) {
    super.setIsTail(isTail);
    this.valueExpression.setIsTail(isTail);
  }

  @Override
  public Object patternMatch(Object value, VirtualFrame frame) throws MatchControlFlowException {
    MatchResult matchResult = matchExpression.match(value, frame);
    if (matchResult.isMatches()) {
      for (AliasNode nameAliasNode : matchResult.getAliases()) {
        nameAliasNode.executeGeneric(frame);
      }
      return valueExpression.executeGeneric(frame);
    } else {
      throw MatchControlFlowException.INSTANCE;
    }
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    return null;
  }

  @Override
  protected String[] requiredIdentifiers() {
    return DependencyUtils.catenateRequiredIdentifiers(matchExpression, valueExpression);
  }
}
