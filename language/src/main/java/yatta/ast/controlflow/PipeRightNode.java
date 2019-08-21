package yatta.ast.controlflow;

import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import yatta.YattaLanguage;
import yatta.ast.ExpressionNode;
import yatta.ast.call.InvokeNode;
import yatta.ast.expression.value.FQNNode;

import java.util.Objects;

@NodeInfo
public final class PipeRightNode extends ExpressionNode {
  @Child private ExpressionNode leftExpression;
  @Child private ExpressionNode rightExpression;
  @Children private final FQNNode[] moduleStack;
  private final YattaLanguage language;

  public PipeRightNode(YattaLanguage language, ExpressionNode leftExpression, ExpressionNode rightExpression, FQNNode[] moduleStack) {
    this.language = language;
    this.leftExpression = leftExpression;
    this.rightExpression = rightExpression;
    this.moduleStack = moduleStack;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    PipeRightNode that = (PipeRightNode) o;
    return Objects.equals(leftExpression, that.leftExpression) &&
        Objects.equals(rightExpression, that.rightExpression);
  }

  @Override
  public int hashCode() {
    return Objects.hash(leftExpression, rightExpression);
  }

  @Override
  public String toString() {
    return "PipeRightNode{" +
        "leftExpression=" + leftExpression +
        ", rightExpression=" + rightExpression +
        '}';
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    InvokeNode invokeNode = new InvokeNode(language, rightExpression, new ExpressionNode[]{leftExpression}, moduleStack);
    return invokeNode.executeGeneric(frame);
  }
}
