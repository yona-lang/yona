package yatta.ast.local;

import yatta.ast.ExpressionNode;
import yatta.runtime.Unit;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.profiles.BranchProfile;

/**
 * Reads a function argument. Arguments are passed in as an object array.
 * <p>
 * Arguments are not type-specialized. To ensure that repeated accesses within a method are
 * specialized and can, e.g., be accessed without unboxing, all arguments are loaded into local
 * variables.
 */
public class ReadArgumentNode extends ExpressionNode {

  /** The argument number, i.e., the index into the array of arguments. */
  private final int index;

  /**
   * Profiling information, collected by the interpreter, capturing whether the function was
   * called with fewer actual arguments than formal arguments.
   */
  private final BranchProfile outOfBoundsTaken = BranchProfile.create();

  public ReadArgumentNode(int index) {
    this.index = index;
  }

  @Override
  public Object executeGeneric(VirtualFrame frame) {
    Object[] args = frame.getArguments();
    if (index < args.length) {
      return args[index];
    } else {
      /* In the interpreter, record profiling information that the branch was used. */
      outOfBoundsTaken.enter();
      /* Use the default null value. */
      return Unit.INSTANCE;
    }
  }

  @Override
  public String toString() {
    return "ReadArgumentNode{" +
        "index=" + index +
        '}';
  }
}
