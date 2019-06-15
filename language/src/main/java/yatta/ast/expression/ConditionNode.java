package yatta.ast.expression;

import yatta.ast.ExpressionNode;
import yatta.runtime.async.Promise;
import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.frame.MaterializedFrame;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.profiles.ConditionProfile;
import yatta.YattaException;

import java.util.Objects;

public final class ConditionNode extends ExpressionNode {
  @Node.Child
  public ExpressionNode ifExpression;
  @Node.Child
  public ExpressionNode thenExpression;
  @Node.Child
  public ExpressionNode elseExpression;

  /**
   * Profiling information, collected by the interpreter, capturing the profiling information of
   * the condition. This allows the compiler to generate better code for conditions that are
   * always true or always false. Additionally the {@link ConditionProfile.Counting} implementation
   * (as opposed to {@link ConditionProfile.Binary} implementation) transmits the probability of
   * the condition to be true to the compiler.
   */
  private final ConditionProfile condition = ConditionProfile.createCountingProfile();

  public ConditionNode(ExpressionNode ifExpression, ExpressionNode thenExpression, ExpressionNode elseExpression) {
    this.ifExpression = ifExpression;
    this.thenExpression = thenExpression;
    this.elseExpression = elseExpression;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    ConditionNode that = (ConditionNode) o;
    return Objects.equals(ifExpression, that.ifExpression) &&
        Objects.equals(thenExpression, that.thenExpression) &&
        Objects.equals(elseExpression, that.elseExpression);
  }

  @Override
  public int hashCode() {
    return Objects.hash(ifExpression, thenExpression, elseExpression);
  }

  @Override
  public String toString() {
    return "ConditionNode{" +
        "ifExpression=" + ifExpression +
        ", thenExpression=" + thenExpression +
        ", elseExpression=" + elseExpression +
        '}';
  }

  @Override
  public void setIsTail(boolean isTail) {
    super.setIsTail(isTail);
    this.thenExpression.setIsTail(isTail);
    this.elseExpression.setIsTail(isTail);
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    /*
     * In the interpreter, record profiling information that the condition was executed and with
     * which outcome.
     */

    Object condValue = ifExpression.executeGeneric(frame);

    if (condValue instanceof Promise) {
      Promise promise = (Promise) condValue;
      CompilerDirectives.transferToInterpreter();
      MaterializedFrame materializedFrame = frame.materialize();

      return promise.map(val -> {
        if ((boolean) val) {
          return thenExpression.executeGeneric(materializedFrame);
        } else {
          return elseExpression.executeGeneric(materializedFrame);
        }
      }, this);
    } else if (condValue instanceof Boolean) {
      if (condition.profile((boolean) condValue)) {
        return thenExpression.executeGeneric(frame);
      } else {
        return elseExpression.executeGeneric(frame);
      }
    } else {
      throw YattaException.typeError(this, condValue);
    }
  }
}
