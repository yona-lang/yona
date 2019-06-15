package yatta.runtime;

import yatta.YattaException;
import com.oracle.truffle.api.nodes.Node;

public final class BadArgException extends YattaException {
  public BadArgException(String message, Node location) {
    super(message, location);
  }
}
