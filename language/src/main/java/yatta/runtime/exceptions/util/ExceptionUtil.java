package yatta.runtime.exceptions.util;

import com.oracle.truffle.api.CompilerDirectives;
import yatta.YattaException;
import yatta.runtime.Context;
import yatta.runtime.Tuple;

public final class ExceptionUtil {
  @CompilerDirectives.TruffleBoundary
  public static final Tuple throwableToTuple(final Throwable throwable, final Context context) {
    if (throwable instanceof YattaException) {
      YattaException yattaException = (YattaException) throwable;
      return yattaException.asTuple();
    } else {
      // TODO deal with non Yatta exceptions ?
      return new Tuple(context.symbol(throwable.getClass().getSimpleName()), throwable.getMessage(), YattaException.stacktraceToSequence(throwable));
    }
  }
}
