package yona.ast.builtin;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.runtime.Seq;
import yona.runtime.exceptions.BadArgException;

@NodeInfo(shortName = "int")
public abstract class ToIntegerBuiltin extends BuiltinNode {
  @Specialization
  public long byteVal(byte value) {
    return value;
  }

  @Specialization
  public long longVal(long value) {
    return value;
  }

  @Specialization
  public long doubleVal(double value) {
    return (long) value;
  }

  @Specialization
  @CompilerDirectives.TruffleBoundary
  public long stringVal(Seq value) {
    try {
      return Long.parseLong(value.asJavaString(this));
    } catch (NumberFormatException e) {
      throw new BadArgException("Unable to parse " + value.asJavaString(this) + " as an integer", this);
    }
  }
}
