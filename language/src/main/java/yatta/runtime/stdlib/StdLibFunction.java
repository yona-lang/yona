package yatta.runtime.stdlib;

import com.oracle.truffle.api.dsl.NodeFactory;
import yatta.ast.builtin.BuiltinNode;

public abstract class StdLibFunction {
  public final NodeFactory<? extends BuiltinNode> node;

  public StdLibFunction(NodeFactory<? extends BuiltinNode> node) {
    this.node = node;
  }

  public abstract boolean isExported();

  public boolean unwrapArgumentPromises() {
    return true;
  }
}
