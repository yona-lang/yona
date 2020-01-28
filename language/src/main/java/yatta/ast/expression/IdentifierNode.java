package yatta.ast.expression;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.TruffleLanguage;
import com.oracle.truffle.api.frame.FrameSlot;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import yatta.YattaException;
import yatta.YattaLanguage;
import yatta.ast.ExpressionNode;
import yatta.ast.call.InvokeNode;
import yatta.ast.expression.value.AnyValueNode;
import yatta.ast.expression.value.FQNNode;
import yatta.ast.local.ReadLocalVariableNode;
import yatta.ast.local.ReadLocalVariableNodeGen;
import yatta.runtime.*;
import yatta.runtime.exceptions.UninitializedFrameSlotException;

@NodeInfo
public final class IdentifierNode extends ExpressionNode {
  private final String name;
  private final YattaLanguage language;
  @Children
  private FQNNode[] moduleStack;

  public IdentifierNode(YattaLanguage language, String name, FQNNode[] moduleStack) {
    this.name = name;
    this.language = language;
    this.moduleStack = moduleStack;
  }

  @Override
  public String toString() {
    return "IdentifierNode{" +
        "name='" + name + '\'' +
        '}';
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    TruffleLanguage.ContextReference<Context> context = lookupContextReference(YattaLanguage.class);
    Object globalValue = context.get().globals.lookup(name);
    if (!Unit.INSTANCE.equals(globalValue)) {
      this.replace(new AnyValueNode(globalValue));
      return globalValue;
    }

    CompilerDirectives.transferToInterpreterAndInvalidate();
    FrameSlot frameSlot = getFrameSlot(frame);
    if (frameSlot == null) {
      if (moduleStack.length > 0) {
        for (int i = moduleStack.length - 1; i >= 0; i--) {
          try {
            YattaModule module = moduleStack[i].executeModule(frame);
            if (module.getFunctions().containsKey(name)) {
              InvokeNode invokeNode = new InvokeNode(language, module.getFunctions().get(name), new ExpressionNode[]{}, moduleStack);
              this.replace(invokeNode);
              return invokeNode.executeGeneric(frame);
            }
          } catch (UnexpectedResultException e) {
            continue;
          } catch (YattaException e) {  // IO error
            continue;
          }
        }
      }

      throw new YattaException("Identifier '" + name + "' not found in the current scope", this);
    }

    ReadLocalVariableNode node = ReadLocalVariableNodeGen.create(frameSlot);

    try {
      Function function = node.executeFunction(frame);

      if (function.getCardinality() == 0) {
        InvokeNode invokeNode = new InvokeNode(language, new SimpleIdentifierNode(name), new ExpressionNode[]{}, moduleStack);
        this.replace(invokeNode);
        return invokeNode.executeGeneric(frame);
      }
    } catch (UnexpectedResultException e) {
      this.replace(new SimpleIdentifierNode(name));
    }

    this.replace(node);
    return node.executeGeneric(frame);
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
