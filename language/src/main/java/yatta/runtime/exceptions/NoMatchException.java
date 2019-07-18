package yatta.runtime.exceptions;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.nodes.Node;
import yatta.YattaException;
import yatta.runtime.annotations.ExceptionSymbol;

@ExceptionSymbol("nomatch")
public final class NoMatchException extends YattaException {
  @CompilerDirectives.TruffleBoundary
  public NoMatchException(Node location) {
    super("NoMatchException", location);
  }

  @CompilerDirectives.TruffleBoundary
  public NoMatchException(Throwable cause, Node location) {
    super(cause, location);
  }
}
