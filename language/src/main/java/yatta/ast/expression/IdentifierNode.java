package yatta.ast.expression;

import yatta.YattaException;
import yatta.YattaLanguage;
import yatta.ast.ExpressionNode;
import yatta.ast.call.InvokeNode;
import yatta.ast.local.ReadLocalVariableNode;
import yatta.ast.local.ReadLocalVariableNodeGen;
import yatta.runtime.Function;
import yatta.runtime.UninitializedFrameSlotException;
import com.oracle.truffle.api.frame.FrameSlot;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.UnexpectedResultException;

public final class IdentifierNode extends ExpressionNode {
  private final String name;
  private boolean functionInvoked;
  private final YattaLanguage language;

  public IdentifierNode(YattaLanguage language, String name) {
    this.name = name;
    this.functionInvoked = false;
    this.language = language;
  }

  @Override
  public String toString() {
    return "IdentifierNode{" +
        "name='" + name + '\'' +
        '}';
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    FrameSlot frameSlot = getFrameSlot(frame);
    if (frameSlot == null) {
      throw new YattaException("Identifier '" + name + "' not found in the current scope", this);
    }
    ReadLocalVariableNode node = ReadLocalVariableNodeGen.create(frameSlot);

    if (functionInvoked) {
      return node.executeGeneric(frame);
    } else {
      try {
        Function function = node.executeFunction(frame);

        if (function.getCardinality() == 0) {
          InvokeNode invokeNode = new InvokeNode(language, new SimpleIdentifierNode(name), new ExpressionNode[]{});
          this.replace(invokeNode);
          functionInvoked = true;
          return invokeNode.executeGeneric(frame);
        }
      } catch (UnexpectedResultException e) {
        this.replace(new SimpleIdentifierNode(name));
      }

      return node.executeGeneric(frame);
    }
  }

  public boolean isBound(VirtualFrame frame) {
    try {
      FrameSlot frameSlot = getFrameSlot(frame);
      if (frameSlot == null) {
        return false;
      } else {
        ReadLocalVariableNode node = ReadLocalVariableNodeGen.create(frameSlot);
        node.executeGeneric(frame);
        return true;
      }
    } catch (UninitializedFrameSlotException e) {
      return false;
    }
  }

  private FrameSlot getFrameSlot(VirtualFrame frame) {
    return frame.getFrameDescriptor().findFrameSlot(name);
  }

  public String name() {
    return name;
  }
}
