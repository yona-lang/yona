package yatta.ast.builtin.modules;

import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;
import yatta.ast.builtin.BuiltinNode;
import yatta.runtime.Seq;
import yatta.runtime.Tuple;
import yatta.runtime.stdlib.Builtins;
import yatta.runtime.stdlib.ExportedFunction;

@BuiltinModuleInfo(moduleName = "Tuple")
public final class TupleBuiltinModule implements BuiltinModule {
  @NodeInfo(shortName = "to_seq")
  abstract static class ToSeqBuiltin extends BuiltinNode {
    @Specialization
    public Seq toSeq(Tuple tuple) {
      return Seq.sequence((Object[]) tuple.toArray());
    }
  }

  public Builtins builtins() {
    Builtins builtins = new Builtins();
    builtins.register(new ExportedFunction(TupleBuiltinModuleFactory.ToSeqBuiltinFactory.getInstance()));
    return builtins;
  }
}
