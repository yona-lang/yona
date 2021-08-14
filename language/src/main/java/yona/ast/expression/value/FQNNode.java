package yona.ast.expression.value;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.frame.FrameSlot;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import yona.YonaException;
import yona.YonaLanguage;
import yona.ast.local.ReadLocalVariableNode;
import yona.ast.local.ReadLocalVariableNodeGen;
import yona.runtime.Context;
import yona.runtime.Unit;
import yona.runtime.YonaModule;

import java.util.Arrays;
import java.util.Objects;

@NodeInfo
public final class FQNNode extends LiteralValueNode {
  public final String[] packageParts;
  public final String moduleName;

  public FQNNode(String[] packageParts, String moduleName) {
    this.packageParts = packageParts;
    this.moduleName = moduleName;
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
      return lookupContextReference(YonaLanguage.class).get().lookupModule(packageParts, moduleName, this);
    }
  }

  @Override
  public String executeString(VirtualFrame frame) {
    return lookupContextReference(YonaLanguage.class).get().getFQN(packageParts, moduleName);
  }

  @Override
  public YonaModule executeModule(VirtualFrame frame) throws UnexpectedResultException {
    Context context = lookupContextReference(YonaLanguage.class).get();
    String fqn = Context.getFQN(packageParts, moduleName);
    Object globalValue = context.globals.lookup(fqn);
    if (!Unit.INSTANCE.equals(globalValue)) {
      this.replace(new AnyValueNode(globalValue));
      return (YonaModule) globalValue;
    }

    CompilerDirectives.transferToInterpreterAndInvalidate();
    FrameSlot frameSlot = frame.getFrameDescriptor().findFrameSlot(fqn);
    if (frameSlot != null) {
      ReadLocalVariableNode node = ReadLocalVariableNodeGen.create(frameSlot);
      this.replace(node);
      Object result = node.executeGeneric(frame);
      if (!(result instanceof YonaModule)) {
        throw new YonaException("Unexpected error while loading a module " + moduleName, this);
      } else
        return (YonaModule) result;
    }

    return context.lookupModule(packageParts, moduleName, this);
  }
}
