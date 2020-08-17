package yona.ast.call;

import com.oracle.truffle.api.dsl.NodeFactory;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.ast.ExpressionNode;
import yona.ast.builtin.BuiltinNode;
import yona.ast.local.ReadArgumentNode;

@NodeInfo
public final class BuiltinCallNode extends ExpressionNode {
  @Child
  private BuiltinNode builtinNode;

  public BuiltinCallNode(NodeFactory<? extends BuiltinNode> nodeFactory) {
    int argumentsCount = nodeFactory.getExecutionSignature().size();
    ExpressionNode[] arguments = new ExpressionNode[argumentsCount];

    for (int i = 0; i < argumentsCount; i++) {
      arguments[i] = new ReadArgumentNode(i);
    }

    builtinNode = nodeFactory.createNode((Object) arguments);
  }

  @Override
  public void setIsTail(boolean isTail) {
    super.setIsTail(isTail);
    builtinNode.setIsTail(isTail);
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    return builtinNode.executeGeneric(frame);
  }

  @Override
  public String[] requiredIdentifiers() {
    return builtinNode.getRequiredIdentifiers();
  }
}
