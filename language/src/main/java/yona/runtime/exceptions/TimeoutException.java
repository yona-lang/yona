package yona.runtime.exceptions;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.nodes.Node;
import yona.YonaException;
import yona.runtime.annotations.ExceptionSymbol;

@ExceptionSymbol("timeout")
public class TimeoutException extends YonaException {
  @CompilerDirectives.TruffleBoundary
  public TimeoutException(Node location) {
    super("Async value timed out", location);
  }
}
