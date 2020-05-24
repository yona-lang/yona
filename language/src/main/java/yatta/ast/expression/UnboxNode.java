package yatta.ast.expression;

import com.oracle.truffle.api.nodes.NodeInfo;
import yatta.Types;
import yatta.ast.ExpressionNode;
import yatta.runtime.*;
import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.dsl.NodeChild;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.dsl.TypeSystemReference;
import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.interop.UnsupportedMessageException;
import com.oracle.truffle.api.library.CachedLibrary;

@TypeSystemReference(Types.class)
@NodeChild
@NodeInfo(shortName = "unbox")
public abstract class UnboxNode extends ExpressionNode {
  static final int LIMIT = 5;

  @Override
  public String toString() {
    return "UnboxNode{}";
  }

  @Specialization
  protected static long fromLong(long value) {
    return value;
  }

  @Specialization
  protected static double fromDouble(double value) {
    return value;
  }

  @Specialization
  protected static Unit fromUnit(Unit value) {
    return value;
  }

  @Specialization
  protected static Set fromSet(Set value) {
    return value;
  }

  @Specialization
  protected static Seq fromSeq(Seq value) {
    return value;
  }

  @Specialization
  protected static Dict fromDictionary(Dict value) {
    return value;
  }

  @Specialization
  protected static Tuple fromTuple(Tuple value) {
    return value;
  }

  @Specialization(limit = "LIMIT")
  public static Object fromForeign(Object value, @CachedLibrary("value") InteropLibrary interop) {
    try {
      if (interop.fitsInLong(value)) {
        return interop.asLong(value);
      } else if (interop.fitsInDouble(value)) {
        return interop.asDouble(value);
      } else if (interop.isString(value)) {
        return interop.asString(value);
      } else if (interop.isBoolean(value)) {
        return interop.asBoolean(value);
      } else {
        return value;
      }
    } catch (UnsupportedMessageException e) {
      CompilerDirectives.transferToInterpreterAndInvalidate();
      throw new AssertionError();
    }
  }

  @Override
  protected String[] requiredIdentifiers() {
    return value.getRequiredIdentifiers();
  }

  private ExpressionNode value;

  public UnboxNode setValue(ExpressionNode value) {
    this.value = value;
    return this;
  }
}
