package yatta.ast.builtin.modules;

import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.interop.ArityException;
import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.interop.UnsupportedMessageException;
import com.oracle.truffle.api.interop.UnsupportedTypeException;
import com.oracle.truffle.api.library.CachedLibrary;
import com.oracle.truffle.api.nodes.NodeInfo;
import yatta.ast.builtin.BuiltinNode;
import yatta.runtime.Builtins;
import yatta.runtime.Function;
import yatta.runtime.Seq;
import yatta.runtime.UndefinedNameException;

@BuiltinModuleInfo(moduleName = "Sequence")
public final class SequenceBuiltinModule implements BuiltinModule {
  @NodeInfo(shortName = "foldl")
  abstract static class FoldLeftBuiltin extends BuiltinNode {
    @Specialization
    public Object foldLeft(Seq sequence, Function function, Object initialValue, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
      try {
        return sequence.foldLeft(initialValue, function, dispatch);
      } catch (ArityException | UnsupportedTypeException | UnsupportedMessageException e) {
        /* Execute was not successful. */
        throw UndefinedNameException.undefinedFunction(this, function);
      }
    }
  }

  @NodeInfo(shortName = "foldr")
  abstract static class FoldRightBuiltin extends BuiltinNode {
    @Specialization
    public Object foldRight(Seq sequence, Function function, Object initialValue, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
      try {
        return sequence.foldRight(initialValue, function, dispatch);
      } catch (ArityException | UnsupportedTypeException | UnsupportedMessageException e) {
        /* Execute was not successful. */
        throw UndefinedNameException.undefinedFunction(this, function);
      }
    }
  }

  public Builtins builtins() {
    Builtins builtins = new Builtins();
    builtins.register(SequenceBuiltinModuleFactory.FoldLeftBuiltinFactory.getInstance());
    builtins.register(SequenceBuiltinModuleFactory.FoldRightBuiltinFactory.getInstance());
    return builtins;
  }
}
