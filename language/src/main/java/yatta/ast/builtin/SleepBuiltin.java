package yatta.ast.builtin;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.dsl.CachedContext;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.library.CachedLibrary;
import com.oracle.truffle.api.nodes.NodeInfo;
import yatta.YattaLanguage;
import yatta.runtime.Context;
import yatta.runtime.Tuple;
import yatta.runtime.Unit;
import yatta.runtime.async.Promise;
import yatta.runtime.stdlib.util.TimeUnitUtil;

@NodeInfo(shortName = "sleep")
public abstract class SleepBuiltin extends BuiltinNode {
  @Specialization
  @CompilerDirectives.TruffleBoundary
  public Promise sleep(Tuple timeUnit, @CachedContext(YattaLanguage.class) Context context, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
    Promise promise = new Promise(dispatch);
    Object millisObj = TimeUnitUtil.getMilliseconds(timeUnit, this);

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
      } catch (InterruptedException interrupt) {
        promise.fulfil(interrupt, this);
      }
      promise.fulfil(Unit.INSTANCE, this);
    });
  }
}
