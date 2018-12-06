package abzu.ast.expression;

import abzu.AbzuException;
import abzu.ast.ExpressionNode;
import abzu.ast.local.ReadLocalVariableNode;
import abzu.ast.local.ReadLocalVariableNodeGen;
import com.oracle.truffle.api.frame.FrameSlot;
import com.oracle.truffle.api.frame.VirtualFrame;

public final class IdentifierNode extends ExpressionNode {
  private String name;

  public IdentifierNode(String name) {
    this.name = name;
  }

  @Override
  public String toString() {
    return "IdentifierNode{" +
           "name='" + name + '\'' +
           '}';
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    FrameSlot frameSlot = frame.getFrameDescriptor().findFrameSlot(name);
    if (frameSlot == null) {
      throw new AbzuException("Identifier '" + name + "' not found in the current scope", this);
    }
    ReadLocalVariableNode node = ReadLocalVariableNodeGen.create(frameSlot);
    return node.executeGeneric(frame);
  }
}
