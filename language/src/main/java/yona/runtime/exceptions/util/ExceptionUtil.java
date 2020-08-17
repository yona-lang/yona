package yona.runtime.exceptions.util;

import com.oracle.truffle.api.CompilerDirectives;
import yona.YonaException;
import yona.runtime.Context;
import yona.runtime.Seq;
import yona.runtime.Tuple;

public final class ExceptionUtil {
  @CompilerDirectives.TruffleBoundary
  public static final Tuple throwableToTuple(final Throwable throwable, final Context context) {
    if (throwable instanceof YonaException) {
      YonaException yonaException = (YonaException) throwable;
      return yonaException.asTuple();
    } else {
      // TODO deal with non Yona exceptions ?
      return new Tuple(context.symbol(throwable.getClass().getSimpleName()), Seq.fromCharSequence(throwable.getMessage()), YonaException.stacktraceToSequence(throwable));
    }
  }
}
