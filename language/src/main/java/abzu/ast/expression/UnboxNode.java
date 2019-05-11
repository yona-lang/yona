package abzu.ast.expression;

import abzu.Types;
import abzu.ast.ExpressionNode;
import abzu.runtime.*;
import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.dsl.NodeChild;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.dsl.TypeSystemReference;
import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.interop.UnsupportedMessageException;
import com.oracle.truffle.api.library.CachedLibrary;

@TypeSystemReference(Types.class)
@NodeChild
public abstract class UnboxNode extends ExpressionNode {

  static final int LIMIT = 5;

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

  @Specialization(limit = "LIMIT")
  public static Object fromForeign(Object value, @CachedLibrary("value") InteropLibrary interop) {
    try {
      if (interop.fitsInLong(value)) {
        return interop.asLong(value);
      } else if (interop.fitsInDouble(value)) {
        return (long) interop.asDouble(value);
      } else if (interop.isString(value)) {
        return interop.asString(value);
      } else if (interop.isBoolean(value)) {
        return interop.asBoolean(value);
      } else {
        return value;
      }
    } catch (UnsupportedMessageException e) {
      CompilerDirectives.transferToInterpreter();
      throw new AssertionError();
    }
  }

}
