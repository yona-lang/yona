package yatta.ast.builtin;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.dsl.CachedContext;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.interop.ArityException;
import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.interop.UnsupportedMessageException;
import com.oracle.truffle.api.interop.UnsupportedTypeException;
import com.oracle.truffle.api.library.CachedLibrary;
import com.oracle.truffle.api.nodes.NodeInfo;
import yatta.YattaLanguage;
import yatta.runtime.Context;
import yatta.runtime.Function;
import yatta.runtime.async.Promise;
import yatta.runtime.exceptions.BadArgException;

@NodeInfo(shortName = "async")
public abstract class AsyncBuiltin extends BuiltinNode {
  @Specialization
  public Promise async(Function function, @CachedContext(YattaLanguage.class) Context context, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
    if (function.getCardinality() > 0) {
      CompilerDirectives.transferToInterpreterAndInvalidate();
      throw new BadArgException("async function accepts only functions with zero arguments. Function " + function + " expects " + function.getCardinality() + "arguments", this);
    }
    Promise promise = new Promise();
    context.threading.submit(promise, function, dispatch, this);
    return promise;
  }
}
