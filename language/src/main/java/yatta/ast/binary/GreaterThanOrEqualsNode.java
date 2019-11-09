package yatta.ast.binary;

import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;
import yatta.YattaException;
import yatta.runtime.Function;
import yatta.runtime.Unit;
import yatta.runtime.async.Promise;

@NodeInfo(shortName = ">=")
public abstract class GreaterThanOrEqualsNode extends BinaryOpNode {
  @Specialization
  public boolean longs(long left, long right) {
    return left >= right;
  }

  @Specialization
  public boolean doubles(double left, double right) {
    return left >= right;
  }

  @Specialization
  public boolean bytes(byte left, byte right) {
    return left >= right;
  }

  @Specialization
  public boolean functions(Function left, Function right) {
    return left == right;
  }

  @Specialization
  public boolean units(Unit left, Unit right) {
    return true;
  }

  protected Promise promise(Object left, Object right) {
    Promise all = Promise.all(new Object[]{left, right}, this);
    return all.map(args -> {
      Object[] argValues = (Object[]) args;

      if (!argValues[0].getClass().equals(argValues[1].getClass())) {
        return YattaException.typeError(this, argValues);
      }

      if (argValues[0] instanceof Long) {
        return (long) argValues[0] >= (long) argValues[1];
      } else if (argValues[0] instanceof Double) {
        return (double) argValues[0] >= (double) argValues[1];
      } else if (argValues[0] instanceof Byte) {
        return (byte) argValues[0] >= (byte) argValues[1];
        // TODO implement
//      } else if (argValues[0] instanceof Dictionary) {
//        return null;
      } else {
        return YattaException.typeError(this, argValues);
      }
    }, this);
  }

  @Specialization
  public Promise leftPromise(Promise left, Object right) {
    return promise(left, right);
  }

  @Specialization
  public Promise rightPromise(Object left, Promise right) {
    return promise(left, right);
  }
}
