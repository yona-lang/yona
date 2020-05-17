package yatta.ast.builtin;

import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;
import yatta.runtime.Seq;
import yatta.runtime.exceptions.BadArgException;

@NodeInfo(shortName = "float")
public abstract class ToFloatBuiltin extends BuiltinNode {
  @Specialization
  public double byteVal(byte value) {
    return value;
  }

  @Specialization
  public double longVal(long value) {
    return value;
  }

  @Specialization
  public double doubleVal(double value) {
    return value;
  }

  @Specialization
  public double stringVal(Seq value) {
    try {
      return Double.parseDouble(value.asJavaString(this));
    } catch (NumberFormatException e) {
      throw new BadArgException("Unable to parse " + value.asJavaString(this) + " as a float", this);
    }
  }
}
