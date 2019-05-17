package abzu.ast.binary;

import abzu.AbzuException;
import abzu.runtime.Dictionary;
import abzu.runtime.Sequence;
import abzu.runtime.async.Promise;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;

@NodeInfo
public abstract class PlusNode extends BinaryOpNode {
  @Specialization
  public long longs(long left, long right) {
    return left + right;
  }

  @Specialization
  public double doubles(double left, double right) {
    return left + right;
  }

  @Specialization
  public String strings(String left, String right) {
    return left + right;
  }

  @Specialization
  public Sequence sequences(Sequence left, Sequence right) {
    // TODO implement
    return null;
  }

  @Specialization
  public Dictionary dictionaries(Dictionary left, Dictionary right) {
    // TODO implement
    return null;
  }

  protected Promise promise(Object left, Object right) {
    Promise all = Promise.all(new Object[]{left, right}, this);
    return all.map(args -> {
      Object[] argValues = (Object[]) args;

      if (!argValues[0].getClass().equals(argValues[1].getClass())) {
        return AbzuException.typeError(this, argValues);
      }

      if (argValues[0] instanceof Long) {
        return (long) argValues[0] + (long) argValues[1];
      } else if (argValues[0] instanceof Double) {
        return (double) argValues[0] + (double) argValues[1];
        // TODO implement
      } else if (argValues[0] instanceof String) {
        return (String) argValues[0] + (String) argValues[1];
//      } else if (argValues[0] instanceof Sequence) {
//        return null;
//      } else if (argValues[0] instanceof Dictionary) {
//        return null;
      } else {
        return AbzuException.typeError(this, argValues);
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
