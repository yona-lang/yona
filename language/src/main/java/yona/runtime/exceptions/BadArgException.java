package yona.runtime.exceptions;

import com.oracle.truffle.api.CompilerDirectives;
import yona.YonaException;
import com.oracle.truffle.api.nodes.Node;
import yona.runtime.Seq;
import yona.runtime.annotations.ExceptionSymbol;

@ExceptionSymbol("badarg")
public final class BadArgException extends YonaException {
  @CompilerDirectives.TruffleBoundary
  public BadArgException(String message, Node location) {
    super(message, location);
  }

  @CompilerDirectives.TruffleBoundary
  public BadArgException(Seq message, Node location) {
    super(message, location);
  }

  @CompilerDirectives.TruffleBoundary
  public BadArgException(Throwable cause, Node location) {
    super(cause, location);
  }

  @CompilerDirectives.TruffleBoundary
  public BadArgException(String message, Throwable cause, Node location) {
    super(message, cause, location);
  }
}
