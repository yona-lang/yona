package yona.ast.builtin.modules;

import com.oracle.truffle.api.dsl.Fallback;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.ast.builtin.BuiltinNode;
import yona.runtime.*;
import yona.runtime.stdlib.Builtins;
import yona.runtime.stdlib.ExportedFunction;

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
    public Object match(YonaModule val) {
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
    return new Builtins(
        new ExportedFunction(TypesBuiltinModuleFactory.IsBooleanBuiltinFactory.getInstance()),
        new ExportedFunction(TypesBuiltinModuleFactory.IsByteBuiltinFactory.getInstance()),
        new ExportedFunction(TypesBuiltinModuleFactory.IsIntegerBuiltinFactory.getInstance()),
        new ExportedFunction(TypesBuiltinModuleFactory.IsFloatBuiltinFactory.getInstance()),
        new ExportedFunction(TypesBuiltinModuleFactory.IsCharBuiltinFactory.getInstance()),
        new ExportedFunction(TypesBuiltinModuleFactory.IsFunctionBuiltinFactory.getInstance()),
        new ExportedFunction(TypesBuiltinModuleFactory.IsTupleBuiltinFactory.getInstance()),
        new ExportedFunction(TypesBuiltinModuleFactory.IsModuleBuiltinFactory.getInstance()),
        new ExportedFunction(TypesBuiltinModuleFactory.IsSeqBuiltinFactory.getInstance()),
        new ExportedFunction(TypesBuiltinModuleFactory.IsSetBuiltinFactory.getInstance()),
        new ExportedFunction(TypesBuiltinModuleFactory.IsDictBuiltinFactory.getInstance()),
        new ExportedFunction(TypesBuiltinModuleFactory.IsSetBuiltinFactory.getInstance()),
        new ExportedFunction(TypesBuiltinModuleFactory.IsNativeBuiltinFactory.getInstance()),
        new ExportedFunction(TypesBuiltinModuleFactory.IsSymbolBuiltinFactory.getInstance())
    );
  }
}
