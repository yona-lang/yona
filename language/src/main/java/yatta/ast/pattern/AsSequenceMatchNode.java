package yatta.ast.pattern;

import yatta.ast.expression.AliasNode;
import yatta.ast.expression.IdentifierNode;
import yatta.ast.expression.value.AnyValueNode;
import com.oracle.truffle.api.frame.VirtualFrame;
import yatta.runtime.Seq;

import javax.sound.midi.Sequence;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;

public final class AsSequenceMatchNode extends MatchNode {
  @Child
  public IdentifierNode identifierNode;
  @Child
  public MatchNode matchNode;

  public AsSequenceMatchNode(IdentifierNode identifierNode, MatchNode matchNode) {
    this.identifierNode = identifierNode;
    this.matchNode = matchNode;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    AsSequenceMatchNode that = (AsSequenceMatchNode) o;
    return Objects.equals(identifierNode, that.identifierNode) &&
        Objects.equals(matchNode, that.matchNode);
  }

  @Override
  public int hashCode() {
    return Objects.hash(identifierNode, matchNode);
  }

  @Override
  public String toString() {
    return "AsSequenceMatchNode{" +
        "identifierNode=" + identifierNode +
        ", matchNode=" + matchNode +
        '}';
  }

  @Override
  public MatchResult match(Object value, VirtualFrame frame) {
    if (value instanceof Seq) {
      Seq sequence = (Seq) value;
      List<AliasNode> aliases = new ArrayList<>();

      MatchResult matchResult = matchNode.match(sequence, frame);
      if (!matchResult.isMatches()) {
        return MatchResult.FALSE;
      } else {
        for (AliasNode aliasNode : matchResult.getAliases()) {
          aliases.add(aliasNode);
        }

        aliases.add(new AliasNode(identifierNode.name(), new AnyValueNode(sequence)));
        for (AliasNode aliasNode : aliases) {
          aliasNode.executeGeneric(frame);
        }

        return MatchResult.TRUE;
      }
    }

    return MatchResult.FALSE;
  }
}
