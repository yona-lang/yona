package yatta.ast.binary;

import com.oracle.truffle.api.dsl.NodeChild;
import com.oracle.truffle.api.dsl.NodeChildren;
import com.oracle.truffle.api.dsl.UnsupportedSpecializationException;
import com.oracle.truffle.api.frame.VirtualFrame;
import yatta.YattaException;
import yatta.ast.ExpressionNode;
import yatta.runtime.DependencyUtils;

@NodeChildren({
    @NodeChild(value = "left", type = ExpressionNode.class),
    @NodeChild(value = "right", type = ExpressionNode.class)
})
public abstract class BinaryOpNode extends ExpressionNode {
  @Override
  public final Object executeGeneric(VirtualFrame frame) {
    try {
      return execute(frame);
    } catch (UnsupportedSpecializationException e) {
      throw YattaException.typeError(e.getNode(), e.getSuppliedValues());
    }
  }

  protected abstract Object execute(VirtualFrame frame);

  @Override
  protected String[] requiredIdentifiers() {
    return DependencyUtils.catenateRequiredIdentifiers(left, right);
  }

  private ExpressionNode left, right;

  public BinaryOpNode setLeft(ExpressionNode left) {
    this.left = left;
    return this;
  }

  public BinaryOpNode setRight(ExpressionNode right) {
    this.right = right;
    return this;
  }
}
