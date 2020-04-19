package yatta.runtime.exceptions;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.nodes.Node;
import yatta.YattaException;
import yatta.runtime.Seq;
import yatta.runtime.annotations.ExceptionSymbol;

@ExceptionSymbol("java")
public final class JavaException extends YattaException {
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
