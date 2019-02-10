package abzu.ast.pattern;

import abzu.ast.ExpressionNode;
import abzu.ast.expression.LetNode;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.Node;

import java.util.Objects;

public class PatternNode extends ExpressionNode {
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

  public Object patternMatch(Object value, VirtualFrame frame) throws MatchException {
    try {
      MatchResult matchResult = matchExpression.match(value, frame);
      if (matchResult.isMatches()) {
        LetNode letNode = new LetNode(matchResult.getAliases(), valueExpression);
        return letNode.executeGeneric(frame);
      } else {
        throw MatchException.INSTANCE;
      }
    } catch (CurriedFunctionMatchException e) {
      throw MatchException.INSTANCE;
    }
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    return null;
  }
}
