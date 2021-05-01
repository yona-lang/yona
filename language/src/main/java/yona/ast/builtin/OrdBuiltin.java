package yona.ast.builtin;

import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.YonaException;

@NodeInfo(shortName = "ord")
public abstract class OrdBuiltin extends BuiltinNode {
  @Specialization
  public byte charVal(int value) {
    if (value > 127) {
      throw YonaException.typeError(this, value);
    }
    return (byte) value;
  }
}
