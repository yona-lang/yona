package yatta.runtime.exceptions;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.nodes.Node;
import yatta.YattaException;
import yatta.runtime.annotations.ExceptionSymbol;

@ExceptionSymbol("norecord")
public final class NoRecordException extends YattaException {
  @CompilerDirectives.TruffleBoundary
  public NoRecordException(String recordType, Node location) {
    super("NoRecordException: " + recordType, location);
  }
}
