package yona.runtime.exceptions;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.nodes.Node;
import yona.YonaException;
import yona.runtime.annotations.ExceptionSymbol;

@ExceptionSymbol("invalidrecord")
public final class InvalidRecordException extends YonaException {
  @CompilerDirectives.TruffleBoundary
  public InvalidRecordException(Object data, Node location) {
    super("InvalidException: " + data, location);
  }
}
