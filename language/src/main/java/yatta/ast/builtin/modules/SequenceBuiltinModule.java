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

  @NodeInfo(shortName = "reducel")
  abstract static class ReduceLeftBuiltin extends BuiltinNode {
    @Specialization
    public Object reduceLeft(Seq sequence, Tuple transducer, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
      try {
        return sequence.reduceLeft(new Function[] {(Function) transducer.get(0), (Function) transducer.get(1), (Function) transducer.get(2)}, dispatch);
      } catch (ArityException | UnsupportedTypeException | UnsupportedMessageException e) {
        /* Execute was not successful. */
        e.printStackTrace();
        throw new YattaException(e, this);
      }
    }
  }

  @NodeInfo(shortName = "reducer")
  abstract static class ReduceRightBuiltin extends BuiltinNode {
    @Specialization
    public Object reduceRight(Seq sequence, Tuple transducer, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
      try {
        return sequence.reduceRight(new Function[] {(Function) transducer.get(0), (Function) transducer.get(1), (Function) transducer.get(2)}, dispatch);
      } catch (ArityException | UnsupportedTypeException | UnsupportedMessageException e) {
        /* Execute was not successful. */
        throw new YattaException(e, this);
      }
    }
  }

  public Builtins builtins() {
    Builtins builtins = new Builtins();
    builtins.register(SequenceBuiltinModuleFactory.FoldLeftBuiltinFactory.getInstance());
    builtins.register(SequenceBuiltinModuleFactory.FoldRightBuiltinFactory.getInstance());
    builtins.register(SequenceBuiltinModuleFactory.ReduceLeftBuiltinFactory.getInstance());
    builtins.register(SequenceBuiltinModuleFactory.ReduceRightBuiltinFactory.getInstance());
    return builtins;
  }
}
