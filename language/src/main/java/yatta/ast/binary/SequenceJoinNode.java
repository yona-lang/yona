package yatta.ast.binary;

import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;
import yatta.YattaException;
import yatta.runtime.Seq;
import yatta.runtime.async.Promise;

@NodeInfo(shortName = "++")
public abstract class SequenceJoinNode extends BinaryOpNode {
  @Specialization
  public Seq sequences(Seq left, Seq right) {
    return Seq.catenate(left, right);
  }

  protected Promise promise(Object left, Object right) {
    Promise all = Promise.all(new Object[]{left, right}, this);
    return all.map(args -> {
      Object[] argValues = (Object[]) args;

      if (!argValues[0].getClass().equals(argValues[1].getClass())) {
        return YattaException.typeError(this, argValues);
      }

      if (argValues[0] instanceof Seq) {
        return Seq.catenate((Seq) argValues[0], (Seq) argValues[1]);
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
