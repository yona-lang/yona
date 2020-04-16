package yatta.ast.pattern;

import yatta.ast.ExpressionNode;
import yatta.ast.expression.AliasNode;
import yatta.ast.expression.FrameSlotAliasNode;
import yatta.ast.expression.NameAliasNode;
import yatta.ast.expression.IdentifierNode;
import yatta.ast.expression.value.AnyValueNode;
import com.oracle.truffle.api.frame.VirtualFrame;
import yatta.ast.local.ReadLocalVariableNode;
import yatta.runtime.exceptions.UninitializedFrameSlotException;

import java.util.Objects;

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
    if (expression instanceof IdentifierNode) {
      IdentifierNode identifierNode = (IdentifierNode) expression;

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
    } else if (expression instanceof ReadLocalVariableNode) {
      ReadLocalVariableNode readLocalVariableNode = (ReadLocalVariableNode) expression;
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

  public ExpressionNode getExpression() {
    return expression;
  }
}
