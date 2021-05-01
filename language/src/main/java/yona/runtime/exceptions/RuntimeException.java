package yona.runtime.exceptions;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.nodes.Node;
import yona.YonaException;
import yona.runtime.Seq;
import yona.runtime.annotations.ExceptionSymbol;

@ExceptionSymbol("runtime")
public final class RuntimeException extends YonaException {
  @CompilerDirectives.TruffleBoundary
  public RuntimeException(String message, Node location) {
    super(message, location);
  }

  @CompilerDirectives.TruffleBoundary
  public RuntimeException(Seq message, Node location) {
    super(message, location);
  }

  @CompilerDirectives.TruffleBoundary
  public RuntimeException(Throwable cause, Node location) {
    super(cause, location);
  }

  @CompilerDirectives.TruffleBoundary
  public RuntimeException(String message, Throwable cause, Node location) {
    super(message, cause, location);
  }
}
