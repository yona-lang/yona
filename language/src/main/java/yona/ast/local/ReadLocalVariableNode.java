package yona.ast.local;

import com.oracle.truffle.api.dsl.NodeField;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.frame.FrameSlot;
import com.oracle.truffle.api.frame.FrameUtil;
import com.oracle.truffle.api.frame.VirtualFrame;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.ast.ExpressionNode;

/**
 * Node to read a local variable from a function's {@link VirtualFrame frame}. The Truffle frame API
 * allows to store primitive values of all Java primitive types, and Object values. This means that
 * all Yona types that are objects are handled by the {@link #readObject} method.
 * <p>
 * We use the primitive type only when the same primitive type is uses for all writes. If the local
 * variable is type-polymorphic, then the value is always stored as an Object, i.e., primitive
 * values are boxed. Even a mixture of {@code long} and {@code boolean} writes leads to both being
 * stored boxed.
 */
@NodeInfo(shortName = "readLocalVariable")
@NodeField(name = "slot", type = FrameSlot.class)
public abstract class ReadLocalVariableNode extends ExpressionNode {

  /**
   * Returns the descriptor of the accessed local variable. The implementation of this method is
   * created by the Truffle DSL based on the {@link NodeField} annotation on the class.
   */
  public abstract FrameSlot getSlot();

  @Specialization(guards = "frame.isLong(getSlot())")
  protected long readLong(VirtualFrame frame) {
    return FrameUtil.getLongSafe(frame, getSlot());
  }

  @Specialization(guards = "frame.isBoolean(getSlot())")
  protected boolean readBoolean(VirtualFrame frame) {
    return FrameUtil.getBooleanSafe(frame, getSlot());
  }

  @Specialization(guards = "frame.isByte(getSlot())")
  protected byte readByte(VirtualFrame frame) {
    return FrameUtil.getByteSafe(frame, getSlot());
  }

  @Specialization(replaces = {"readLong", "readBoolean", "readByte"})
  protected Object readObject(VirtualFrame frame) {
    /*
     * The FrameSlotKind has been set to Object, so from now on all writes to the local
     * variable will be Object writes. However, now we are in a frame that still has an old
     * non-Object value. This is a slow-path operation: we read the non-Object value, and
     * write it immediately as an Object value so that we do not hit this path again
     * multiple times for the same variable of the same frame.
     */
    Object result = frame.getValue(getSlot());
    frame.setObject(getSlot(), result);
    return result;
  }

  @Override
  protected String[] requiredIdentifiers() {
    return new String[0];
  }
}
