package abzu.ast.expression.value;

import abzu.ast.ExpressionNode;
import abzu.runtime.Sequence;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;

import java.util.Objects;

@NodeInfo
public final class TwoSequenceNode extends ExpressionNode {
  @Child public ExpressionNode expressionOne;
  @Child public ExpressionNode expressionTwo;

  public TwoSequenceNode(ExpressionNode expressionOne, ExpressionNode expressionTwo) {
    this.expressionOne = expressionOne;
    this.expressionTwo = expressionTwo;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    TwoSequenceNode that = (TwoSequenceNode) o;
    return Objects.equals(expressionOne, that.expressionOne) &&
        Objects.equals(expressionTwo, that.expressionTwo);
  }

  @Override
  public int hashCode() {
    return Objects.hash(expressionOne, expressionTwo);
  }

  @Override
  public String toString() {
    return "TwoSequenceNode{" +
        "expressionOne=" + expressionOne +
        ", expressionTwo=" + expressionTwo +
        '}';
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    return execute(frame);
  }

  @Override
  public Sequence executeSequence(VirtualFrame frame) throws UnexpectedResultException {
    return execute(frame);
  }

  private Sequence execute(VirtualFrame frame) {
    return Sequence.sequence(expressionOne.executeGeneric(frame), expressionTwo.executeGeneric(frame));
  }
}
