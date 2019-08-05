package yatta.ast.binary;

import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;
import yatta.YattaException;
import yatta.runtime.Sequence;
import yatta.runtime.async.Promise;

@NodeInfo(shortName = ":>")
public abstract class SequenceJoinNode extends BinaryOpNode {
  @Specialization
  public Sequence sequences(Sequence left, Sequence right) {
    return Sequence.catenate(left, right);
  }

  protected Promise promise(Object left, Object right) {
    Promise all = Promise.all(new Object[]{left, right}, this);
    return all.map(args -> {
      Object[] argValues = (Object[]) args;

      if (!argValues[0].getClass().equals(argValues[1].getClass())) {
        return YattaException.typeError(this, argValues);
      }

      if (argValues[0] instanceof Sequence) {
        return Sequence.catenate((Sequence) argValues[0], (Sequence) argValues[1]);
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
