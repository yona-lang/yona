package yatta.ast.pattern;

import com.oracle.truffle.api.frame.VirtualFrame;
import yatta.ast.ExpressionNode;

public abstract class PatternMatchable extends ExpressionNode {
  public abstract Object patternMatch(Object value, VirtualFrame frame) throws MatchControlFlowException;
}
