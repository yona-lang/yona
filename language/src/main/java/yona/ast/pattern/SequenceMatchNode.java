package yona.ast.pattern;

import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.ast.AliasNode;
import yona.runtime.DependencyUtils;
import yona.runtime.Seq;

import java.util.Arrays;

@NodeInfo(shortName = "sequenceMatch")
public final class SequenceMatchNode extends MatchNode {
  @Node.Children
  public MatchNode[] matchNodes;

  public SequenceMatchNode(MatchNode[] matchNodes) {
    this.matchNodes = matchNodes;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    SequenceMatchNode that = (SequenceMatchNode) o;
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
  protected String[] requiredIdentifiers() {
    return DependencyUtils.catenateRequiredIdentifiers(matchNodes);
  }

  @Override
  public MatchResult match(Object value, VirtualFrame frame) {
    if (value instanceof Seq) {
      Seq sequence = (Seq) value;

      if (sequence.length() == matchNodes.length) {
        Seq aliases = Seq.EMPTY;

        for (int i = 0; i < matchNodes.length; i++) {
          MatchNode matchNode = matchNodes[i];
          MatchResult matchResult = matchNode.match(sequence.lookup(i, this), frame);

          if (!matchResult.isMatches()) {
            return MatchResult.FALSE;
          } else {
            aliases = Seq.catenate(aliases, Seq.sequence((Object[]) matchResult.getAliases()));
          }
        }

        aliases.foldLeft(null, (acc, alias) -> {
          ((AliasNode) alias).executeGeneric(frame);
          return null;
        });

        return MatchResult.TRUE;
      }
    }

    return MatchResult.FALSE;
  }

  @Override
  protected String[] providedIdentifiers() {
    return DependencyUtils.catenateProvidedIdentifiers(matchNodes);
  }
}
