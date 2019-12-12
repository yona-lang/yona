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
  @Child private InvokeNode invokeNode;

  public PipeRightNode(YattaLanguage language, ExpressionNode leftExpression, ExpressionNode rightExpression, FQNNode[] moduleStack) {
    this.invokeNode = new InvokeNode(language, rightExpression, new ExpressionNode[]{leftExpression}, moduleStack);
  }

  @Override
  public String toString() {
    return "PipeRightNode{" +
        "invokeNode=" + invokeNode +
        '}';
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    return invokeNode.executeGeneric(frame);
  }
}
