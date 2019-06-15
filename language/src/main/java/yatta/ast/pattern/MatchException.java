package yatta.ast.pattern;

import com.oracle.truffle.api.nodes.ControlFlowException;

public class MatchException extends ControlFlowException {
  public static MatchException INSTANCE = new MatchException();

  private MatchException() {
  }
}
