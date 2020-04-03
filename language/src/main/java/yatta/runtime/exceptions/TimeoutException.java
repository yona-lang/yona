package yatta.runtime.exceptions;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.nodes.Node;
import yatta.YattaException;
import yatta.runtime.annotations.ExceptionSymbol;

@ExceptionSymbol("timeout")
public class TimeoutException extends YattaException  {
  @CompilerDirectives.TruffleBoundary
  public TimeoutException(Node location) {
    super("", location);
  }
}
