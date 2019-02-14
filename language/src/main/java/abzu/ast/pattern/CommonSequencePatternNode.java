package abzu.ast.pattern;

import abzu.ast.ExpressionNode;
import abzu.ast.expression.AliasNode;
import abzu.ast.expression.IdentifierNode;
import abzu.ast.expression.value.EmptySequenceNode;
import abzu.runtime.NodeMaker;
import abzu.runtime.Sequence;
import com.oracle.truffle.api.frame.VirtualFrame;

import java.util.ArrayList;
import java.util.List;
import java.util.Objects;

public abstract class CommonSequencePatternNode extends MatchNode {
  public MatchResult matchEmptySequence(Sequence sequence, ExpressionNode tailsNode, MatchNode headNode, VirtualFrame frame) {
    List<AliasNode> aliases = new ArrayList<>();

    MatchResult headMatches = headNode.match(sequence, frame);
    if (headMatches.isMatches()) {
      for (AliasNode aliasNode : headMatches.getAliases()) {
        aliases.add(aliasNode);
      }
    } else {
      return MatchResult.FALSE;
    }

    // Abzu.g4: tails : identifier | emptySequence | underscore ;
    if (tailsNode instanceof IdentifierNode) {
      IdentifierNode identifierNode = (IdentifierNode) tailsNode;

      if (identifierNode.isBound(frame)) {
        Sequence identifierValue = (Sequence) identifierNode.executeGeneric(frame);

        if (!Objects.equals(identifierValue, sequence)) {
          return MatchResult.FALSE;
        }
      } else {
        aliases.add(new AliasNode(identifierNode.name(), NodeMaker.makeNode(sequence)));
      }
    } else if (tailsNode instanceof EmptySequenceNode) {
      if (sequence.length() > 0) {
        return MatchResult.FALSE;
      }
    }

    return new MatchResult(true, aliases.toArray(new AliasNode[] {}));
  }
}
