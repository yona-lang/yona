package yona.ast.pattern;

import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.ast.AliasNode;
import yona.ast.ExpressionNode;
import yona.ast.expression.ConditionNode;
import yona.ast.expression.ThrowNode;
import yona.runtime.DependencyUtils;

import java.util.Objects;

@NodeInfo(shortName = "guardedPattern")
public final class GuardedPattern extends PatternMatchable {
  @Child
  public MatchNode matchExpression;
  @Child
  public ConditionNode conditionNode;

  public Object value;

  public GuardedPattern(MatchNode matchExpression, ExpressionNode guardExpression, ExpressionNode valueExpression) {
    this.matchExpression = matchExpression;
    this.conditionNode = new ConditionNode(guardExpression, valueExpression, new ThrowNode(MatchControlFlowException.INSTANCE));
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    GuardedPattern that = (GuardedPattern) o;
    return Objects.equals(matchExpression, that.matchExpression) &&
        Objects.equals(conditionNode, that.conditionNode);
  }

  @Override
  public int hashCode() {
    return Objects.hash(matchExpression, conditionNode);
  }

  @Override
  public String toString() {
    return "GuardedPattern{" +
        "matchExpression=" + matchExpression +
        ", conditionNode=" + conditionNode +
        '}';
  }

  @Override
  public void setIsTail(boolean isTail) {
    super.setIsTail(isTail);
    conditionNode.setIsTail(isTail);
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    matchExpression.setValue(value);
    MatchResult matchResult = (MatchResult) matchExpression.executeGeneric(frame);
    if (matchResult.isMatches()) {
      for (AliasNode aliasNode : matchResult.getAliases()) {
        aliasNode.executeGeneric(frame);
      }
      return conditionNode.executeGeneric(frame);
    } else {
      throw MatchControlFlowException.INSTANCE;
    }
  }

  @Override
  protected String[] requiredIdentifiers() {
    return DependencyUtils.catenateRequiredIdentifiers(matchExpression, conditionNode);
  }

  @Override
  public void setValue(Object value) {
    this.value = value;
  }
}
