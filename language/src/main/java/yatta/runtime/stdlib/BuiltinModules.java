package yatta.runtime.stdlib;

import yatta.ast.builtin.modules.BuiltinModule;
import yatta.ast.builtin.modules.BuiltinModuleInfo;
import yatta.runtime.Context;

import java.util.HashMap;

public class BuiltinModules {
  private static final Builtins EMPTY_BUILTINS = new Builtins();
  public final HashMap<String, Builtins> builtins = new HashMap<>();

  public void register(BuiltinModule builtinModule) {
    BuiltinModuleInfo info = Context.lookupBuiltinModuleInfo(builtinModule.getClass());
    this.builtins.put(Context.getFQN(info.packageParts(), info.moduleName()), builtinModule.builtins());
  }

  public StdLibFunction lookup(String fqn, String functionName) {
    return builtins.getOrDefault(fqn, EMPTY_BUILTINS).lookup(functionName);
  }
}
