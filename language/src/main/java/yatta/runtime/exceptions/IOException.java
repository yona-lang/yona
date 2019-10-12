package yatta.runtime.exceptions;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.nodes.Node;
import yatta.YattaException;
import yatta.runtime.annotations.ExceptionSymbol;

@ExceptionSymbol("ioerror")
public final class IOException extends YattaException {
  @CompilerDirectives.TruffleBoundary
  public IOException(String message, Node location) {
    super(message, location);
  }

  @CompilerDirectives.TruffleBoundary
  public IOException(Throwable cause, Node location) {
    super(cause, location);
  }
}
