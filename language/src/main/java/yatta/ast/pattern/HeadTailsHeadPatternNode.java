package yatta.ast.pattern;

import com.oracle.truffle.api.frame.VirtualFrame;
import yatta.ast.ExpressionNode;
import yatta.ast.expression.AliasNode;
import yatta.ast.expression.IdentifierNode;
import yatta.ast.expression.value.AnyValueNode;
import yatta.ast.expression.value.EmptySequenceNode;
import yatta.runtime.Seq;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Objects;

public class HeadTailsHeadPatternNode extends MatchNode {
  @Children
  public final MatchNode[] leftNodes;
  @Child
  public ExpressionNode tailsNode;
  @Children
  public final MatchNode[] rightPatterns;

  public HeadTailsHeadPatternNode(MatchNode[] leftNodes, ExpressionNode tailsNode, MatchNode[] rightPatterns) {
    this.leftNodes = leftNodes;
    this.tailsNode = tailsNode;
    this.rightPatterns = rightPatterns;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    HeadTailsHeadPatternNode that = (HeadTailsHeadPatternNode) o;
    return Arrays.equals(leftNodes, that.leftNodes) &&
        Objects.equals(tailsNode, that.tailsNode) &&
        Arrays.equals(rightPatterns, that.rightPatterns);
  }

  @Override
  public int hashCode() {
    int result = Objects.hash(tailsNode);
    result = 31 * result + Arrays.hashCode(leftNodes);
    result = 31 * result + Arrays.hashCode(rightPatterns);
    return result;
  }

  @Override
  public String toString() {
    return "HeadTailsHeadPatternNode{" +
        "leftNodes=" + Arrays.toString(leftNodes) +
        ", tailsNode=" + tailsNode +
        ", rightPatterns=" + Arrays.toString(rightPatterns) +
        '}';
  }

  @Override
  public MatchResult match(Object value, VirtualFrame frame) {
    if (value instanceof Seq) {
      Seq sequence = (Seq) value;
      List<AliasNode> aliases = new ArrayList<>();

      if (leftNodes.length + rightPatterns.length > sequence.length()) {
        return MatchResult.FALSE;
      }

      if (sequence.length() > 0) {
        for (int i = 0; i < leftNodes.length; i++) {
          MatchNode headNode = leftNodes[i];
          MatchResult headMatches = headNode.match(sequence.first(this), frame);
          if (headMatches.isMatches()) {
            for (AliasNode aliasNode : headMatches.getAliases()) {
              aliases.add(aliasNode);
            }
            sequence = sequence.removeFirst(this);
          } else {
            return MatchResult.FALSE;
          }
        }

        for (int i = rightPatterns.length - 1; i >= 0; i--) {
          MatchNode headNode = rightPatterns[i];
          MatchResult headMatches = headNode.match(sequence.last(this), frame);
          if (headMatches.isMatches()) {
            for (AliasNode aliasNode : headMatches.getAliases()) {
              aliases.add(aliasNode);
            }
            sequence = sequence.removeLast(this);
          } else {
            return MatchResult.FALSE;
          }
        }

        // YattaParser.g4: tails : identifier | emptySequence | underscore ;
        if (tailsNode instanceof IdentifierNode) {
          IdentifierNode identifierNode = (IdentifierNode) tailsNode;

          if (identifierNode.isBound(frame)) {
            Seq identifierValue = (Seq) identifierNode.executeGeneric(frame);

            if (!Objects.equals(identifierValue, sequence)) {
              return MatchResult.FALSE;
            }
          } else {
            aliases.add(new AliasNode(identifierNode.name(), new AnyValueNode(sequence)));
          }
        } else if (tailsNode instanceof EmptySequenceNode) {
          if (sequence.length() > 0) {
            return MatchResult.FALSE;
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
