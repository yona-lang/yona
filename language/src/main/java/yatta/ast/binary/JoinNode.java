package yatta.ast.binary;

import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;
import yatta.YattaException;
import yatta.runtime.Dict;
import yatta.runtime.Seq;
import yatta.runtime.Set;
import yatta.runtime.Tuple;
import yatta.runtime.async.Promise;

import java.util.Arrays;

@NodeInfo(shortName = "++")
public abstract class JoinNode extends BinaryOpNode {
  @Specialization
  public Seq sequences(Seq left, Seq right) {
    return Seq.catenate(left, right);
  }

  @Specialization
  public Dict dict(Dict dict, Tuple tuple) {
    if (2 != tuple.length()) {
      throw YattaException.typeError(this, tuple.toArray());
    }
    return dict.add(tuple.get(0), tuple.get(1));
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
      } else if (argValues[0] instanceof Set) {
        return set((Set) argValues[0], argValues[1]);
      } else if (argValues[0] instanceof Dict && argValues[1] instanceof Tuple) {
        return dict((Dict) argValues[0], (Tuple) argValues[1]);
      } else {
        return YattaException.typeError(this, argValues);
      }
    }, this);
  }

  @Specialization
  public Set set(Set left, Object right) {
    return left.add(right);
  }
}
