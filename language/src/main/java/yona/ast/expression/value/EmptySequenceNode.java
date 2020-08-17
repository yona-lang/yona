package yona.ast.expression.value;

import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import yona.runtime.Seq;

@NodeInfo
public final class EmptySequenceNode extends LiteralValueNode {
  public EmptySequenceNode() {
  }

  @Override
  public String toString() {
    return "EmptySequenceNode{}";
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    return Seq.EMPTY;
  }

  @Override
  public Seq executeSequence(VirtualFrame frame) throws UnexpectedResultException {
    return Seq.EMPTY;
  }
}
