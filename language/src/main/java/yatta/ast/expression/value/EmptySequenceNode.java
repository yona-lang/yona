package yatta.ast.expression.value;

import yatta.ast.ExpressionNode;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import yatta.runtime.Seq;

@NodeInfo
public final class EmptySequenceNode extends ExpressionNode {
  public EmptySequenceNode() {
  }

  @Override
  public String toString() {
    return "EmptySequenceNode{}";
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    return Seq.sequence();
  }

  @Override
  public Seq executeSequence(VirtualFrame frame) throws UnexpectedResultException {
    return Seq.sequence();
  }
}
