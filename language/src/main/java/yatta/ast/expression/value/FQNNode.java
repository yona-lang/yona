package yatta.ast.expression.value;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.TruffleLanguage;
import com.oracle.truffle.api.frame.FrameSlot;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import yatta.YattaException;
import yatta.YattaLanguage;
import yatta.ast.ExpressionNode;
import yatta.ast.local.ReadLocalVariableNode;
import yatta.ast.local.ReadLocalVariableNodeGen;
import yatta.runtime.Context;
import yatta.runtime.Module;
import yatta.runtime.UninitializedFrameSlotException;
import yatta.runtime.Unit;

import java.util.Arrays;
import java.util.Objects;

@NodeInfo
public final class FQNNode extends ExpressionNode {
  public final String[] packageParts;
  public final String moduleName;
  private final Context context;

  public FQNNode(String[] packageParts, String moduleName) {
    this.packageParts = packageParts;
    this.moduleName = moduleName;
    this.context = Context.getCurrent();
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    FQNNode fqnNode = (FQNNode) o;
    return Arrays.equals(packageParts, fqnNode.packageParts) &&
        Objects.equals(moduleName, fqnNode.moduleName);
  }

  @Override
  public int hashCode() {
    int result = Objects.hash(moduleName);
    result = 31 * result + Arrays.hashCode(packageParts);
    return result;
  }

  @Override
  public String toString() {
    return "FQNNode{" +
        "packageParts=" + Arrays.toString(packageParts) +
        ", moduleName='" + moduleName + '\'' +
        '}';
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    try {
      return executeModule(frame);
    } catch (UnexpectedResultException e) {
      return context.lookupModule(packageParts, moduleName, this);
    }
  }

  @Override
  public String executeString(VirtualFrame frame) throws UnexpectedResultException {
    return context.getFQN(packageParts, moduleName);
  }

  @Override
  public Module executeModule(VirtualFrame frame) throws UnexpectedResultException {
    try {
      String fqn = Context.getFQN(packageParts, moduleName);
      TruffleLanguage.ContextReference<Context> context = lookupContextReference(YattaLanguage.class);
      Object globalValue = context.get().globals.lookup(fqn);
      if (!Unit.INSTANCE.equals(globalValue)) {
        this.replace(new AnyValueNode(globalValue));
        return (Module) globalValue;
      }

      CompilerDirectives.transferToInterpreterAndInvalidate();
      FrameSlot frameSlot = frame.getFrameDescriptor().findFrameSlot(fqn);
      if (frameSlot != null) {
        ReadLocalVariableNode node = ReadLocalVariableNodeGen.create(frameSlot);
        return node.executeModule(frame);
      }
    } catch (UninitializedFrameSlotException | UnexpectedResultException e) {
      throw new YattaException("Unexpected error while loading a module " + moduleName, e, this);
    }

    return context.lookupModule(packageParts, moduleName, this);
  }
}
