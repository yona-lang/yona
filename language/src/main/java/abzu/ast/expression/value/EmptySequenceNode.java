package abzu.ast.expression.value;

import abzu.ast.ExpressionNode;
import abzu.runtime.Sequence;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;

@NodeInfo
public final class EmptySequenceNode extends ExpressionNode {
  public static EmptySequenceNode INSTANCE = new EmptySequenceNode();

  private EmptySequenceNode() {
  }

  @Override
  public String toString() {
    return "EmptySequenceNode{}";
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    return Sequence.sequence();
  }

  @Override
  public Sequence executeSequence(VirtualFrame frame) throws UnexpectedResultException {
    return Sequence.sequence();
  }
}
