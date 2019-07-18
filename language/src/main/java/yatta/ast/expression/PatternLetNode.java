package yatta.ast.expression;

import yatta.ast.ExpressionNode;
import yatta.runtime.async.Promise;
import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.frame.MaterializedFrame;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.ExplodeLoop;

import java.util.Arrays;
import java.util.Objects;

public final class PatternLetNode extends LexicalScopeNode {
  @Children
  public ExpressionNode[] patternAliases;  // PatternAliasNode | AliasNode
  @Child
  public ExpressionNode expression;

  public PatternLetNode(ExpressionNode[] patternAliases, ExpressionNode expression) {
    this.patternAliases = patternAliases;
    this.expression = expression;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    PatternLetNode letNode = (PatternLetNode) o;
    return Arrays.equals(patternAliases, letNode.patternAliases) &&
        Objects.equals(expression, letNode.expression);
  }

  @Override
  public int hashCode() {
    return Objects.hash(Arrays.hashCode(patternAliases), expression);
  }

  @Override
  public String toString() {
    return "PatternLetNode{" +
        "patterns=" + Arrays.toString(patternAliases) +
        ", expression=" + expression +
        '}';
  }

  @Override
  public void setIsTail(boolean isTail) {
    super.setIsTail(isTail);
    this.expression.setIsTail(isTail);
  }

  @Override
  @ExplodeLoop
  public Object executeGeneric(VirtualFrame frame) {
    Promise promise = null;
    MaterializedFrame materializedFrame = null;

    for (ExpressionNode patternAlias : patternAliases) {
      if (promise == null) {
        Object aliasValue = patternAlias.executeGeneric(frame);

        if (aliasValue instanceof Promise) {
          promise = (Promise) aliasValue;
          CompilerDirectives.transferToInterpreterAndInvalidate();
          materializedFrame = frame.materialize();
        }
      } else {
        final MaterializedFrame finalMaterializedFrame = materializedFrame;
        promise = promise.map(ignore -> patternAlias.executeGeneric(finalMaterializedFrame), this);
      }
    }

    if (promise != null) {
      final MaterializedFrame finalMaterializedFrame = materializedFrame;
      return promise.map(ignore -> expression.executeGeneric(finalMaterializedFrame), this);
    } else {
      return expression.executeGeneric(frame);
    }
  }
}
