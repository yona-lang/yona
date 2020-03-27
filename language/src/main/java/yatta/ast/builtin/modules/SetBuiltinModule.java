package yatta.ast.builtin.modules;

import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.interop.ArityException;
import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.interop.UnsupportedMessageException;
import com.oracle.truffle.api.interop.UnsupportedTypeException;
import com.oracle.truffle.api.library.CachedLibrary;
import com.oracle.truffle.api.nodes.NodeInfo;
import yatta.YattaException;
import yatta.ast.builtin.BuiltinNode;
import yatta.runtime.*;
import yatta.runtime.exceptions.UndefinedNameException;
import yatta.runtime.stdlib.Builtins;
import yatta.runtime.stdlib.ExportedFunction;

@BuiltinModuleInfo(moduleName = "Set")
public final class SetBuiltinModule implements BuiltinModule {
  private static final Set DEFAULT_EMPTY = Set.empty();

  @NodeInfo(shortName = "fold")
  abstract static class FoldBuiltin extends BuiltinNode {
    @Specialization
    public Object fold(Set set, Function function, Object initialValue, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
      try {
        return set.fold(initialValue, function, dispatch);
      } catch (ArityException | UnsupportedTypeException | UnsupportedMessageException e) {
        /* Execute was not successful. */
        throw UndefinedNameException.undefinedFunction(this, function);
      }
    }
  }

  @NodeInfo(shortName = "reduce")
  abstract static class ReduceBuiltin extends BuiltinNode {
    @Specialization
    public Object reduce(Set set, Tuple reducer, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
      try {
        return set.reduce(new Object[] {reducer.get(0), reducer.get(1), reducer.get(2)}, dispatch);
      } catch (ArityException | UnsupportedTypeException | UnsupportedMessageException e) {
        /* Execute was not successful. */
        throw new YattaException(e, this);
      }
    }
  }

  @NodeInfo(shortName = "empty")
  abstract static class EmptyBuiltin extends BuiltinNode {
    @Specialization
    public Object empty() {
      return DEFAULT_EMPTY;
    }
  }

  @NodeInfo(shortName = "len")
  abstract static class LengthBuiltin extends BuiltinNode {
    @Specialization
    public long length(Set set) {
      return set.size();
    }
  }

  public Builtins builtins() {
    Builtins builtins = new Builtins();
    builtins.register(new ExportedFunction(SetBuiltinModuleFactory.FoldBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(SetBuiltinModuleFactory.ReduceBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(SetBuiltinModuleFactory.EmptyBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(SetBuiltinModuleFactory.LengthBuiltinFactory.getInstance()));
    return builtins;
  }
}
