package abzu.ast.binary;

import abzu.AbzuException;
import abzu.ast.ExpressionNode;
import com.oracle.truffle.api.dsl.NodeChild;
import com.oracle.truffle.api.dsl.UnsupportedSpecializationException;
import com.oracle.truffle.api.frame.VirtualFrame;

@NodeChild(value = "arguments", type = ExpressionNode[].class)
public abstract class BinaryOpNode extends ExpressionNode {
  @Override
  public final Object executeGeneric(VirtualFrame frame) {
    try {
      return execute(frame);
    } catch (UnsupportedSpecializationException e) {
      throw AbzuException.typeError(e.getNode(), e.getSuppliedValues());
    }
  }

  protected abstract Object execute(VirtualFrame frame);
}
