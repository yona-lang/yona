package yatta.ast.builtin.modules;

import com.oracle.truffle.api.dsl.Fallback;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;
import yatta.ast.builtin.BuiltinNode;
import yatta.runtime.Dict;
import yatta.runtime.Seq;
import yatta.runtime.Set;
import yatta.runtime.stdlib.Builtins;
import yatta.runtime.stdlib.ExportedFunction;

@BuiltinModuleInfo(moduleName = "Types")
public final class TypesBuiltinModule implements BuiltinModule {
  @NodeInfo(shortName = "is_seq")
  abstract static class IsSeqBuiltin extends BuiltinNode {
    @Specialization
    public Object match(Seq val) {
      return true;
    }

    @Fallback
    public Object otherwise(Object val) {
      return false;
    }
  }

  @NodeInfo(shortName = "is_set")
  abstract static class IsSetBuiltin extends BuiltinNode {
    @Specialization
    public Object match(Set val) {
      return true;
    }

    @Fallback
    public Object otherwise(Object val) {
      return false;
    }
  }

  @NodeInfo(shortName = "is_dict")
  abstract static class IsDictBuiltin extends BuiltinNode {
    @Specialization
    public Object match(Dict val) {
      return true;
    }

    @Fallback
    public Object otherwise(Object val) {
      return false;
    }
  }

  public Builtins builtins() {
    Builtins builtins = new Builtins();
    builtins.register(new ExportedFunction(TypesBuiltinModuleFactory.IsSeqBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(TypesBuiltinModuleFactory.IsSetBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(TypesBuiltinModuleFactory.IsDictBuiltinFactory.getInstance()));
    return builtins;
  }
}
