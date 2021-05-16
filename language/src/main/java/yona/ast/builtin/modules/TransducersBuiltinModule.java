package yona.ast.builtin.modules;

import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.ast.builtin.BuiltinNode;
import yona.runtime.exceptions.TransducerDoneException;
import yona.runtime.stdlib.Builtins;
import yona.runtime.stdlib.ExportedFunction;

@BuiltinModuleInfo(moduleName = "Transducers")
public final class TransducersBuiltinModule implements BuiltinModule {
  @NodeInfo(shortName = "done")
  abstract static class RaiseDoneBuiltin extends BuiltinNode {
    @Specialization
    public Object raiseDone() {
      throw TransducerDoneException.INSTANCE;
    }
  }

  public Builtins builtins() {
    return new Builtins(
        new ExportedFunction(TransducersBuiltinModuleFactory.RaiseDoneBuiltinFactory.getInstance())
    );
  }
}
