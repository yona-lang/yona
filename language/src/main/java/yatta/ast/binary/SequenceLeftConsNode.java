package yatta.ast.binary;

import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;
import yatta.YattaException;
import yatta.runtime.Sequence;
import yatta.runtime.async.Promise;

@NodeInfo(shortName = "<:")
public abstract class SequenceLeftConsNode extends BinaryOpNode {
  @Specialization
  public Sequence sequences(Object left, Sequence right) {
    return right.push(left);
  }

  protected Promise promise(Object left, Object right) {
    Promise all = Promise.all(new Object[]{left, right}, this);
    return all.map(args -> {
      Object[] argValues = (Object[]) args;

      if (argValues[1] instanceof Sequence) {
        return ((Sequence) argValues[1]).push(argValues[0]);
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
