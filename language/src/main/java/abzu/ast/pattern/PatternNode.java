package abzu.ast.pattern;

import abzu.ast.ExpressionNode;
import abzu.ast.expression.LetNode;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.Node;

public class PatternNode extends ExpressionNode {
  @Node.Child
  public MatchNode matchExpression;

  @Node.Child
  public ExpressionNode valueExpression;

  public PatternNode(MatchNode matchExpression, ExpressionNode valueExpression) {
    this.matchExpression = matchExpression;
    this.valueExpression = valueExpression;
  }

  public Object patternMatch(Object value, VirtualFrame frame) throws MatchException {
    MatchResult matchResult = matchExpression.match(value, frame);
    if (matchResult.isMatches()) {
      LetNode letNode = new LetNode(matchResult.getAliases(), valueExpression);
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
