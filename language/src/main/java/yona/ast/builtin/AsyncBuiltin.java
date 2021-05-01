package yona.ast.builtin;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.dsl.CachedContext;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.library.CachedLibrary;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.YonaLanguage;
import yona.runtime.Context;
import yona.runtime.Function;
import yona.runtime.async.Promise;
import yona.runtime.exceptions.BadArgException;
import yona.runtime.threading.ExecutableFunction;

@NodeInfo(shortName = "async")
public abstract class AsyncBuiltin extends BuiltinNode {
  @Specialization
  public Promise async(Function function, @CachedContext(YonaLanguage.class) Context context, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
    if (function.getCardinality() > 0) {
      CompilerDirectives.transferToInterpreterAndInvalidate();
      throw new BadArgException("async function accepts only functions with zero arguments. Function " + function + " expects " + function.getCardinality() + "arguments", this);
    }
    return context.threading.submit(new Promise(dispatch), new ExecutableFunction.YonaExecutableFunction(function, dispatch, this));
  }
}
