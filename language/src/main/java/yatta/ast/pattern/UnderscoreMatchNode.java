package yatta.ast.pattern;

import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;

@NodeInfo(shortName = "underscoreMatch")
public final class UnderscoreMatchNode extends MatchNode {
  public UnderscoreMatchNode() {
  }

  @Override
  public String toString() {
    return "UnderscoreMatchNode{}";
  }

  @Override
  protected String[] requiredIdentifiers() {
    return new String[0];
  }

  @Override
  public MatchResult match(Object value, VirtualFrame frame) {
    return MatchResult.TRUE;
  }

  @Override
  protected String[] providedIdentifiers() {
    return new String[0];
  }
}
