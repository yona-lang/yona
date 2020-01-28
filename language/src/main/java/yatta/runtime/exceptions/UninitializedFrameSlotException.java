package yatta.runtime.exceptions;

public final class UninitializedFrameSlotException extends RuntimeException {
  public static UninitializedFrameSlotException INSTANCE = new UninitializedFrameSlotException();

  private UninitializedFrameSlotException() {
  }
}
