package abzu.ast.pattern;

import abzu.ast.ExpressionNode;
import abzu.ast.expression.AliasNode;
import abzu.ast.expression.IdentifierNode;
import abzu.runtime.NodeMaker;
import abzu.runtime.Tuple;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.Node;

import java.util.ArrayList;
import java.util.List;
import java.util.Objects;

public class TupleMatchNode extends MatchNode {
  @Node.Children
  public ExpressionNode[] expressions;

  public TupleMatchNode(ExpressionNode[] expressions) {
    this.expressions = expressions;
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
              for (AliasNode aliasNode : nestedMatchResult.getAliases()) {
                aliases.add(aliasNode);
              }
            }
          } else if (expressions[i] instanceof IdentifierNode) {
            IdentifierNode identifierNode = (IdentifierNode) expressions[i];

            if (identifierNode.isBound(frame)) {
              Object identifierValue = identifierNode.executeGeneric(frame);

              if (!Objects.equals(identifierValue, tuple.get(i))) {
                return MatchResult.FALSE;
              }
            } else {
              aliases.add(new AliasNode(identifierNode.name(), NodeMaker.makeNode(tuple.get(i))));
            }
          } else {
            Object exprVal = expressions[i].executeGeneric(frame);

            if (!Objects.equals(exprVal, tuple.get(i))) {
              return MatchResult.FALSE;
            }
          }
        }

        return new MatchResult(true, aliases.toArray(new AliasNode[]{}));
      }
    }

    return MatchResult.FALSE;
  }
}
