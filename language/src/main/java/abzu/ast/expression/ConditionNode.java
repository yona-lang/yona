package abzu.ast.expression;

import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import com.oracle.truffle.api.profiles.ConditionProfile;
import abzu.AbzuException;
import abzu.ast.AbzuExpressionNode;

import java.util.Objects;

public final class ConditionNode extends AbzuExpressionNode {
  @Node.Child
  public AbzuExpressionNode ifExpression;
  @Node.Child
  public AbzuExpressionNode thenExpression;
  @Node.Child
  public AbzuExpressionNode elseExpression;

  /**
   * Profiling information, collected by the interpreter, capturing the profiling information of
   * the condition. This allows the compiler to generate better code for conditions that are
   * always true or always false. Additionally the {@link CountingConditionProfile} implementation
   * (as opposed to {@link BinaryConditionProfile} implementation) transmits the probability of
   * the condition to be true to the compiler.
   */
  private final ConditionProfile condition = ConditionProfile.createCountingProfile();

  public ConditionNode(AbzuExpressionNode ifExpression, AbzuExpressionNode thenExpression, AbzuExpressionNode elseExpression) {
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
  public Object executeGeneric(VirtualFrame frame) {
    /*
     * In the interpreter, record profiling information that the condition was executed and with
     * which outcome.
     */
    if (condition.profile(evaluateCondition(frame))) {
      return thenExpression.executeGeneric(frame);
    } else {
      return elseExpression.executeGeneric(frame);
    }
  }

  private boolean evaluateCondition(VirtualFrame frame) {
    try {
      /*
       * The condition must evaluate to a boolean value, so we call the boolean-specialized
       * execute method.
       */
      return ifExpression.executeBoolean(frame);
    } catch (UnexpectedResultException ex) {
      /*
       * The condition evaluated to a non-boolean result. This is a type error in the Abzu
       * program.
       */
      throw AbzuException.typeError(this, ex.getResult());
    }
  }
}
