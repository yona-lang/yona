package yona.ast.binary;

import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.YonaException;
import yona.runtime.Dict;
import yona.runtime.Function;
import yona.runtime.Set;
import yona.runtime.Unit;
import yona.runtime.async.Promise;

@NodeInfo(shortName = ">")
public abstract class GreaterThanNode extends BinaryOpNode {
  @Specialization
  public long chars(int left, Object right) {
    throw YonaException.typeError(this, left, right);
  }

  @Specialization
  public long chars(Object left, int right) {
    throw YonaException.typeError(this, left, right);
  }

  @Specialization
  public boolean longs(long left, long right) {
    return left > right;
  }

  @Specialization
  public boolean doubles(double left, double right) {
    return left > right;
  }

  @Specialization
  public boolean bytes(byte left, byte right) {
    return left > right;
  }

  @Specialization
  public boolean functions(Function left, Function right) {
    return false;
  }

  @Specialization
  public boolean units(Unit left, Unit right) {
    return false;
  }

  @Specialization
  public boolean sets(Set left, Set right) {
    return left.compareTo(right) > 0;
  }

  @Specialization
  public boolean dicts(Dict left, Dict right) {
    return left.compareTo(right) > 0;
  }

  protected Promise promise(Object left, Object right) {
    Promise all = Promise.all(new Object[]{left, right}, this);
    return all.map(args -> {
      Object[] argValues = (Object[]) args;

      if (!argValues[0].getClass().equals(argValues[1].getClass())) {
        return YonaException.typeError(this, argValues);
      }

      if (argValues[0] instanceof Long && argValues[1] instanceof Long) {
        return (long) argValues[0] > (long) argValues[1];
      } else if (argValues[0] instanceof Double && argValues[1] instanceof Double) {
        return (double) argValues[0] > (double) argValues[1];
      } else if (argValues[0] instanceof Byte && argValues[1] instanceof Byte) {
        return (byte) argValues[0] > (byte) argValues[1];
      } else if (argValues[0] instanceof Set && argValues[1] instanceof Set) {
        return sets((Set) argValues[0], (Set) argValues[1]);
      } else if (argValues[0] instanceof Dict && argValues[1] instanceof Dict) {
        return dicts((Dict) argValues[0], (Dict) argValues[1]);
      } else {
        return YonaException.typeError(this, argValues);
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
