package yatta.ast.builtin.modules;

import com.oracle.truffle.api.dsl.Fallback;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;
import yatta.ast.builtin.BuiltinNode;
import yatta.runtime.stdlib.Builtins;
import yatta.runtime.stdlib.ExportedFunction;

@BuiltinModuleInfo(moduleName = "Time")
public final class TimeBuiltinModule implements BuiltinModule {
  @NodeInfo(shortName = "now")
  abstract static class NowBuiltin extends BuiltinNode {
    @Specialization
    public long now() {
      return System.currentTimeMillis();
    }
  }

  public Builtins builtins() {
    Builtins builtins = new Builtins();
    builtins.register(new ExportedFunction(TimeBuiltinModuleFactory.NowBuiltinFactory.getInstance()));
    return builtins;
  }
}
