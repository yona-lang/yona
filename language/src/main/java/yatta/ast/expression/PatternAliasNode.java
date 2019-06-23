package yatta.ast.expression;

import yatta.YattaException;
import yatta.ast.ExpressionNode;
import yatta.ast.pattern.MatchNode;
import yatta.ast.pattern.MatchResult;
import yatta.runtime.Unit;
import yatta.runtime.async.Promise;
import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.frame.MaterializedFrame;
import com.oracle.truffle.api.frame.VirtualFrame;
import yatta.runtime.exceptions.NoMatchException;

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
  public void setIsTail(boolean isTail) {
    super.setIsTail(isTail);
    this.expression.setIsTail(isTail);
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

  private Object execute(Object value, VirtualFrame frame) {
    MatchResult matchResult = matchNode.match(value, frame);
    if (matchResult.isMatches()) {
      for (AliasNode aliasNode : matchResult.getAliases()) {
        aliasNode.executeGeneric(frame);
      }

      return Unit.INSTANCE;
    } else {
      throw new NoMatchException(this);
    }
  }
}
