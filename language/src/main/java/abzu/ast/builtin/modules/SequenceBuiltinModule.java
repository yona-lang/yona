package abzu.ast.builtin.modules;

import abzu.ast.builtin.BuiltinNode;
import abzu.runtime.Builtins;
import abzu.runtime.Function;
import abzu.runtime.Sequence;
import abzu.runtime.UndefinedNameException;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.interop.ArityException;
import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.interop.UnsupportedMessageException;
import com.oracle.truffle.api.interop.UnsupportedTypeException;
import com.oracle.truffle.api.library.CachedLibrary;
import com.oracle.truffle.api.nodes.NodeInfo;

@BuiltinModuleInfo(moduleName = "Sequence")
public final class SequenceBuiltinModule implements BuiltinModule {
  @NodeInfo(shortName = "foldl")
  abstract static class FoldLeftBuiltin extends BuiltinNode {
    @Specialization
    public Object foldLeft(Sequence sequence, Function function, Object initialValue, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
      return sequence.foldLeft((acc, val) -> {
        try {
          return dispatch.execute(function, new Object[] {acc, val});
        } catch (ArityException | UnsupportedTypeException | UnsupportedMessageException e) {
          /* Execute was not successful. */
          throw UndefinedNameException.undefinedFunction(this, function);
        }
      }, initialValue);
    }
  }

  @NodeInfo(shortName = "foldr")
  abstract static class FoldRightBuiltin extends BuiltinNode {
    @Specialization
    public Object foldRight(Sequence sequence, Function function, Object initialValue, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
      return sequence.foldRight((acc, val) -> {
        try {
          return dispatch.execute(function, new Object[] {acc, val});
        } catch (ArityException | UnsupportedTypeException | UnsupportedMessageException e) {
          /* Execute was not successful. */
          throw UndefinedNameException.undefinedFunction(this, function);
        }
      }, initialValue);
    }
  }

  public Builtins builtins() {
    Builtins builtins = new Builtins();
    builtins.register(SequenceBuiltinModuleFactory.FoldLeftBuiltinFactory.getInstance());
    builtins.register(SequenceBuiltinModuleFactory.FoldRightBuiltinFactory.getInstance());
    return builtins;
  }
}
