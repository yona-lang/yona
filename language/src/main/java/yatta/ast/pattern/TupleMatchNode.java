package yatta.ast.pattern;

import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.ExplodeLoop;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.nodes.NodeInfo;
import yatta.ast.AliasNode;
import yatta.ast.ExpressionNode;
import yatta.runtime.DependencyUtils;
import yatta.runtime.Tuple;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Objects;

@NodeInfo(shortName = "tupleMatch")
public final class TupleMatchNode extends MatchNode {
  @Node.Children
  public ExpressionNode[] expressions;

  public TupleMatchNode(ExpressionNode... expressions) {
    this.expressions = expressions;
  }

  @Override
  public String toString() {
    return "TupleMatchNode{" +
        "expressions=" + Arrays.toString(expressions) +
        '}';
  }

  @Override
  protected String[] requiredIdentifiers() {
    return DependencyUtils.catenateRequiredIdentifiers(expressions);
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    TupleMatchNode that = (TupleMatchNode) o;
    return Arrays.equals(expressions, that.expressions);
  }

  @Override
  public int hashCode() {
    return Arrays.hashCode(expressions);
  }

  @Override
  public MatchResult match(Object value, VirtualFrame frame) {
    if (value instanceof Tuple) {
      Tuple tuple = (Tuple) value;

      if (tuple.size() == expressions.length) {
        List<AliasNode> aliases = new ArrayList<>();

        for (int i = 0; i < expressions.length; i++) {
          if (expressions[i] instanceof MatchNode) {
            MatchNode matchNode = (MatchNode) expressions[i];
            MatchResult nestedMatchResult = matchNode.match(tuple.get(i), frame);

            if (!nestedMatchResult.isMatches()) {
              return MatchResult.FALSE;
            } else {
              aliases.addAll(Arrays.asList(nestedMatchResult.getAliases()));
            }
          } else {
            Object exprVal = expressions[i].executeGeneric(frame);

            if (!Objects.equals(exprVal, tuple.get(i))) {
              return MatchResult.FALSE;
            }
          }
        }

        for (AliasNode nameAliasNode : aliases) {
          nameAliasNode.executeGeneric(frame);
        }

        return MatchResult.TRUE;
      }
    }

    return MatchResult.FALSE;
  }

  @Override
  @ExplodeLoop
  protected String[] providedIdentifiers() {
    int matchNodesLen = 0;

    for (ExpressionNode expressionNode : expressions) {
      if (expressionNode instanceof MatchNode) {
        matchNodesLen += 1;
      }
    }

    MatchNode[] matchNodes = new MatchNode[matchNodesLen];

    int i = 0;
    for (ExpressionNode expressionNode : expressions) {
      if (expressionNode instanceof MatchNode) {
        matchNodes[i++] = (MatchNode) expressionNode;
      }
    }

    return DependencyUtils.catenateProvidedIdentifiers(matchNodes);
  }
}
