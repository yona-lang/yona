package yatta.runtime.exceptions;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.nodes.Node;
import yatta.YattaException;
import yatta.runtime.Seq;
import yatta.runtime.annotations.ExceptionSymbol;

@ExceptionSymbol("polyglot")
public final class PolyglotException extends YattaException {
  @CompilerDirectives.TruffleBoundary
  public PolyglotException(String message, Node location) {
    super(message, location);
  }

  @CompilerDirectives.TruffleBoundary
  public PolyglotException(String message, Throwable cause, Node location) {
    super(message, cause, location);
  }

  @CompilerDirectives.TruffleBoundary
  public PolyglotException(Seq message, Node location) {
    super(message, location);
  }

  @CompilerDirectives.TruffleBoundary
  public PolyglotException(Throwable cause, Node location) {
    super(cause, location);
  }
}
