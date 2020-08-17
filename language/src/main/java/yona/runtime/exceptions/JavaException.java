package yona.runtime.exceptions;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.nodes.Node;
import yona.YonaException;
import yona.runtime.Seq;
import yona.runtime.annotations.ExceptionSymbol;

@ExceptionSymbol("java")
public final class JavaException extends YonaException {
  @CompilerDirectives.TruffleBoundary
  public JavaException(String message, Node location) {
    super(message, location);
  }

  @CompilerDirectives.TruffleBoundary
  public JavaException(String message, Throwable cause, Node location) {
    super(message, cause, location);
  }

  @CompilerDirectives.TruffleBoundary
  public JavaException(Seq message, Node location) {
    super(message, location);
  }

  @CompilerDirectives.TruffleBoundary
  public JavaException(Throwable cause, Node location) {
    super(cause, location);
  }
}
