package yatta.runtime.exceptions;

import com.oracle.truffle.api.CompilerDirectives;
import yatta.YattaException;
import com.oracle.truffle.api.nodes.Node;
import yatta.runtime.Seq;
import yatta.runtime.annotations.ExceptionSymbol;

@ExceptionSymbol("badarg")
public final class BadArgException extends YattaException {
  @CompilerDirectives.TruffleBoundary
  public BadArgException(String message, Node location) {
    super(message, location);
  }

  @CompilerDirectives.TruffleBoundary
  public BadArgException(Seq message, Node location) {
    super(message, location);
  }
}
