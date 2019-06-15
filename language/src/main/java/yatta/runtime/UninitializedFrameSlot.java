package yatta.runtime;

public final class UninitializedFrameSlot {
  public static UninitializedFrameSlot INSTANCE = new UninitializedFrameSlot();

  private UninitializedFrameSlot() {
  }
}
