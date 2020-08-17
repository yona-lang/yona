package yona.runtime.stdlib;

import com.oracle.truffle.api.dsl.NodeFactory;
import yona.ast.builtin.BuiltinNode;

public class ExportedFunction extends StdLibFunction {
  public ExportedFunction(NodeFactory<? extends BuiltinNode> node) {
    super(node);
  }

  @Override
  public boolean isExported() {
    return true;
  }
}
