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

@BuiltinModuleInfo(moduleName = "Dict")
public final class DictBuiltinModule implements BuiltinModule {
  @NodeInfo(shortName = "fold")
  abstract static class FoldBuiltin extends BuiltinNode {
    @Specialization
    public Object fold(Dict dict, Function function, Object initialValue, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
      try {
        return dict.fold(initialValue, function, dispatch);
      } catch (ArityException | UnsupportedTypeException | UnsupportedMessageException e) {
        /* Execute was not successful. */
        throw UndefinedNameException.undefinedFunction(this, function);
      }
    }
  }

  @NodeInfo(shortName = "reduce")
  abstract static class ReduceBuiltin extends BuiltinNode {
    @Specialization
    public Object reduce(Dict dict, Tuple reducer, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
      try {
        return dict.reduce(new Function[] {(Function) reducer.get(0), (Function) reducer.get(1), (Function) reducer.get(2)}, dispatch);
      } catch (ArityException | UnsupportedTypeException | UnsupportedMessageException e) {
        /* Execute was not successful. */
        throw new YattaException(e, this);
      }
    }
  }

  public Builtins builtins() {
    Builtins builtins = new Builtins();
    builtins.register(DictBuiltinModuleFactory.FoldBuiltinFactory.getInstance());
    builtins.register(DictBuiltinModuleFactory.ReduceBuiltinFactory.getInstance());
    return builtins;
  }
}
