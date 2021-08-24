package yona.runtime.exceptions;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.nodes.Node;
import yona.YonaException;
import yona.runtime.annotations.ExceptionSymbol;

@ExceptionSymbol("json_parser_error")
public class JSONParserException extends YonaException {
  @CompilerDirectives.TruffleBoundary
  public JSONParserException(String text, Node location) {
    super("JSON parse error occurred at: " + text, location);
  }
}
