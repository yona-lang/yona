package yatta.ast.controlflow;

import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import yatta.YattaLanguage;
import yatta.ast.ExpressionNode;
import yatta.ast.call.InvokeNode;
import yatta.ast.expression.value.FQNNode;

import java.util.Objects;

@NodeInfo
public final class PipeLeftNode extends ExpressionNode {
  @Child private ExpressionNode leftExpression;
  @Child private ExpressionNode rightExpression;
  @Children private final FQNNode[] moduleStack;
  private final YattaLanguage language;

  public PipeLeftNode(YattaLanguage language, ExpressionNode leftExpression, ExpressionNode rightExpression, FQNNode[] moduleStack) {
    this.language = language;
    this.leftExpression = leftExpression;
    this.rightExpression = rightExpression;
    this.moduleStack = moduleStack;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    PipeLeftNode that = (PipeLeftNode) o;
    return Objects.equals(leftExpression, that.leftExpression) &&
        Objects.equals(rightExpression, that.rightExpression);
  }

  @Override
  public int hashCode() {
    return Objects.hash(leftExpression, rightExpression);
  }

  @Override
  public String toString() {
    return "PipeLeftNode{" +
        "leftExpression=" + leftExpression +
        ", rightExpression=" + rightExpression +
        '}';
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    InvokeNode invokeNode = new InvokeNode(language, leftExpression, new ExpressionNode[]{rightExpression}, moduleStack);
    return invokeNode.executeGeneric(frame);
  }
}
