package yona.runtime.exceptions;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.nodes.Node;
import yona.YonaException;
import yona.runtime.Function;
import yona.runtime.Seq;
import yona.runtime.annotations.ExceptionSymbol;

@ExceptionSymbol("undefinedname")
public final class UndefinedNameException extends YonaException {

  private static final long serialVersionUID = 1L;

  @CompilerDirectives.TruffleBoundary
  public static UndefinedNameException undefinedFunction(Node location, Function name) {
    throw new UndefinedNameException("Undefined function: " + name, location);
  }

  @CompilerDirectives.TruffleBoundary
  public static UndefinedNameException undefinedProperty(Node location, Object name) {
    throw new UndefinedNameException("Undefined property: " + name, location);
  }

  private UndefinedNameException(String message, Node node) {
    super(message, node);
  }

  private UndefinedNameException(Seq message, Node node) {
    super(message, node);
  }
}
