package yatta.runtime;

import yatta.ast.builtin.BuiltinNode;
import com.oracle.truffle.api.dsl.NodeFactory;

import java.util.HashMap;
import java.util.Map;

public class Builtins {
  private final Map<Object, NodeFactory<? extends BuiltinNode>> builtins = new HashMap<>();

  public void register(NodeFactory<? extends BuiltinNode> node) {
    this.builtins.put(Context.lookupNodeInfo(node.getNodeClass()).shortName(), node);
  }

  public NodeFactory<? extends BuiltinNode> lookup(Object name) {
    return builtins.get(name);
  }
}
