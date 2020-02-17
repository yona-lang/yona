package yatta.ast.binary;

import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;
import yatta.YattaException;
import yatta.runtime.Seq;
import yatta.runtime.async.Promise;

@NodeInfo(shortName = "|-")
public abstract class SequenceRightConsNode extends BinaryOpNode {
  @Specialization
  public Promise leftPromise(Promise left, Object right) {
    return promise(left, right);
  }

  @Specialization
  public Promise rightPromise(Object left, Promise right) {
    return promise(left, right);
  }

  @Specialization
  public Seq sequences(Seq left, Object right) {
    return left.insertLast(right);
  }

  protected Promise promise(Object left, Object right) {
    Promise all = Promise.all(new Object[]{left, right}, this);
    return all.map(args -> {
      Object[] argValues = (Object[]) args;

      if (argValues[0] instanceof Seq) {
        return ((Seq) argValues[0]).insertLast(argValues[1]);
      } else {
        return YattaException.typeError(this, argValues);
      }
    }, this);
  }
}
