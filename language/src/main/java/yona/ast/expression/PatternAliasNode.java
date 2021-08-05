package yona.ast.expression;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.frame.MaterializedFrame;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.ast.AliasNode;
import yona.ast.ExpressionNode;
import yona.ast.pattern.MatchNode;
import yona.ast.pattern.MatchResult;
import yona.runtime.Unit;
import yona.runtime.async.Promise;
import yona.runtime.exceptions.NoMatchException;

import java.util.Objects;

@NodeInfo(shortName = "patternAlias")
public final class PatternAliasNode extends AliasNode {
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
    CompilerDirectives.transferToInterpreterAndInvalidate();
    Object value = expression.executeGeneric(frame);

    if (value instanceof Promise) {
      Promise promise = (Promise) value;
      Object unwrappedValue = promise.unwrap();

      if (unwrappedValue != null) {
        return execute(unwrappedValue, frame);
      } else {
        MaterializedFrame materializedFrame = frame.materialize();
        return promise.map(val -> execute(val, materializedFrame), this);
      }
    } else {
      return execute(value, frame);
    }
  }

  private Object execute(Object value, VirtualFrame frame) {
    matchNode.setValue(value);
    MatchResult matchResult = (MatchResult) matchNode.executeGeneric(frame);
    if (matchResult.isMatches()) {
      for (AliasNode nameAliasNode : matchResult.getAliases()) {
        nameAliasNode.executeGeneric(frame);
      }

      return Unit.INSTANCE;
    } else {
      throw new NoMatchException(this, value);
    }
  }

  @Override
  public String[] requiredIdentifiers() {
    return expression.getRequiredIdentifiers();
  }

  @Override
  public String[] providedIdentifiers() {
    return matchNode.getProvidedIdentifiers();
  }
}
