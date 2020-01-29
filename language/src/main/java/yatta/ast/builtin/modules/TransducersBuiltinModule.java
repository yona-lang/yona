package yatta.ast.builtin.modules;

import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;
import yatta.ast.builtin.BuiltinNode;
import yatta.runtime.exceptions.TransducerDoneException;
import yatta.runtime.stdlib.Builtins;
import yatta.runtime.stdlib.ExportedFunction;

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
    Builtins builtins = new Builtins();
    builtins.register(new ExportedFunction(TransducersBuiltinModuleFactory.RaiseDoneBuiltinFactory.getInstance()));
    return builtins;
  }
}
