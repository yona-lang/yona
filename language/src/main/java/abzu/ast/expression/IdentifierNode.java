package abzu.ast.expression;

import abzu.AbzuException;
import abzu.AbzuLanguage;
import abzu.ast.ExpressionNode;
import abzu.ast.call.InvokeNode;
import abzu.ast.local.ReadLocalVariableNode;
import abzu.ast.local.ReadLocalVariableNodeGen;
import abzu.runtime.Function;
import abzu.runtime.UninitializedFrameSlot;
import abzu.runtime.UninitializedFrameSlotException;
import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.frame.FrameDescriptor;
import com.oracle.truffle.api.frame.FrameSlot;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.UnexpectedResultException;

public final class IdentifierNode extends ExpressionNode {
  private final String name;
  private boolean functionInvoked;
  private final AbzuLanguage language;

  public IdentifierNode(AbzuLanguage language, String name) {
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
      throw new AbzuException("Identifier '" + name + "' not found in the current scope", this);
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
