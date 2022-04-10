package yona.runtime.exceptions.util;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.nodes.Node;
import yona.YonaException;
import yona.runtime.Context;
import yona.runtime.Seq;
import yona.runtime.Tuple;

public final class ExceptionUtil {
  @CompilerDirectives.TruffleBoundary
  public static Tuple throwableToTuple(final Throwable throwable, final Node node) {
    if (throwable instanceof YonaException yonaException) {
      return yonaException.asTuple();
    } else {
      // TODO deal with non-Yona exceptions ?
      return Tuple.allocate(node, Context.get(node).symbol(throwable.getClass().getSimpleName()), throwable.getMessage() != null ? Seq.fromCharSequence(throwable.getMessage()) : Seq.EMPTY, YonaException.stacktraceToSequence(throwable, node));
    }
  }
}
