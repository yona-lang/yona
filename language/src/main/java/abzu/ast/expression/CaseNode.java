package abzu.ast.expression;

import abzu.AbzuException;
import abzu.ast.ExpressionNode;
import abzu.ast.pattern.MatchException;
import abzu.ast.pattern.PatternMatchable;
import abzu.runtime.async.Promise;
import com.oracle.truffle.api.CompilerAsserts;
import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.frame.MaterializedFrame;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.ExplodeLoop;
import com.oracle.truffle.api.nodes.Node;

import java.util.Arrays;
import java.util.Objects;

public class CaseNode extends ExpressionNode {
  @Node.Child
  public ExpressionNode expression;

  @Node.Children
  public PatternMatchable[] patternNodes;

  public CaseNode(ExpressionNode expression, PatternMatchable[] patternNodes) {
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
  public void setIsTail(boolean isTail) {
    super.setIsTail(isTail);
    for (PatternMatchable patternMatchable : patternNodes) {
      ((ExpressionNode) patternMatchable).setIsTail(isTail);
    }
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    Object value = expression.executeGeneric(frame);

    if (value instanceof Promise) {
      Promise promise = (Promise) value;
      Object unwrappedValue = promise.unwrap();

      if (unwrappedValue != null) {
        return execute(unwrappedValue, frame);
      } else {
        CompilerDirectives.transferToInterpreter();
        MaterializedFrame materializedFrame = frame.materialize();
        return promise.map(val -> execute(val, materializedFrame), this);
      }
    } else {
      return execute(value, frame);
    }
  }

  @ExplodeLoop
  private Object execute(Object value, VirtualFrame frame) {
    CompilerAsserts.compilationConstant(patternNodes.length);
    Object retValue = null;
    for (int i = 0; i < patternNodes.length; i++) {
      try {
        retValue = patternNodes[i].patternMatch(value, frame);
        break;
      } catch (MatchException ex) {
        continue;
      }
    }

    if (retValue != null) {
      return retValue;
    } else {
      throw new AbzuException("MatchException", this);
    }
  }
}
