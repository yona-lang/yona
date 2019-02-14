package abzu.ast.pattern;

import abzu.ast.expression.AliasNode;
import abzu.ast.expression.IdentifierNode;
import abzu.runtime.NodeMaker;
import abzu.runtime.Sequence;
import com.oracle.truffle.api.frame.VirtualFrame;

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
    if (value instanceof Sequence) {
      Sequence sequence = (Sequence) value;
      List<AliasNode> aliases = new ArrayList<>();

      MatchResult matchResult = matchNode.match(sequence, frame);
      if (!matchResult.isMatches()) {
        return MatchResult.FALSE;
      } else {
        for (AliasNode aliasNode : matchResult.getAliases()) {
          aliases.add(aliasNode);
        }

        aliases.add(new AliasNode(identifierNode.name(), NodeMaker.makeNode(sequence)));
        for (AliasNode aliasNode : aliases) {
          aliasNode.executeGeneric(frame);
        }

        return new MatchResult(true, new AliasNode[]{});
      }
    }

    return MatchResult.FALSE;
  }
}
