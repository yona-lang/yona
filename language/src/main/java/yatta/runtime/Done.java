package yatta.runtime;

import yatta.YattaException;

public final class Done extends YattaException  {
  public static final Done INSTANCE = new Done();

  private Done() {
    super("done", null);
  }
}
