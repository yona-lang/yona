package yona.ast.builtin;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.library.CachedLibrary;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.runtime.Context;
import yona.runtime.Tuple;
import yona.runtime.Unit;
import yona.runtime.async.Promise;
import yona.runtime.stdlib.util.TimeUnitUtil;

@NodeInfo(shortName = "sleep")
public abstract class SleepBuiltin extends BuiltinNode {
  @Specialization
  @CompilerDirectives.TruffleBoundary
  public Promise sleep(Tuple timeUnit, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
    Promise promise = new Promise(dispatch);
    Object millisObj = TimeUnitUtil.getMilliseconds(timeUnit, this);
    Context context = Context.get(this);

    if (millisObj instanceof Long) {
      sleep(context, promise, (long) millisObj);
    } else { // Promise
      ((Promise) millisObj).map((millis) -> {
        sleep(context, promise, (long) millisObj);
        return Unit.INSTANCE;
      }, this);
    }

    return promise;
  }

  private void sleep(Context context, Promise promise, long millisObj) {
    context.ioExecutor.submit(() -> {
      try {
        Thread.sleep(millisObj);
        promise.fulfil(Unit.INSTANCE, this);
      } catch (InterruptedException interrupt) {
        promise.fulfil(new yona.runtime.exceptions.InterruptedException(interrupt, this), this);
      }
    });
  }
}
