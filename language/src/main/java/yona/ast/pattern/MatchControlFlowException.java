package yona.ast.pattern;

import com.oracle.truffle.api.nodes.ControlFlowException;

public class MatchControlFlowException extends ControlFlowException {
  public static MatchControlFlowException INSTANCE = new MatchControlFlowException();

  private MatchControlFlowException() {
  }
}
