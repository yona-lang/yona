package abzu.ast.expression;

import abzu.ast.ExpressionNode;
import abzu.ast.pattern.MatchException;
import abzu.ast.pattern.PatternNode;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.Node;

import java.util.Arrays;
import java.util.Objects;

public class CaseNode extends ExpressionNode {
  @Node.Child
  public ExpressionNode expression;

  @Node.Children
  public PatternNode[] patternNodes;

  public CaseNode(ExpressionNode expression, PatternNode[] patternNodes) {
    this.expression = expression;
    this.patternNodes = patternNodes;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    CaseNode caseNode = (CaseNode) o;
    return Objects.equals(expression, caseNode.expression) &&
        Arrays.equals(patternNodes, caseNode.patternNodes);
  }

  @Override
  public int hashCode() {
    int result = Objects.hash(expression);
    result = 31 * result + Arrays.hashCode(patternNodes);
    return result;
  }

  @Override
  public String toString() {
    return "CaseNode{" +
        "expression=" + expression +
        ", patternNodes=" + Arrays.toString(patternNodes) +
        '}';
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    Object value = expression.executeGeneric(frame);
    Object retValue = null;

    for (PatternNode patternNode : patternNodes) {
      try {
        retValue = patternNode.patternMatch(value, frame);
        break;
      } catch (MatchException ex) {
        continue;
      }
    }

    if (retValue != null) {
      return retValue;
    } else {
      throw MatchException.INSTANCE;
    }
  }
}
