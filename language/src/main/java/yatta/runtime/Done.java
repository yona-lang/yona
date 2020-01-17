package yatta.runtime;

import com.oracle.truffle.api.nodes.ControlFlowException;

public final class Done extends ControlFlowException {
  public static final Done INSTANCE = new Done();

  private Done() {
    super();
  }
}
