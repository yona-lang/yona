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

public final class TailsHeadMatchPatternNode extends CommonSequencePatternNode {
  @Child
  public MatchNode headNode;
  @Child
  public ExpressionNode tailsNode;

  public TailsHeadMatchPatternNode(ExpressionNode tailsNode, MatchNode headNode) {
    this.headNode = headNode;
    this.tailsNode = tailsNode;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    TailsHeadMatchPatternNode that = (TailsHeadMatchPatternNode) o;
    return Objects.equals(headNode, that.headNode) &&
        Objects.equals(tailsNode, that.tailsNode);
  }

  @Override
  public int hashCode() {
    return Objects.hash(headNode, tailsNode);
  }

  @Override
  public String toString() {
    return "TailsHeadMatchPatternNode{" +
        "headNode=" + headNode +
        ", tailsNode=" + tailsNode +
        '}';
  }

  @Override
  public MatchResult match(Object value, VirtualFrame frame) {
    if (value instanceof Sequence) {
      Sequence sequence = (Sequence) value;
      List<AliasNode> aliases = new ArrayList<>();

      if (sequence.length() > 0) {
        MatchResult headMatches = headNode.match(sequence.last(), frame);
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

            if (!Objects.equals(identifierValue, sequence.removeLast())) {
              return MatchResult.FALSE;
            }
          } else {
            aliases.add(new AliasNode(identifierNode.name(), NodeMaker.makeNode(sequence.removeFirst())));
          }
        } else if (tailsNode instanceof EmptySequenceNode) {
          if (sequence.removeFirst().length() > 0) {
            return MatchResult.FALSE;
          }
        }

        for (AliasNode aliasNode : aliases) {
          aliasNode.executeGeneric(frame);
        }

        return MatchResult.TRUE;
      } else if (sequence.length() == 0) {
        MatchResult matchResult = matchEmptySequence(sequence, tailsNode, headNode, frame);
        if (!matchResult.isMatches()) {
          return MatchResult.FALSE;
        } else {
          for (AliasNode aliasNode : matchResult.getAliases()) {
            aliasNode.executeGeneric(frame);
          }
          return MatchResult.TRUE;
        }
      }
    }

    return MatchResult.FALSE;
  }
}
