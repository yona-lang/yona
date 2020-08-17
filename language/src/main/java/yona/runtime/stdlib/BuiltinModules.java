package yona.runtime.stdlib;

import yona.ast.builtin.modules.BuiltinModule;
import yona.ast.builtin.modules.BuiltinModuleInfo;
import yona.runtime.Context;

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
