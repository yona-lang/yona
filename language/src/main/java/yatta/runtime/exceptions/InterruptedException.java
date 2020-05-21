package yatta.runtime.exceptions;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.nodes.Node;
import yatta.YattaException;
import yatta.runtime.annotations.ExceptionSymbol;

@ExceptionSymbol("interrupted")
public class InterruptedException extends YattaException {
  @CompilerDirectives.TruffleBoundary
  public InterruptedException(Node location) {
    super("Async value timed out", location);
  }

  @CompilerDirectives.TruffleBoundary
  public InterruptedException(Throwable cause, Node location) {
    super(cause, location);
  }
}
