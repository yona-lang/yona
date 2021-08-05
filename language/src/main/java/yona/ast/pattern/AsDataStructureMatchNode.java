package yona.ast.pattern;

import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.ast.AliasNode;
import yona.ast.expression.IdentifierNode;
import yona.ast.expression.NameAliasNode;
import yona.ast.expression.value.AnyValueNode;

import java.util.Objects;

@NodeInfo(shortName = "asDataStructureMatch")
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
    matchNode.setValue(value);
    MatchResult matchResult = (MatchResult) matchNode.executeGeneric(frame);
    if (!matchResult.isMatches()) {
      return MatchResult.FALSE;
    } else {
      for (AliasNode nameAliasNode : matchResult.getAliases()) {
        nameAliasNode.executeGeneric(frame);
      }
      AliasNode identifierAlias = new NameAliasNode(identifierNode.name(), new AnyValueNode(value));
      identifierAlias.executeGeneric(frame);

      return MatchResult.TRUE;
    }
  }

  @Override
  protected String[] requiredIdentifiers() {
    return matchNode.getRequiredIdentifiers();
  }

  @Override
  protected String[] providedIdentifiers() {
    return matchNode.getProvidedIdentifiers();
  }
}
