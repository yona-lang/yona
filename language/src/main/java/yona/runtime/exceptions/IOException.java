package yona.runtime.exceptions;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.nodes.Node;
import yona.YonaException;
import yona.runtime.Seq;
import yona.runtime.annotations.ExceptionSymbol;

@ExceptionSymbol("ioerror")
public final class IOException extends YonaException {
  @CompilerDirectives.TruffleBoundary
  public IOException(String message, Node location) {
    super(message, location);
  }

  @CompilerDirectives.TruffleBoundary
  public IOException(Seq message, Node location) {
    super(message, location);
  }

  @CompilerDirectives.TruffleBoundary
  public IOException(Throwable cause, Node location) {
    super(cause, location);
  }
}
