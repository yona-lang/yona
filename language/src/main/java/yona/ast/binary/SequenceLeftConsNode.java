package yona.ast.binary;

import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.YonaException;
import yona.runtime.Seq;
import yona.runtime.async.Promise;

@NodeInfo(shortName = "-|")
public abstract class SequenceLeftConsNode extends BinaryOpNode {
  @Specialization
  public Promise leftPromise(Promise left, Object right) {
    return promise(left, right);
  }

  @Specialization
  public Promise rightPromise(Object left, Promise right) {
    return promise(left, right);
  }

  @Specialization
  public Seq sequences(Object left, Seq right) {
    return right.insertFirst(left);
  }

  protected Promise promise(Object left, Object right) {
    Promise all = Promise.all(new Object[]{left, right}, this);
    return all.map(args -> {
      Object[] argValues = (Object[]) args;

      if (argValues[1] instanceof Seq) {
        return ((Seq) argValues[1]).insertFirst(argValues[0]);
      } else {
        return YonaException.typeError(this, argValues);
      }
    }, this);
  }
}
