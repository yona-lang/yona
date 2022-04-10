package yona.ast.builtin;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.library.CachedLibrary;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.runtime.*;
import yona.runtime.async.Promise;
import yona.runtime.exceptions.BadArgException;
import yona.runtime.threading.ExecutableFunction;

@NodeInfo(shortName = "async")
public abstract class AsyncBuiltin extends BuiltinNode {
  @Specialization
  public Promise async(Function function, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
    if (function.getCardinality() > 0) {
      CompilerDirectives.transferToInterpreterAndInvalidate();
      throw new BadArgException("async function accepts only functions with zero arguments. Function %s expects %d arguments".formatted(function, function.getCardinality()), this);
    }
    return Context.get(this).threading.submit(new Promise(dispatch), new ExecutableFunction.YonaExecutableFunction(function, dispatch, this));
  }

  @Specialization
  public byte async(byte value) {
    return value;
  }

  @Specialization
  public long async(long value) {
    return value;
  }

  @Specialization
  public double async(double value) {
    return value;
  }

  @Specialization
  public int async(int value) {
    return value;
  }

  @Specialization
  public boolean async(boolean value) {
    return value;
  }

  @Specialization
  public Symbol async(Symbol value) {
    return value;
  }

  @Specialization
  public Unit async(Unit value) {
    return value;
  }

  @Specialization
  public String async(String value) {
    return value;
  }

  @Specialization
  public StringList async(StringList value) {
    return value;
  }

  @Specialization
  public YonaModule async(YonaModule value) {
    return value;
  }

  @Specialization
  public Seq async(Seq value) {
    return value;
  }
}
