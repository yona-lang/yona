package yatta.ast.builtin.modules;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;
import yatta.ast.builtin.BuiltinNode;
import yatta.runtime.exceptions.BadArgException;
import yatta.runtime.stdlib.Builtins;
import yatta.runtime.stdlib.ExportedFunction;

@BuiltinModuleInfo(packageParts = "java", moduleName = "Types")
public final class JavaTypesBuiltinModule implements BuiltinModule {
  @NodeInfo(shortName = "to_int")
  abstract static class ToIntBuiltin extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    public int toInt(long value) {
      if (value >= Integer.MIN_VALUE && value <= Integer.MAX_VALUE) {
        return (int) value;
      } else {
        throw new BadArgException("value does not fit in Java Integer range: " + value, this);
      }
    }
  }

  @NodeInfo(shortName = "to_float")
  abstract static class ToFloatBuiltin extends BuiltinNode {
    @Specialization
    @CompilerDirectives.TruffleBoundary
    public float toInt(double value) {
      if (value >= Double.MIN_VALUE && value <= Double.MAX_VALUE) {
        return (float) value;
      } else {
        throw new BadArgException("value does not fit in Java Float range: " + value, this);
      }
    }
  }

  @Override
  public Builtins builtins() {
    Builtins builtins = new Builtins();
    builtins.register(new ExportedFunction(JavaTypesBuiltinModuleFactory.ToIntBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(JavaTypesBuiltinModuleFactory.ToFloatBuiltinFactory.getInstance()));
    return builtins;
  }
}
