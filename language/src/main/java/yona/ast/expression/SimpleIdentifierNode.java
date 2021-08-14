package yona.ast.expression;

import com.oracle.truffle.api.frame.FrameSlot;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.YonaException;
import yona.ast.ExpressionNode;
import yona.ast.local.ReadLocalVariableNode;
import yona.ast.local.ReadLocalVariableNodeGen;
import yona.runtime.UninitializedFrameSlot;

@NodeInfo(shortName = "simpleIdentifier")
public final class SimpleIdentifierNode extends ExpressionNode {
  public final String name;

  public SimpleIdentifierNode(String name) {
    this.name = name;
  }

  @Override
  public String toString() {
    return "SimpleIdentifierNode{" +
           "name='" + name + '\'' +
           '}';
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    FrameSlot frameSlot = frame.getFrameDescriptor().findFrameSlot(name);
    if (frameSlot == null) {
      throw new YonaException("Identifier '" + name + "' not found in the current scope", this);
    }
    ReadLocalVariableNode node = ReadLocalVariableNodeGen.create(frameSlot);
    Object result = node.executeGeneric(frame);
    if (result == UninitializedFrameSlot.INSTANCE) {
      throw new YonaException("Identifier '" + name + "' not found in the current scope", this);
    } else {
      return result;
    }
  }

  @Override
  protected String[] requiredIdentifiers() {
    return new String[]{name};
  }
}
