package yatta.runtime;

import yatta.ast.builtin.BuiltinNode;
import yatta.ast.builtin.modules.BuiltinModule;
import yatta.ast.builtin.modules.BuiltinModuleInfo;
import yatta.ast.expression.value.FQNNode;
import com.oracle.truffle.api.dsl.NodeFactory;

import java.util.HashMap;

public class BuiltinModules {
  private static final Builtins EMPTY_BUILTINS = new Builtins();
  private final HashMap<FQNNode, Builtins> builtins = new HashMap<>();

  public void register(BuiltinModule builtinModule) {
    BuiltinModuleInfo info = Context.lookupBuiltinModuleInfo(builtinModule.getClass());
    this.builtins.put(new FQNNode(info.packageParts(), info.moduleName()), builtinModule.builtins());
  }

  public NodeFactory<? extends BuiltinNode> lookup(FQNNode fqnNode, String functionName) {
    return builtins.getOrDefault(fqnNode, EMPTY_BUILTINS).lookup(functionName);
  }
}
