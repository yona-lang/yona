package yatta.ast.binary;

import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;
import yatta.YattaException;
import yatta.runtime.Dict;
import yatta.runtime.Seq;
import yatta.runtime.Set;
import yatta.runtime.async.Promise;

@NodeInfo(shortName = "--")
public abstract class DifferenceNode extends BinaryOpNode {
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

      if (argValues[0] instanceof Set) {
        return set((Set) argValues[0], argValues[1]);
      } else if (argValues[0] instanceof Dict) {
        return dict((Dict) argValues[0], argValues[1]);
      } else {
        return YattaException.typeError(this, argValues);
      }
    }, this);
  }

  @Specialization
  public Set set(Set left, Object right) {
    return left.remove(right);
  }

  @Specialization
  public Dict dict(Dict left, Object right) {
    return left.remove(right);
  }
}
