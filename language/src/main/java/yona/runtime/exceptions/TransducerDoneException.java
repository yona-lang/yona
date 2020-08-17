package yona.runtime.exceptions;

import com.oracle.truffle.api.nodes.ControlFlowException;

public final class TransducerDoneException extends ControlFlowException {
  public static final TransducerDoneException INSTANCE = new TransducerDoneException();

  private TransducerDoneException() {
    super();
  }
}
