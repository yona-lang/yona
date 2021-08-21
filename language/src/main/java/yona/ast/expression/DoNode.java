package yona.ast.expression;

import com.oracle.truffle.api.CompilerAsserts;
import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.frame.MaterializedFrame;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.TypesGen;
import yona.ast.ExpressionNode;
import yona.runtime.DependencyUtils;
import yona.runtime.async.Promise;

import java.util.Arrays;

@NodeInfo(shortName = "do")
public final class DoNode extends ExpressionNode {
  @Children
  public ExpressionNode[] steps;  // PatternAliasNode | AliasNode | ExpressionNode

  public DoNode(ExpressionNode[] steps) {
    this.steps = steps;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    DoNode doNode = (DoNode) o;
    return Arrays.equals(steps, doNode.steps);
  }

  @Override
  public int hashCode() {
    return Arrays.hashCode(steps);
  }

  @Override
  public String toString() {
    return "DoNode{" +
        "steps=" + Arrays.toString(steps) +
        '}';
  }

  @Override
  public void setIsTail(boolean isTail) {
    super.setIsTail(isTail);
    steps[steps.length - 1].setIsTail(isTail);
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    CompilerAsserts.compilationConstant(steps.length);

    Promise promise = null;
    MaterializedFrame materializedFrame = null;
    Object result = null;

    for (ExpressionNode step : steps) {
      if (promise == null) {
        result = step.executeGeneric(frame);

        if (result instanceof Promise) {
          promise = (Promise) result;
        }
      } else {
        if (materializedFrame == null) {
          materializedFrame = frame.materialize();
        }

        final MaterializedFrame finalMaterializedFrame = materializedFrame;
        promise = promise.map(ignore -> step.executeGeneric(finalMaterializedFrame), this);
      }
    }

    if (promise != null) {
      return promise;
    } else {
      return TypesGen.ensureNotNull(result);
    }
  }

  @Override
  protected String[] requiredIdentifiers() {
    return DependencyUtils.catenateRequiredIdentifiers(steps);
  }
}
