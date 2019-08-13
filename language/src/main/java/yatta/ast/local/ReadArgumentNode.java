package yatta.ast.local;

import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import yatta.ast.ExpressionNode;

/**
 * Reads a function argument. Arguments are passed in as an object array.
 * <p>
 * Arguments are not type-specialized. To ensure that repeated accesses within a method are
 * specialized and can, e.g., be accessed without unboxing, all arguments are loaded into local
 * variables.
 */
@NodeInfo
public class ReadArgumentNode extends ExpressionNode {
  /** The argument number, i.e., the index into the array of arguments. */
  private final int index;

  public ReadArgumentNode(int index) {
    this.index = index;
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    Object[] args = frame.getArguments();
    assert index < args.length;
    return args[index];
  }

  @Override
  public String toString() {
    return "ReadArgumentNode{" +
        "index=" + index +
        '}';
  }
}
