package abzu.runtime;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.nodes.Node;
import abzu.AbzuException;

public final class UndefinedNameException extends AbzuException {

  private static final long serialVersionUID = 1L;

  @CompilerDirectives.TruffleBoundary
  public static UndefinedNameException undefinedFunction(Node location, Object name) {
    throw new UndefinedNameException("Undefined function: " + name, location);
  }

  @CompilerDirectives.TruffleBoundary
  public static UndefinedNameException undefinedProperty(Node location, Object name) {
    throw new UndefinedNameException("Undefined property: " + name, location);
  }

  private UndefinedNameException(String message, Node node) {
    super(message, node);
  }
}
