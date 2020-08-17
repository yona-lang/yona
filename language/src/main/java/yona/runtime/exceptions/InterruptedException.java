package yona.runtime.exceptions;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.nodes.Node;
import yona.YonaException;
import yona.runtime.annotations.ExceptionSymbol;

@ExceptionSymbol("interrupted")
public class InterruptedException extends YonaException {
  @CompilerDirectives.TruffleBoundary
  public InterruptedException(Node location) {
    super("Async value timed out", location);
  }

  @CompilerDirectives.TruffleBoundary
  public InterruptedException(Throwable cause, Node location) {
    super(cause, location);
  }
}
