package yona.ast.expression.value;

import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import yona.TypesGen;
import yona.YonaException;
import yona.ast.ExpressionNode;
import yona.runtime.DependencyUtils;
import yona.runtime.Seq;
import yona.runtime.async.Promise;

import java.util.Objects;

@NodeInfo
public final class RangeNode extends ExpressionNode {
  @Child
  public ExpressionNode step;
  @Child
  public ExpressionNode start;
  @Child
  public ExpressionNode end;

  public RangeNode(ExpressionNode step, ExpressionNode start, ExpressionNode end) {
    this.step = step;
    this.start = start;
    this.end = end;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    RangeNode rangeNode = (RangeNode) o;
    return Objects.equals(step, rangeNode.step) && Objects.equals(start, rangeNode.start) && Objects.equals(end, rangeNode.end);
  }

  @Override
  public int hashCode() {
    return Objects.hash(step, start, end);
  }

  @Override
  public String toString() {
    return "RangeNode{" +
        "step=" + step +
        ", start=" + start +
        ", end=" + end +
        '}';
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    Object stepResult = null;
    if (step != null) {
      stepResult = step.executeGeneric(frame);
    }

    Object startResult = start.executeGeneric(frame);
    Object endResult = end.executeGeneric(frame);

    if (stepResult instanceof Promise || startResult instanceof Promise || endResult instanceof Promise) {
      return Promise.all(new Object[]{stepResult, startResult, endResult}, this).map(res -> {
        Object[] args = (Object[]) res;
        return execute(args[0], args[1], args[2]);
      }, this);
    } else {
      return execute(stepResult, startResult, endResult);
    }
  }

  @Override
  protected String[] requiredIdentifiers() {
    return DependencyUtils.catenateRequiredIdentifiers(step, start, end);
  }

  private Seq execute(Object stepResult, Object startResult, Object endResult) {
    try {
      final long stepVal = stepResult != null ? TypesGen.expectLong(stepResult) : 1L;
      final long startVal = TypesGen.expectLong(startResult);
      final long endVal = TypesGen.expectLong(endResult);

      Seq result = Seq.EMPTY;
      for (long i = startVal; i < endVal; i += stepVal) {
        result = result.insertLast(i);
      }

      return result;
    } catch (UnexpectedResultException e) {
      throw YonaException.typeError(this, stepResult, startResult, endResult);
    }
  }
}
