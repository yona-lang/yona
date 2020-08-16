package yatta.ast.binary;

import yatta.YattaException;
import yatta.runtime.async.Promise;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;

@NodeInfo(shortName = "%")
public abstract class ModuloNode extends BinaryOpNode {
  @Specialization
  public long chars(int left, Object right) {
    throw YattaException.typeError(this, left, right);
  }

  @Specialization
  public long chars(Object left, int right) {
    throw YattaException.typeError(this, left, right);
  }

  @Specialization
  public long longs(long left, long right) {
    return left % right;
  }

  @Specialization
  public double doubles(double left, double right) {
    return left % right;
  }

  protected Promise promise(Object left, Object right) {
    Promise all = Promise.all(new Object[]{left, right}, this);
    return all.map(args -> {
      Object[] argValues = (Object[]) args;

      if (!argValues[0].getClass().equals(argValues[1].getClass())) {
        return YattaException.typeError(this, argValues);
      }

      if (argValues[0] instanceof Long && argValues[1] instanceof Long) {
        return (long) argValues[0] % (long) argValues[1];
      } else if (argValues[0] instanceof Double) {
        return (double) argValues[0] % (double) argValues[1];
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
