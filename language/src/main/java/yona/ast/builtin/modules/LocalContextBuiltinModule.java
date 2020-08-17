package yona.ast.builtin.modules;

import com.oracle.truffle.api.dsl.CachedContext;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.YonaLanguage;
import yona.ast.builtin.BuiltinNode;
import yona.runtime.Context;
import yona.runtime.Seq;
import yona.runtime.stdlib.Builtins;
import yona.runtime.stdlib.ExportedFunction;

@BuiltinModuleInfo(packageParts = {"context"}, moduleName = "Local")
public final class LocalContextBuiltinModule implements BuiltinModule {
  @NodeInfo(shortName = "lookup")
  abstract static class LookupBuiltin extends BuiltinNode {
    @Specialization
    public Object fold(Seq key, @CachedContext(YonaLanguage.class) Context context) {
      return context.lookupLocalContext(key.asJavaString(this));
    }
  }

  @NodeInfo(shortName = "contains")
  abstract static class ContainsBuiltin extends BuiltinNode {
    @Specialization
    public boolean fold(Seq key, @CachedContext(YonaLanguage.class) Context context) {
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
