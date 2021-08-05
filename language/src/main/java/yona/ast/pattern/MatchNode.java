package yona.ast.pattern;

import com.oracle.truffle.api.frame.VirtualFrame;
import yona.ast.AliasNode;

public abstract class MatchNode extends AliasNode {
  protected Object value;

  public void setValue(Object value) {
    this.value = value;
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    return match(value, frame);
  }

  protected abstract MatchResult match(Object value, VirtualFrame frame);
}
