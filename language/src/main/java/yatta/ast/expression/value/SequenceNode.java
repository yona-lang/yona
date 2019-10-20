package yatta.ast.expression.value;

import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import yatta.ast.ExpressionNode;
import yatta.runtime.Seq;

import java.util.Objects;

@NodeInfo
public final class SequenceNode extends ExpressionNode {
  @Node.Children
  public final ExpressionNode[] expressions;

  public SequenceNode(ExpressionNode[] expressions) {
    this.expressions = expressions;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    SequenceNode sequenceNode = (SequenceNode) o;
    return Objects.equals(expressions, sequenceNode.expressions);
  }

  @Override
  public int hashCode() {
    return Objects.hash(expressions);
  }

  @Override
  public String toString() {
    return "SequenceNode{" +
        "expressions=" + expressions +
        '}';
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    return execute(frame);
  }

  @Override
  public Seq executeSequence(VirtualFrame frame) throws UnexpectedResultException {
    return execute(frame);
  }

  private Seq execute(VirtualFrame frame) {
    Object[] values = new Object[expressions.length];
    int i = 0;
    for (ExpressionNode expressionNode : expressions) {
      values[i++] = expressionNode.executeGeneric(frame);
    }

    return Seq.sequence(values);
  }
}
