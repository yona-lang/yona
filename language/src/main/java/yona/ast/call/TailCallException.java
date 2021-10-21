package yona.ast.call;

import yona.runtime.Function;
import com.oracle.truffle.api.nodes.ControlFlowException;

public final class TailCallException extends ControlFlowException {
  public final Function function;
  public final Object[] arguments;

  public TailCallException(Function function, Object[] arguments) {
    this.function = function;
    this.arguments = arguments;
  }
}
