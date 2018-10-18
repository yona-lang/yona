package abzu;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.TruffleException;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.nodes.NodeInfo;
import com.oracle.truffle.api.source.SourceSection;
import abzu.runtime.AbzuContext;
import abzu.runtime.AbzuFunction;
import abzu.runtime.AbzuNone;

public class AbzuException extends RuntimeException implements TruffleException {
  private static final long serialVersionUID = -6799734413727348507L;

  private final Node location;

  @CompilerDirectives.TruffleBoundary
  public AbzuException(String message, Node location) {
    super(message);
    this.location = location;
  }

  @SuppressWarnings("sync-override")
  @Override
  public Throwable fillInStackTrace() {
    return null;
  }

  public Node getLocation() {
    return location;
  }

  /**
   * Provides a user-readable message for run-time type errors. SL is strongly typed, i.e., there
   * are no automatic type conversions of values.
   */
  @CompilerDirectives.TruffleBoundary
  public static AbzuException typeError(Node operation, Object... values) {
    StringBuilder result = new StringBuilder();
    result.append("Type error");

    if (operation != null) {
      SourceSection ss = operation.getEncapsulatingSourceSection();
      if (ss != null && ss.isAvailable()) {
        result.append(" at ").append(ss.getSource().getName()).append(" line ").append(ss.getStartLine()).append(" col ").append(ss.getStartColumn());
      }
    }

    result.append(": operation");
    if (operation != null) {
      NodeInfo nodeInfo = AbzuContext.lookupNodeInfo(operation.getClass());
      if (nodeInfo != null) {
        result.append(" \"").append(nodeInfo.shortName()).append("\"");
      }
    }

    result.append(" not defined for");

    String sep = " ";
    for (int i = 0; i < values.length; i++) {
      Object value = values[i];
      result.append(sep);
      sep = ", ";
      if (value instanceof Long) {
        result.append("Number ").append(value);
      } else if (value instanceof Boolean) {
        result.append("Boolean ").append(value);
      } else if (value instanceof String) {
        result.append("String \"").append(value).append("\"");
      } else if (value instanceof AbzuFunction) {
        result.append("Function ").append(value);
      } else if (value == AbzuNone.SINGLETON) {
        result.append("NULL");
      } else if (value == null) {
        // value is not evaluated because of short circuit evaluation
        result.append("ANY");
      } else {
        result.append(value);
      }
    }
    return new AbzuException(result.toString(), operation);
  }
}
