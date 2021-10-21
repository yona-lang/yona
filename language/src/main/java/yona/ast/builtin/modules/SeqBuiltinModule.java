package yona.ast.builtin.modules;

import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.library.CachedLibrary;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.ast.builtin.BuiltinNode;
import yona.runtime.Function;
import yona.runtime.Seq;
import yona.runtime.Tuple;
import yona.runtime.stdlib.Builtins;
import yona.runtime.stdlib.ExportedFunction;

@BuiltinModuleInfo(moduleName = "Seq")
public final class SeqBuiltinModule implements BuiltinModule {
  @NodeInfo(shortName = "foldl")
  abstract static class FoldLeftBuiltin extends BuiltinNode {
    @Specialization
    public Object foldLeft(Function function, Object initialValue, Seq sequence, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
      return sequence.foldLeft(initialValue, function, dispatch, this);
    }
  }

  @NodeInfo(shortName = "foldr")
  abstract static class FoldRightBuiltin extends BuiltinNode {
    @Specialization
    public Object foldRight(Function function, Object initialValue, Seq sequence, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
      return sequence.foldRight(initialValue, function, dispatch, this);
    }
  }

  @NodeInfo(shortName = "reducel")
  abstract static class ReduceLeftBuiltin extends BuiltinNode {
    @Specialization
    public Object reduceLeft(Tuple reducer, Seq sequence, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
      return sequence.reduceLeft(new Object[]{reducer.get(0), reducer.get(1), reducer.get(2)}, dispatch, this);
    }
  }

  @NodeInfo(shortName = "reducer")
  abstract static class ReduceRightBuiltin extends BuiltinNode {
    @Specialization
    public Object reduceRight(Tuple reducer, Seq sequence, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
      return sequence.reduceRight(new Object[]{reducer.get(0), reducer.get(1), reducer.get(2)}, dispatch, this);
    }
  }

  @NodeInfo(shortName = "len")
  abstract static class LengthBuiltin extends BuiltinNode {
    @Specialization
    public long length(Seq sequence) {
      return sequence.length();
    }
  }

  @NodeInfo(shortName = "split_at")
  abstract static class SplitBuiltin extends BuiltinNode {
    @Specialization
    public Tuple length(long idx, Seq sequence) {
      return new Tuple((Object[]) sequence.split(idx, this));
    }
  }

  @NodeInfo(shortName = "take")
  abstract static class TakeBuiltin extends BuiltinNode {
    @Specialization
    public Seq take(long n, Seq sequence) {
      return sequence.take(n, this);
    }
  }

  @NodeInfo(shortName = "drop")
  abstract static class DropBuiltin extends BuiltinNode {
    @Specialization
    public Seq drop(long n, Seq sequence) {
      return sequence.drop(n, this);
    }
  }

  @NodeInfo(shortName = "is_string")
  abstract static class IsStringBuiltin extends BuiltinNode {
    @Specialization
    public boolean length(Seq sequence) {
      return sequence.isString();
    }
  }

  @NodeInfo(shortName = "lookup")
  abstract static class LookupBuiltin extends BuiltinNode {
    @Specialization
    public Object length(long idx, Seq sequence) {
      return sequence.lookup(idx, this);
    }
  }

  @NodeInfo(shortName = "encode")
  abstract static class EncodeBuiltin extends BuiltinNode {
    @Specialization
    public Object encode(Seq sequence) {
      return sequence.map(el -> (byte) ((int) el));
    }
  }

  @NodeInfo(shortName = "decode")
  abstract static class DecodeBuiltin extends BuiltinNode {
    @Specialization
    public Object decode(Seq sequence) {
      return sequence.map(el -> (int) ((byte) el));
    }
  }

  @NodeInfo(shortName = "trim")
  abstract static class TrimBuiltin extends BuiltinNode {
    @Specialization
    public Object trim(Seq sequence) {
      return Seq.fromCharSequence(sequence.asJavaString(this).trim());
    }
  }

  public Builtins builtins() {
    return new Builtins(
        new ExportedFunction(SeqBuiltinModuleFactory.LengthBuiltinFactory.getInstance()),
        new ExportedFunction(SeqBuiltinModuleFactory.FoldLeftBuiltinFactory.getInstance()),
        new ExportedFunction(SeqBuiltinModuleFactory.FoldRightBuiltinFactory.getInstance()),
        new ExportedFunction(SeqBuiltinModuleFactory.ReduceLeftBuiltinFactory.getInstance()),
        new ExportedFunction(SeqBuiltinModuleFactory.ReduceRightBuiltinFactory.getInstance()),
        new ExportedFunction(SeqBuiltinModuleFactory.SplitBuiltinFactory.getInstance()),
        new ExportedFunction(SeqBuiltinModuleFactory.IsStringBuiltinFactory.getInstance()),
        new ExportedFunction(SeqBuiltinModuleFactory.LookupBuiltinFactory.getInstance()),
        new ExportedFunction(SeqBuiltinModuleFactory.TakeBuiltinFactory.getInstance()),
        new ExportedFunction(SeqBuiltinModuleFactory.DropBuiltinFactory.getInstance()),
        new ExportedFunction(SeqBuiltinModuleFactory.EncodeBuiltinFactory.getInstance()),
        new ExportedFunction(SeqBuiltinModuleFactory.DecodeBuiltinFactory.getInstance()),
        new ExportedFunction(SeqBuiltinModuleFactory.TrimBuiltinFactory.getInstance())
    );
  }
}
