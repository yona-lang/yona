package yona.ast.builtin.modules;

import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.interop.ArityException;
import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.interop.UnsupportedMessageException;
import com.oracle.truffle.api.interop.UnsupportedTypeException;
import com.oracle.truffle.api.library.CachedLibrary;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.YonaException;
import yona.ast.builtin.BuiltinNode;
import yona.runtime.*;
import yona.runtime.exceptions.UndefinedNameException;
import yona.runtime.stdlib.Builtins;
import yona.runtime.stdlib.ExportedFunction;

@BuiltinModuleInfo(moduleName = "Dict")
public final class DictBuiltinModule implements BuiltinModule {
  @NodeInfo(shortName = "fold")
  abstract static class FoldBuiltin extends BuiltinNode {
    @Specialization
    public Object fold(Function function, Object initialValue, Dict dict, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
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
    public Object reduce(Tuple reducer, Dict dict, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
      try {
        return dict.reduce(new Object[]{reducer.get(0), reducer.get(1), reducer.get(2)}, dispatch);
      } catch (ArityException | UnsupportedTypeException | UnsupportedMessageException e) {
        /* Execute was not successful. */
        throw new YonaException(e, this);
      }
    }
  }

  @NodeInfo(shortName = "len")
  abstract static class LengthBuiltin extends BuiltinNode {
    @Specialization
    public long length(Dict dict) {
      return dict.size();
    }
  }

  @NodeInfo(shortName = "lookup")
  abstract static class LookupBuiltin extends BuiltinNode {
    @Specialization
    public Object lookup(Object key, Dict dict) {
      return dict.lookup(key);
    }
  }

  @NodeInfo(shortName = "entries")
  abstract static class EntriesBuiltin extends BuiltinNode {
    @Specialization
    public Seq reduce(Dict dict) {
      return dict.fold(Seq.EMPTY, (acc, key, val) -> acc.insertLast(new Tuple(key, val)));
    }
  }

  @NodeInfo(shortName = "keys")
  abstract static class KeysBuiltin extends BuiltinNode {
    @Specialization
    public Set reduce(Dict dict) {
      return dict.keys();
    }
  }

  public Builtins builtins() {
    return new Builtins(
        new ExportedFunction(DictBuiltinModuleFactory.FoldBuiltinFactory.getInstance()),
        new ExportedFunction(DictBuiltinModuleFactory.ReduceBuiltinFactory.getInstance()),
        new ExportedFunction(DictBuiltinModuleFactory.LengthBuiltinFactory.getInstance()),
        new ExportedFunction(DictBuiltinModuleFactory.LookupBuiltinFactory.getInstance()),
        new ExportedFunction(DictBuiltinModuleFactory.EntriesBuiltinFactory.getInstance()),
        new ExportedFunction(DictBuiltinModuleFactory.KeysBuiltinFactory.getInstance())
    );
  }
}
