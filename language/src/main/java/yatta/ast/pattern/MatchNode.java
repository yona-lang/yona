package yatta.ast.pattern;

import yatta.ast.ExpressionNode;
import com.oracle.truffle.api.frame.VirtualFrame;

public abstract class MatchNode extends ExpressionNode {
  @Override
  public Object executeGeneric(VirtualFrame frame) {
    return null;
  }

  public abstract MatchResult match(Object value, VirtualFrame frame);
}
