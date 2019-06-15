package yatta.ast.expression.value;

import yatta.ast.ExpressionNode;
import yatta.runtime.Sequence;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;

import java.util.Objects;

@NodeInfo
public final class OneSequenceNode extends ExpressionNode {
  @Child
  public ExpressionNode expression;

  public OneSequenceNode(ExpressionNode expression) {
    this.expression = expression;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    OneSequenceNode sequenceNode = (OneSequenceNode) o;
    return Objects.equals(expression, sequenceNode.expression);
  }

  @Override
  public int hashCode() {
    return Objects.hash(expression);
  }

  @Override
  public String toString() {
    return "OneSequenceNode{" +
        "expression=" + expression +
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
    return Sequence.sequence(expression.executeGeneric(frame));
  }
}
