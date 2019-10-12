package yatta.runtime;

import com.oracle.truffle.api.dsl.NodeFactory;
import yatta.ast.builtin.BuiltinNode;
import yatta.ast.builtin.modules.BuiltinModule;
import yatta.ast.builtin.modules.BuiltinModuleInfo;

import java.util.HashMap;

public class BuiltinModules {
  private static final Builtins EMPTY_BUILTINS = new Builtins();
  public final HashMap<String, Builtins> builtins = new HashMap<>();

  public void register(BuiltinModule builtinModule) {
    BuiltinModuleInfo info = Context.lookupBuiltinModuleInfo(builtinModule.getClass());
    this.builtins.put(Context.getFQN(info.packageParts(), info.moduleName()), builtinModule.builtins());
  }

  public NodeFactory<? extends BuiltinNode> lookup(String fqn, String functionName) {
    return builtins.getOrDefault(fqn, EMPTY_BUILTINS).lookup(functionName);
  }
}
