package yona.ast.builtin;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.dsl.CachedContext;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.YonaLanguage;
import yona.runtime.Context;
import yona.runtime.Tuple;
import yona.runtime.async.Promise;
import yona.runtime.exceptions.BadArgException;
import yona.runtime.exceptions.TimeoutException;
import yona.runtime.stdlib.util.TimeUnitUtil;

@NodeInfo(shortName = "timeout")
public abstract class TimeoutBuiltin extends BuiltinNode {
  @Specialization
  @CompilerDirectives.TruffleBoundary
  public Object timeout(Tuple timeUnit, Promise promise, @CachedContext(YonaLanguage.class) Context context) {
    Object millisObj = TimeUnitUtil.getMilliseconds(timeUnit, this);

    if (millisObj instanceof Long) {
      return timeoutForMillis((long) millisObj, promise, context);
    } else { // Promise
      return ((Promise) millisObj).map((millis) -> timeoutForMillis((long) millis, promise, context), this);
    }
  }

  @Specialization
  public Object timeout(Tuple timeUnit, Object value) {
    TimeUnitUtil.getMilliseconds(timeUnit, this);  // just arg check
    return value;
  }

  @Specialization
  public Object timeout(Promise timeUnit, Promise value, @CachedContext(YonaLanguage.class) Context context) {
    return timeUnit.map((maybeTimeUnit) -> {
      if (maybeTimeUnit instanceof Tuple) {
        Object millisObj = TimeUnitUtil.getMilliseconds((Tuple) maybeTimeUnit, this);
        if (millisObj instanceof Long) {
          return timeoutForMillis((long) millisObj, value, context);
        } else { // Promise
          return ((Promise) millisObj).map((millis) -> timeoutForMillis((long) millis, value, context), this);
        }
      } else {
        throw new BadArgException("first argument of timeout must of a type tuple, for example (:seconds, 10)", this);
      }
    }, this);
  }

  @Specialization
  public Object timeout(Promise timeUnit, Object value) {
    return timeUnit.map((maybeTimeUnit) -> {
      if (maybeTimeUnit instanceof Tuple) {
        Object millisObj = TimeUnitUtil.getMilliseconds((Tuple) maybeTimeUnit, this);
        if (millisObj instanceof Long) {
          return value;
        } else { // Promise
          return ((Promise) millisObj).map((millis) -> value, this);
        }
      } else {
        throw new BadArgException("first argument of timeout must of a type tuple, for example (:seconds, 10)", this);
      }
    }, this);
  }

  private Object timeoutForMillis(long millis, Promise promise, Context context) {
    if (millis < 0) {
      throw new BadArgException("millis value should be >= 0", this);
    }

    final Promise result = new Promise();

    context.ioExecutor.submit(() -> {
      try {
        if (Promise.timeout(promise, millis)) {
          result.fulfil(promise.unwrapWithError(), this);
        } else {
          result.fulfil(new TimeoutException(this), this);
        }
      } catch (InterruptedException interrupt) {
        result.fulfil(new yona.runtime.exceptions.InterruptedException(interrupt, this), this);
      }
    });

    return result;
  }
}
