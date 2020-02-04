package yatta.ast.generators;

import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import yatta.YattaLanguage;
import yatta.ast.ExpressionNode;
import yatta.ast.call.InvokeNode;

@NodeInfo
public final class SequenceGeneratorNode extends BaseGeneratorNode {
  @Child private InvokeNode callNode;

  public SequenceGeneratorNode(YattaLanguage language, ExpressionNode reducer, ExpressionNode condition, String stepName, ExpressionNode stepExpression, ExpressionNode[] moduleStack) {
    this.callNode = getGeneratorNode(language, GeneratedCollection.SEQ, reducer, condition, stepName, stepExpression, moduleStack);
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    this.replace(callNode);
    return callNode.executeGeneric(frame);
  }
}
