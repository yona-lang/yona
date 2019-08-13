package yatta.runtime;

import com.oracle.truffle.api.nodes.ControlFlowException;

public final class UninitializedFrameSlotException extends RuntimeException {
  public static UninitializedFrameSlotException INSTANCE = new UninitializedFrameSlotException();

  private UninitializedFrameSlotException() {
  }
}
