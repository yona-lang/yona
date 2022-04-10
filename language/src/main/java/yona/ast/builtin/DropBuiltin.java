package yona.ast.builtin;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.library.CachedLibrary;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.runtime.Context;
import yona.runtime.Function;
import yona.runtime.Unit;
import yona.runtime.async.Promise;
import yona.runtime.exceptions.BadArgException;
import yona.runtime.threading.ExecutableFunction;

@NodeInfo(shortName = "drop")
public abstract class DropBuiltin extends BuiltinNode {
  @Specialization
  public Unit drop(Function function, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
    if (function.getCardinality() > 0) {
      CompilerDirectives.transferToInterpreterAndInvalidate();
      throw new BadArgException("async function accepts only functions with zero arguments. Function " + function + " expects " + function.getCardinality() + "arguments", this);
    }
    Context.get(this).threading.submit(new Promise(dispatch), new ExecutableFunction.YonaExecutableFunction(function, dispatch, this));
    return Unit.INSTANCE;
  }
}
