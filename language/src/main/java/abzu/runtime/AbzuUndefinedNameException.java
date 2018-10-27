package abzu.runtime;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.nodes.Node;
import abzu.AbzuException;

public final class AbzuUndefinedNameException extends AbzuException {

  private static final long serialVersionUID = 1L;

  @CompilerDirectives.TruffleBoundary
  public static AbzuUndefinedNameException undefinedFunction(Node location, Object name) {
    throw new AbzuUndefinedNameException("Undefined function: " + name, location);
  }

  @CompilerDirectives.TruffleBoundary
  public static AbzuUndefinedNameException undefinedProperty(Node location, Object name) {
    throw new AbzuUndefinedNameException("Undefined property: " + name, location);
  }

  private AbzuUndefinedNameException(String message, Node node) {
    super(message, node);
  }
}
