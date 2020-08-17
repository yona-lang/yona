package yona.ast.local;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.YonaException;
import yona.ast.ExpressionNode;

import java.util.Arrays;

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
    if (index >= args.length) {
      // This should be ultimately an assert check only: https://github.com/yona-lang/yona/issues/37
      CompilerDirectives.transferToInterpreterAndInvalidate();
      throw new YonaException("Unable to read argument number " + index + "(zero-indexed) from args: " + Arrays.toString(args) + ". This is most likely because the function is being called with fewer arguments than how many are defined.", this);
    }
    return args[index];
  }

  @Override
  protected String[] requiredIdentifiers() {
    return new String[0];
  }

  @Override
  public String toString() {
    return "ReadArgumentNode{" +
        "index=" + index +
        '}';
  }
}
