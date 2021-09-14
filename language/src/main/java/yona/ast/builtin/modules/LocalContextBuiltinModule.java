package yona.ast.builtin.modules;

import com.oracle.truffle.api.Truffle;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.frame.Frame;
import com.oracle.truffle.api.frame.FrameInstance;
import com.oracle.truffle.api.frame.FrameSlot;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.ast.builtin.BuiltinNode;
import yona.runtime.Seq;
import yona.runtime.Unit;
import yona.runtime.stdlib.Builtins;
import yona.runtime.stdlib.ExportedFunction;

@BuiltinModuleInfo(packageParts = {"context"}, moduleName = "Local")
public final class LocalContextBuiltinModule implements BuiltinModule {
  @NodeInfo(shortName = "lookup")
  abstract static class LookupBuiltin extends BuiltinNode {
    @Specialization
    public Object fold(Seq key) {
      FrameInstance frameInstance = Truffle.getRuntime().getCallerFrame();
      Frame callerFrame = frameInstance.getFrame(FrameInstance.FrameAccess.READ_ONLY);
      FrameSlot slot = callerFrame.getFrameDescriptor().findFrameSlot(key.asJavaString(this));
      if (slot != null) {
        return callerFrame.getValue(slot);
      } else {
        return Unit.INSTANCE;
      }
    }
  }

  @NodeInfo(shortName = "contains")
  abstract static class ContainsBuiltin extends BuiltinNode {
    @Specialization
    public boolean fold(Seq key) {
      FrameInstance frameInstance = Truffle.getRuntime().getCallerFrame();
      Frame callerFrame = frameInstance.getFrame(FrameInstance.FrameAccess.READ_ONLY);
      FrameSlot slot = callerFrame.getFrameDescriptor().findFrameSlot(key.asJavaString(this));
      return slot != null;
    }
  }

  public Builtins builtins() {
    return new Builtins(
        new ExportedFunction(LocalContextBuiltinModuleFactory.LookupBuiltinFactory.getInstance()),
        new ExportedFunction(LocalContextBuiltinModuleFactory.ContainsBuiltinFactory.getInstance())
    );
  }
}
