package yatta.ast.expression;

import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.instrumentation.InstrumentableNode;
import com.oracle.truffle.api.nodes.NodeInterface;

public interface AliasNode extends NodeInterface, InstrumentableNode {
  Object executeGeneric(VirtualFrame frame);
}
