package yatta.runtime.exceptions;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.nodes.Node;
import yatta.YattaException;
import yatta.runtime.annotations.ExceptionSymbol;

@ExceptionSymbol("invalidrecord")
public final class InvalidRecordException extends YattaException {
  @CompilerDirectives.TruffleBoundary
  public InvalidRecordException(Object data, Node location) {
    super("InvalidException: " + data, location);
  }
}
