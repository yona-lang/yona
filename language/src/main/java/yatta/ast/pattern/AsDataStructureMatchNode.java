package yatta.ast.pattern;

import com.oracle.truffle.api.frame.VirtualFrame;
import yatta.ast.expression.AliasNode;
import yatta.ast.expression.IdentifierNode;
import yatta.ast.expression.value.AnyValueNode;

import java.util.Objects;

public final class AsDataStructureMatchNode extends MatchNode {
  @Child
  public IdentifierNode identifierNode;
  @Child
  public MatchNode matchNode;

  public AsDataStructureMatchNode(IdentifierNode identifierNode, MatchNode matchNode) {
    this.identifierNode = identifierNode;
    this.matchNode = matchNode;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    AsDataStructureMatchNode that = (AsDataStructureMatchNode) o;
    return Objects.equals(identifierNode, that.identifierNode) &&
        Objects.equals(matchNode, that.matchNode);
  }

  @Override
  public int hashCode() {
    return Objects.hash(identifierNode, matchNode);
  }

  @Override
  public String toString() {
    return "AsDataStructureMatchNode{" +
        "identifierNode=" + identifierNode +
        ", matchNode=" + matchNode +
        '}';
  }

  @Override
  public MatchResult match(Object value, VirtualFrame frame) {
    MatchResult matchResult = matchNode.match(value, frame);
    if (!matchResult.isMatches()) {
      return MatchResult.FALSE;
    } else {
      for (AliasNode aliasNode : matchResult.getAliases()) {
        aliasNode.executeGeneric(frame);
      }
      AliasNode identifierAlias = new AliasNode(identifierNode.name(), new AnyValueNode(value));
      identifierAlias.executeGeneric(frame);

      return MatchResult.TRUE;
    }
  }
}
