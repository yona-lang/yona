package yona.ast.pattern;

import com.oracle.truffle.api.frame.VirtualFrame;
import yona.ast.AliasNode;

public abstract class MatchNode extends AliasNode {
  @Override
  public Object executeGeneric(VirtualFrame frame) {
    return null;
  }

  public abstract MatchResult match(Object value, VirtualFrame frame);
}
