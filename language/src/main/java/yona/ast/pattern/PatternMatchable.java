package yona.ast.pattern;

import com.oracle.truffle.api.frame.VirtualFrame;
import yona.ast.ExpressionNode;

public abstract class PatternMatchable extends ExpressionNode {
  public abstract Object patternMatch(Object value, VirtualFrame frame) throws MatchControlFlowException;
}
