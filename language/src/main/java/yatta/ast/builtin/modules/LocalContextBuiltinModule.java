package yatta.ast.builtin.modules;

import com.oracle.truffle.api.dsl.CachedContext;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;
import yatta.YattaLanguage;
import yatta.ast.builtin.BuiltinNode;
import yatta.runtime.Context;
import yatta.runtime.Seq;
import yatta.runtime.stdlib.Builtins;
import yatta.runtime.stdlib.ExportedFunction;

@BuiltinModuleInfo(packageParts = {"context"}, moduleName = "Local")
public final class LocalContextBuiltinModule implements BuiltinModule {
  @NodeInfo(shortName = "lookup")
  abstract static class LookupBuiltin extends BuiltinNode {
    @Specialization
    public Object fold(Seq key, @CachedContext(YattaLanguage.class) Context context) {
      return context.lookupLocalContext(key.asJavaString(this));
    }
  }

  @NodeInfo(shortName = "contains")
  abstract static class ContainsBuiltin extends BuiltinNode {
    @Specialization
    public boolean fold(Seq key, @CachedContext(YattaLanguage.class) Context context) {
      return context.containsLocalContext(key.asJavaString(this));
    }
  }

  public Builtins builtins() {
    Builtins builtins = new Builtins();
    builtins.register(new ExportedFunction(LocalContextBuiltinModuleFactory.LookupBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(LocalContextBuiltinModuleFactory.ContainsBuiltinFactory.getInstance()));
    return builtins;
  }
}
