package yatta.runtime.exceptions;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.nodes.Node;
import yatta.YattaException;
import yatta.runtime.Seq;
import yatta.runtime.annotations.ExceptionSymbol;

@ExceptionSymbol("stm")
public final class STMException extends YattaException {
  @CompilerDirectives.TruffleBoundary
  public STMException(String message, Node location) {
    super(message, location);
  }

  @CompilerDirectives.TruffleBoundary
  public STMException(Seq message, Node location) {
    super(message, location);
  }

  @CompilerDirectives.TruffleBoundary
  public STMException(Throwable cause, Node location) {
    super(cause, location);
  }
}
