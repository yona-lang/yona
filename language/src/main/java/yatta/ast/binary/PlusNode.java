package yatta.ast.binary;

import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;
import yatta.YattaException;
import yatta.runtime.Dict;
import yatta.runtime.Set;
import yatta.runtime.Tuple;
import yatta.runtime.async.Promise;

@NodeInfo(shortName = "+")
public abstract class PlusNode extends BinaryOpNode {
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
    return left + right;
  }

  @Specialization
  public double doubles(double left, double right) {
    return left + right;
  }

  @Specialization
  public Dict dict(Dict dict, Tuple tuple) {
    if (2 != tuple.length()) {
      throw YattaException.typeError(this, tuple);
    }
    return dict.add(tuple.get(0), tuple.get(1));
  }

  protected Promise promise(Object left, Object right) {
    Promise all = Promise.all(new Object[]{left, right}, this);
    return all.map(args -> {
      Object[] argValues = (Object[]) args;

      if (argValues[0] instanceof Long && argValues[1] instanceof Long) {
        return (long) argValues[0] + (long) argValues[1];
      } else if (argValues[0] instanceof Double && argValues[1] instanceof Double) {
        return (double) argValues[0] + (double) argValues[1];
      } else if (argValues[0] instanceof Dict && argValues[1] instanceof Tuple) {
        return dict((Dict) argValues[0], (Tuple) argValues[1]);
      } else if (argValues[0] instanceof Set) {
        return set((Set) argValues[0], argValues[1]);
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

  @Specialization
  public Set set(Set set, Object el) {
    return set.add(el);
  }
}
