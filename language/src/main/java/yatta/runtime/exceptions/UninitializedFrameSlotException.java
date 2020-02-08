package yatta.runtime.exceptions;

import com.oracle.truffle.api.nodes.ControlFlowException;

public final class UninitializedFrameSlotException extends ControlFlowException {
  public static final UninitializedFrameSlotException INSTANCE = new UninitializedFrameSlotException();

  private UninitializedFrameSlotException() {
  }
}
