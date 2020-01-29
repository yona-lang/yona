package yatta.runtime.stdlib;

import com.oracle.truffle.api.dsl.NodeFactory;
import yatta.ast.builtin.BuiltinNode;

public final class PrivateFunction extends StdLibFunction {
  public PrivateFunction(NodeFactory<? extends BuiltinNode> node) {
    super(node);
  }

  @Override
  public boolean isExported() {
    return false;
  }
}
