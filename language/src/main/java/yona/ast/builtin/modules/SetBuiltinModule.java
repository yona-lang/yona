package yona.ast.builtin.modules;

import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.library.CachedLibrary;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.ast.builtin.BuiltinNode;
import yona.runtime.Function;
import yona.runtime.Seq;
import yona.runtime.Set;
import yona.runtime.Tuple;
import yona.runtime.stdlib.Builtins;
import yona.runtime.stdlib.ExportedFunction;

@BuiltinModuleInfo(moduleName = "Set")
public final class SetBuiltinModule implements BuiltinModule {
  private static final Set DEFAULT_EMPTY = Set.empty();

  @NodeInfo(shortName = "fold")
  abstract static class FoldBuiltin extends BuiltinNode {
    @Specialization
    public Object fold(Function function, Object initialValue, Set set, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
      return set.fold(initialValue, function, dispatch, this);
    }
  }

  @NodeInfo(shortName = "reduce")
  abstract static class ReduceBuiltin extends BuiltinNode {
    @Specialization
    public Object reduce(Tuple reducer, Set set, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
      return set.reduce(new Object[]{reducer.get(0), reducer.get(1), reducer.get(2)}, dispatch, this);
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

  @NodeInfo(shortName = "to_seq")
  abstract static class ToSeqBuiltin extends BuiltinNode {
    @Specialization
    public Seq length(Set set) {
      return set.fold(Seq.EMPTY, Seq::insertLast);
    }
  }

  public Builtins builtins() {
    return new Builtins(
        new ExportedFunction(SetBuiltinModuleFactory.FoldBuiltinFactory.getInstance()),
        new ExportedFunction(SetBuiltinModuleFactory.ReduceBuiltinFactory.getInstance()),
        new ExportedFunction(SetBuiltinModuleFactory.EmptyBuiltinFactory.getInstance()),
        new ExportedFunction(SetBuiltinModuleFactory.LengthBuiltinFactory.getInstance()),
        new ExportedFunction(SetBuiltinModuleFactory.ToSeqBuiltinFactory.getInstance())
    );
  }
}
