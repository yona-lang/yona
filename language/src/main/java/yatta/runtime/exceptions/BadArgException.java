package yatta.runtime.exceptions;

import com.oracle.truffle.api.CompilerDirectives;
import yatta.YattaException;
import com.oracle.truffle.api.nodes.Node;
import yatta.runtime.annotations.ExceptionSymbol;

@ExceptionSymbol("badarg")
public final class BadArgException extends YattaException {
  @CompilerDirectives.TruffleBoundary
  public BadArgException(String message, Node location) {
    super(message, location);
  }
}
