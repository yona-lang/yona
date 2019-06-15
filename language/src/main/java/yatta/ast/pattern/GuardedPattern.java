package yatta.ast.pattern;

import yatta.ast.ExpressionNode;
import yatta.ast.expression.ConditionNode;
import yatta.ast.expression.LetNode;
import yatta.ast.expression.ThrowNode;
import com.oracle.truffle.api.frame.VirtualFrame;

import java.util.Objects;

public class GuardedPattern extends ExpressionNode implements PatternMatchable {
  @Child
  public MatchNode matchExpression;
  @Child
  public ExpressionNode valueExpression;
  @Child
  public ExpressionNode guardExpression;

  public GuardedPattern(MatchNode matchExpression, ExpressionNode guardExpression, ExpressionNode valueExpression) {
    this.matchExpression = matchExpression;
    this.guardExpression = guardExpression;
    this.valueExpression = valueExpression;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    GuardedPattern that = (GuardedPattern) o;
    return Objects.equals(matchExpression, that.matchExpression) &&
        Objects.equals(valueExpression, that.valueExpression) &&
        Objects.equals(guardExpression, that.guardExpression);
  }

  @Override
  public int hashCode() {
    return Objects.hash(matchExpression, valueExpression, guardExpression);
  }

  @Override
  public String toString() {
    return "GuardedPattern{" +
        "matchExpression=" + matchExpression +
        ", valueExpression=" + valueExpression +
        ", guardExpression=" + guardExpression +
        '}';
  }

  @Override
  public void setIsTail(boolean isTail) {
    super.setIsTail(isTail);
    valueExpression.setIsTail(isTail);
  }

  @Override
  public Object patternMatch(Object value, VirtualFrame frame) throws MatchException {
    MatchResult matchResult = matchExpression.match(value, frame);
    if (matchResult.isMatches()) {
      ConditionNode ifNode = new ConditionNode(guardExpression, valueExpression, new ThrowNode(MatchException.INSTANCE));
      LetNode letNode = new LetNode(matchResult.getAliases(), ifNode);
      return letNode.executeGeneric(frame);
    } else {
      throw MatchException.INSTANCE;
    }
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    return null;
  }
}
