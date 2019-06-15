package abzu.runtime;

import abzu.ast.builtin.BuiltinNode;
import abzu.ast.builtin.modules.BuiltinModule;
import abzu.ast.builtin.modules.BuiltinModuleInfo;
import abzu.ast.expression.value.FQNNode;
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
