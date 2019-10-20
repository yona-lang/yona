package yatta.ast.pattern;

import yatta.ast.expression.AliasNode;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.Node;
import yatta.runtime.Seq;

import javax.sound.midi.Sequence;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

public final class SequenceMatchPatternNode extends MatchNode {
  @Node.Children
  public MatchNode[] matchNodes;

  public SequenceMatchPatternNode(MatchNode[] matchNodes) {
    this.matchNodes = matchNodes;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    SequenceMatchPatternNode that = (SequenceMatchPatternNode) o;
    return Arrays.equals(matchNodes, that.matchNodes);
  }

  @Override
  public int hashCode() {
    return Arrays.hashCode(matchNodes);
  }

  @Override
  public String toString() {
    return "SequenceMatchPatternNode{" +
        "matchNodes=" + Arrays.toString(matchNodes) +
        '}';
  }

  @Override
  public MatchResult match(Object value, VirtualFrame frame) {
    if (value instanceof Seq) {
      Seq sequence = (Seq) value;

      if (sequence.length() == matchNodes.length) {
        List<AliasNode> aliases = new ArrayList<>();

        for (int i = 0; i < matchNodes.length; i++) {
          MatchNode matchNode = matchNodes[i];
          MatchResult matchResult = matchNode.match(sequence.lookup(i, this), frame);

          if (!matchResult.isMatches()) {
            return MatchResult.FALSE;
          } else {
            for (AliasNode aliasNode : matchResult.getAliases()) {
              aliases.add(aliasNode);
            }
          }
        }

        for (AliasNode aliasNode : aliases) {
          aliasNode.executeGeneric(frame);
        }

        return MatchResult.TRUE;
      }
    }

    return MatchResult.FALSE;
  }
}
