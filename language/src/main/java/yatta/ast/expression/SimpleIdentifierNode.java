package yatta.ast.expression;

import com.oracle.truffle.api.CompilerDirectives;
import yatta.YattaException;
import yatta.ast.ExpressionNode;
import yatta.ast.local.ReadLocalVariableNode;
import yatta.ast.local.ReadLocalVariableNodeGen;
import com.oracle.truffle.api.frame.FrameSlot;
import com.oracle.truffle.api.frame.VirtualFrame;
import yatta.runtime.exceptions.UninitializedFrameSlotException;

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
    CompilerDirectives.transferToInterpreterAndInvalidate();
    FrameSlot frameSlot = frame.getFrameDescriptor().findFrameSlot(name);
    if (frameSlot == null) {
      throw new YattaException("Identifier '" + name + "' not found in the current scope", this);
    }
    ReadLocalVariableNode node = ReadLocalVariableNodeGen.create(frameSlot);
    try {
      return node.executeGeneric(frame);
    } catch (UninitializedFrameSlotException e) {
      throw new YattaException("Identifier '" + name + "' not found in the current scope", this);
    }
  }
}
