package yona.runtime.exceptions;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.nodes.Node;
import yona.YonaException;
import yona.runtime.annotations.ExceptionSymbol;

@ExceptionSymbol("norecordfield")
public final class NoRecordFieldException extends YonaException {
  @CompilerDirectives.TruffleBoundary
  public NoRecordFieldException(String recordType, String fieldName, Node location) {
    super("NoRecordFieldException: " + recordType + '(' + fieldName + ')' , location);
  }
}
