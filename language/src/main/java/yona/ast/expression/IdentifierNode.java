package yona.ast.expression;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.TruffleLanguage;
import com.oracle.truffle.api.frame.FrameSlot;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import yona.YonaException;
import yona.YonaLanguage;
import yona.ast.ExpressionNode;
import yona.ast.call.InvokeNode;
import yona.ast.expression.value.AnyValueNode;
import yona.ast.local.ReadLocalVariableNode;
import yona.ast.local.ReadLocalVariableNodeGen;
import yona.runtime.Context;
import yona.runtime.Function;
import yona.runtime.Unit;
import yona.runtime.YonaModule;
import yona.runtime.exceptions.UninitializedFrameSlotException;

@NodeInfo(shortName = "identifier")
public final class IdentifierNode extends ExpressionNode {
  private final String name;
  private final YonaLanguage language;
  @Children
  private final ExpressionNode[] moduleStack;  // FQNNode or AnyValueNode

  public IdentifierNode(YonaLanguage language, String name, ExpressionNode[] moduleStack) {
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
    TruffleLanguage.ContextReference<Context> context = lookupContextReference(YonaLanguage.class);
    Object globalValue = context.get().globals.lookup(name);
    if (!Unit.INSTANCE.equals(globalValue)) {
      if (globalValue instanceof Function && ((Function) globalValue).getCardinality() == 0) {
        CompilerDirectives.transferToInterpreterAndInvalidate();
        InvokeNode invokeNode = new InvokeNode(language, (Function) globalValue, new ExpressionNode[]{}, moduleStack);
        this.replace(invokeNode);
        return invokeNode.executeGeneric(frame);
      } else {
        CompilerDirectives.transferToInterpreterAndInvalidate();
        this.replace(new AnyValueNode(globalValue));
        return globalValue;
      }
    }

    CompilerDirectives.transferToInterpreterAndInvalidate();
    FrameSlot frameSlot = getFrameSlot(frame);
    if (moduleStack.length > 0) {
      for (int i = moduleStack.length - 1; i >= 0; i--) {
        try {
          YonaModule module = moduleStack[i].executeModule(frame);
          if (module.getFunctions().containsKey(name)) {
            CompilerDirectives.transferToInterpreterAndInvalidate();
            InvokeNode invokeNode = new InvokeNode(language, module.getFunctions().get(name), new ExpressionNode[]{}, moduleStack);
            this.replace(invokeNode);
            return invokeNode.executeGeneric(frame);
          }
        } catch (UnexpectedResultException e) {
          continue;
        } catch (YonaException e) {  // IO error
          continue;
        }
      }
    }

    if (frameSlot != null) {
      ReadLocalVariableNode node = ReadLocalVariableNodeGen.create(frameSlot);
      Object result;
      try {
        result = node.executeGeneric(frame);
      } catch (UninitializedFrameSlotException ignored) {
        throw new YonaException("Identifier '" + name + "' not found in the current scope", this);
      }

      if (result instanceof Function function) {
        if (function.getCardinality() == 0) {
          InvokeNode invokeNode = new InvokeNode(language, new SimpleIdentifierNode(name), new ExpressionNode[]{}, moduleStack);
          this.replace(invokeNode);
          return invokeNode.executeGeneric(frame);
        }
      }

      CompilerDirectives.transferToInterpreterAndInvalidate();
      this.replace(node);
      return result;
    } else {
      throw new YonaException("Identifier '" + name + "' not found in the current scope", this);
    }
  }

  @Override
  public String[] requiredIdentifiers() {
    return new String[]{name};
  }

  public boolean isBound(VirtualFrame frame) {
    TruffleLanguage.ContextReference<Context> context = lookupContextReference(YonaLanguage.class);
    if (context.get().globals.lookup(name) != Unit.INSTANCE) {
      return true;
    }
    FrameSlot frameSlot = getFrameSlot(frame);
    if (frameSlot == null) {
      return false;
    } else {
      CompilerDirectives.transferToInterpreterAndInvalidate();
      ReadLocalVariableNode node = ReadLocalVariableNodeGen.create(frameSlot);
      try {
        node.executeGeneric(frame);
        return true;
      } catch (UninitializedFrameSlotException ignored) {
        return false;
      }
    }
  }

  private FrameSlot getFrameSlot(VirtualFrame frame) {
    CompilerDirectives.transferToInterpreterAndInvalidate();
    return frame.getFrameDescriptor().findFrameSlot(name);
  }

  public String name() {
    return name;
  }
}
