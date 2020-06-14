package yatta.ast.builtin.modules;

import com.oracle.truffle.api.dsl.Fallback;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;
import yatta.ast.builtin.BuiltinNode;
import yatta.runtime.*;
import yatta.runtime.stdlib.Builtins;
import yatta.runtime.stdlib.ExportedFunction;

@BuiltinModuleInfo(moduleName = "Types")
public final class TypesBuiltinModule implements BuiltinModule {
  @NodeInfo(shortName = "is_boolean")
  abstract static class IsBooleanBuiltin extends BuiltinNode {
    @Specialization
    public Object match(boolean val) {
      return true;
    }

    @Fallback
    public Object otherwise(Object val) {
      return false;
    }
  }

  @NodeInfo(shortName = "is_byte")
  abstract static class IsByteBuiltin extends BuiltinNode {
    @Specialization
    public Object match(byte val) {
      return true;
    }

    @Fallback
    public Object otherwise(Object val) {
      return false;
    }
  }

  @NodeInfo(shortName = "is_integer")
  abstract static class IsIntegerBuiltin extends BuiltinNode {
    @Specialization
    public Object match(long val) {
      return true;
    }

    @Fallback
    public Object otherwise(Object val) {
      return false;
    }
  }

  @NodeInfo(shortName = "is_float")
  abstract static class IsFloatBuiltin extends BuiltinNode {
    @Specialization
    public Object match(double val) {
      return true;
    }

    @Fallback
    public Object otherwise(Object val) {
      return false;
    }
  }

  @NodeInfo(shortName = "is_char")
  abstract static class IsCharBuiltin extends BuiltinNode {
    @Specialization
    public Object match(int val) {
      return true;
    }

    @Fallback
    public Object otherwise(Object val) {
      return false;
    }
  }

  @NodeInfo(shortName = "is_function")
  abstract static class IsFunctionBuiltin extends BuiltinNode {
    @Specialization
    public Object match(Function val) {
      return true;
    }

    @Fallback
    public Object otherwise(Object val) {
      return false;
    }
  }

  @NodeInfo(shortName = "is_tuple")
  abstract static class IsTupleBuiltin extends BuiltinNode {
    @Specialization
    public Object match(Tuple val) {
      return true;
    }

    @Fallback
    public Object otherwise(Object val) {
      return false;
    }
  }

  @NodeInfo(shortName = "is_module")
  abstract static class IsModuleBuiltin extends BuiltinNode {
    @Specialization
    public Object match(YattaModule val) {
      return true;
    }

    @Fallback
    public Object otherwise(Object val) {
      return false;
    }
  }

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

  @NodeInfo(shortName = "is_native")
  abstract static class IsNativeBuiltin extends BuiltinNode {
    @Specialization
    public Object match(NativeObject val) {
      return true;
    }

    @Fallback
    public Object otherwise(Object val) {
      return false;
    }
  }

  @NodeInfo(shortName = "is_symbol")
  abstract static class IsSymbolBuiltin extends BuiltinNode {
    @Specialization
    public Object match(Symbol val) {
      return true;
    }

    @Fallback
    public Object otherwise(Object val) {
      return false;
    }
  }

  public Builtins builtins() {
    Builtins builtins = new Builtins();
    builtins.register(new ExportedFunction(TypesBuiltinModuleFactory.IsBooleanBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(TypesBuiltinModuleFactory.IsByteBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(TypesBuiltinModuleFactory.IsIntegerBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(TypesBuiltinModuleFactory.IsFloatBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(TypesBuiltinModuleFactory.IsCharBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(TypesBuiltinModuleFactory.IsFunctionBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(TypesBuiltinModuleFactory.IsTupleBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(TypesBuiltinModuleFactory.IsModuleBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(TypesBuiltinModuleFactory.IsSeqBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(TypesBuiltinModuleFactory.IsSetBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(TypesBuiltinModuleFactory.IsDictBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(TypesBuiltinModuleFactory.IsSetBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(TypesBuiltinModuleFactory.IsNativeBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(TypesBuiltinModuleFactory.IsSymbolBuiltinFactory.getInstance()));
    return builtins;
  }
}
