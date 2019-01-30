package abzu.ast.pattern;

import abzu.ast.expression.AliasNode;
import abzu.runtime.Sequence;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.Node;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

public final class SequenceMatchPattern extends MatchNode {
  @Node.Children
  public MatchNode[] matchNodes;

  public SequenceMatchPattern(MatchNode[] matchNodes) {
    this.matchNodes = matchNodes;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    SequenceMatchPattern that = (SequenceMatchPattern) o;
    return Arrays.equals(matchNodes, that.matchNodes);
  }

  @Override
  public int hashCode() {
    return Arrays.hashCode(matchNodes);
  }

  @Override
  public String toString() {
    return "SequenceMatchPattern{" +
        "matchNodes=" + Arrays.toString(matchNodes) +
        '}';
  }

  @Override
  public MatchResult match(Object value, VirtualFrame frame) {
    if (value instanceof Sequence) {
      Sequence sequence = (Sequence) value;

      if (sequence.length() == matchNodes.length) {
        List<AliasNode> aliases = new ArrayList<>();

        for (int i = 0; i < matchNodes.length; i++) {
          MatchNode matchNode = matchNodes[i];
          MatchResult matchResult = matchNode.match(sequence.lookup(i), frame);

          if (!matchResult.isMatches()) {
            return MatchResult.FALSE;
          } else {
            for (AliasNode aliasNode : matchResult.getAliases()) {
              aliases.add(aliasNode);
            }
          }
        }

        return new MatchResult(true, aliases.toArray(new AliasNode[]{}));
      }
    }

    return MatchResult.FALSE;
  }
}
