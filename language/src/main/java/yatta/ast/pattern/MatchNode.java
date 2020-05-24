package yatta.ast.pattern;

import com.oracle.truffle.api.frame.VirtualFrame;
import yatta.ast.AliasNode;

public abstract class MatchNode extends AliasNode {
  @Override
  public Object executeGeneric(VirtualFrame frame) {
    return null;
  }

  public abstract MatchResult match(Object value, VirtualFrame frame);
}
