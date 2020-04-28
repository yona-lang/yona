package yatta.runtime.stdlib;

import com.oracle.truffle.api.dsl.NodeFactory;
import yatta.ast.builtin.BuiltinNode;

public class ExportedFunction extends StdLibFunction {
  public ExportedFunction(NodeFactory<? extends BuiltinNode> node) {
    super(node);
  }

  @Override
  public boolean isExported() {
    return true;
  }
}
