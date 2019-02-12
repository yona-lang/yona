package abzu.ast.pattern;

import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInterface;

public interface PatternMatchable extends NodeInterface {
  public Object patternMatch(Object value, VirtualFrame frame) throws MatchException;
}
