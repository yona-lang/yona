package yona.ast.binary;

import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.YonaException;
import yona.runtime.Seq;
import yona.runtime.async.Promise;

@NodeInfo(shortName = "++")
public abstract class JoinNode extends BinaryOpNode {
  @Specialization
  public Seq sequences(Seq left, Seq right) {
    return Seq.catenate(left, right);
  }

  @Specialization
  public Promise leftPromise(Promise left, Object right) {
    return promise(left, right);
  }

  @Specialization
  public Promise rightPromise(Object left, Promise right) {
    return promise(left, right);
  }

  protected Promise promise(Object left, Object right) {
    Promise all = Promise.all(new Object[]{left, right}, this);
    return all.map(args -> {
      Object[] argValues = (Object[]) args;

      if (argValues[0] instanceof Seq && argValues[1] instanceof Seq) {
        return Seq.catenate((Seq) argValues[0], (Seq) argValues[1]);
      } else {
        return YonaException.typeError(this, argValues);
      }
    }, this);
  }
}
