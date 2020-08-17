package yona.ast.builtin.modules;

import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.ast.builtin.BuiltinNode;
import yona.runtime.Seq;
import yona.runtime.Tuple;
import yona.runtime.stdlib.Builtins;
import yona.runtime.stdlib.ExportedFunction;

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
