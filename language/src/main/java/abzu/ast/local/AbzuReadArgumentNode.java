package abzu.ast.local;

import abzu.runtime.AbzuUnit;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.profiles.BranchProfile;
import abzu.ast.AbzuExpressionNode;

/**
 * Reads a function argument. Arguments are passed in as an object array.
 * <p>
 * Arguments are not type-specialized. To ensure that repeated accesses within a method are
 * specialized and can, e.g., be accessed without unboxing, all arguments are loaded into local
 * variables {@link AbzuNodeFactory#addFormalParameter in the method prologue}.
 */
public class AbzuReadArgumentNode extends AbzuExpressionNode {

  /** The argument number, i.e., the index into the array of arguments. */
  private final int index;

  /**
   * Profiling information, collected by the interpreter, capturing whether the function was
   * called with fewer actual arguments than formal arguments.
   */
  private final BranchProfile outOfBoundsTaken = BranchProfile.create();

  public AbzuReadArgumentNode(int index) {
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
      return AbzuUnit.INSTANCE;
    }
  }
}
