package abzu.runtime;

import abzu.AbzuException;
import com.oracle.truffle.api.nodes.Node;

public final class BadArgException extends AbzuException {
  public BadArgException(String message, Node location) {
    super(message, location);
  }
}
