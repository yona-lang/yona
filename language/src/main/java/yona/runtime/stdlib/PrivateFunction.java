package yona.runtime.stdlib;

import com.oracle.truffle.api.dsl.NodeFactory;
import yona.ast.builtin.BuiltinNode;

public final class PrivateFunction extends StdLibFunction {
  public PrivateFunction(NodeFactory<? extends BuiltinNode> node) {
    super(node);
  }

  @Override
  public boolean isExported() {
    return false;
  }
}
