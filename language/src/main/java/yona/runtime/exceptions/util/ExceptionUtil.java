package yona.runtime.exceptions.util;

import com.oracle.truffle.api.CompilerDirectives;
import yona.YonaException;
import yona.runtime.Context;
import yona.runtime.Seq;
import yona.runtime.Tuple;

public final class ExceptionUtil {
  @CompilerDirectives.TruffleBoundary
  public static Tuple throwableToTuple(final Throwable throwable, final Context context) {
    if (throwable instanceof YonaException yonaException) {
      return yonaException.asTuple();
    } else {
      // TODO deal with non Yona exceptions ?
      return new Tuple(context.symbol(throwable.getClass().getSimpleName()), throwable.getMessage() != null ? Seq.fromCharSequence(throwable.getMessage()) : Seq.EMPTY, YonaException.stacktraceToSequence(throwable));
    }
  }
}
