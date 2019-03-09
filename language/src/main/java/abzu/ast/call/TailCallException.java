package abzu.ast.call;

import abzu.runtime.Function;
import com.oracle.truffle.api.nodes.ControlFlowException;

public class TailCallException extends ControlFlowException {
  public final Function function;
  public final Object[] arguments;

  public TailCallException(Function function, Object[] arguments) {
    this.function = function;
    this.arguments = arguments;
  }
}
