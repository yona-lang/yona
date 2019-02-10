package abzu.runtime;

import com.oracle.truffle.api.nodes.ControlFlowException;

public final class UninitializedFrameSlotException extends ControlFlowException {
  public static UninitializedFrameSlotException INSTANCE = new UninitializedFrameSlotException();

  private UninitializedFrameSlotException() {
  }
}
