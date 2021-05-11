package yona.ast.pattern;

import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.ast.AliasNode;
import yona.ast.ExpressionNode;
import yona.ast.expression.FrameSlotAliasNode;
import yona.ast.expression.IdentifierNode;
import yona.ast.expression.NameAliasNode;
import yona.ast.expression.value.AnyValueNode;
import yona.ast.local.ReadLocalVariableNode;
import yona.runtime.exceptions.UninitializedFrameSlotException;

import java.util.Objects;

@NodeInfo(shortName = "valueMatch")
public final class ValueMatchNode extends MatchNode {
  @Child
  private ExpressionNode expression;

  public ValueMatchNode(ExpressionNode expression) {
    this.expression = expression;
  }

  @Override
  public String toString() {
    return "ValueMatchNode{" +
        "expression=" + expression +
        '}';
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    ValueMatchNode that = (ValueMatchNode) o;
    return Objects.equals(expression, that.expression);
  }

  @Override
  public int hashCode() {
    return Objects.hash(expression);
  }

  @Override
  public MatchResult match(Object value, VirtualFrame frame) {
    if (expression instanceof IdentifierNode identifierNode) {

      if (identifierNode.isBound(frame)) {
        Object identifierValue = identifierNode.executeGeneric(frame);

        if (!Objects.equals(identifierValue, value)) {
          return MatchResult.FALSE;
        } else {
          return MatchResult.TRUE;
        }
      } else {
        return new MatchResult(true, new AliasNode[]{new NameAliasNode(identifierNode.name(), new AnyValueNode(value))});
      }
    } else if (expression instanceof ReadLocalVariableNode readLocalVariableNode) {
      boolean isBound;
      try {
        readLocalVariableNode.executeGeneric(frame);
        isBound = true;
      } catch (UninitializedFrameSlotException | IllegalStateException e) {
        isBound = false;
      }
      if (isBound) {
        Object readValue = readLocalVariableNode.executeGeneric(frame);
        if (!Objects.equals(readValue, value)) {
          return MatchResult.FALSE;
        } else {
          return MatchResult.TRUE;
        }
      } else {
        return new MatchResult(true, new AliasNode[]{new FrameSlotAliasNode(readLocalVariableNode.getSlot(), new AnyValueNode(value))});
      }
    } else {
      Object exprValue = expression.executeGeneric(frame);
      return Objects.equals(value, exprValue) ? MatchResult.TRUE : MatchResult.FALSE;
    }
  }

  @Override
  public String[] requiredIdentifiers() {
    return expression.getRequiredIdentifiers();
  }

  public ExpressionNode getExpression() {
    return expression;
  }

  @Override
  protected String[] providedIdentifiers() {
    if (expression instanceof IdentifierNode identifierNode) {
      return new String[]{identifierNode.name()};
    } else if (expression instanceof ReadLocalVariableNode readLocalVariableNode) {
      return new String[]{(String) readLocalVariableNode.getSlot().getIdentifier()};
    } else {
      return new String[0];
    }
  }
}
