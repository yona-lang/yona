package yatta.ast.binary;

import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;
import yatta.YattaException;
import yatta.runtime.Dictionary;
import yatta.runtime.Seq;
import yatta.runtime.Set;
import yatta.runtime.Unit;
import yatta.runtime.async.Promise;

@NodeInfo(shortName = "in")
public abstract class InNode extends BinaryOpNode {
  @Specialization
  public Promise leftPromise(Promise left, Object right) {
    return promise(left, right);
  }

  @Specialization
  public Promise rightPromise(Object left, Promise right) {
    return promise(left, right);
  }

  @Specialization
  public boolean seq(Object el, Seq seq) {
    return seq.contains(el, this);
  }

  @Specialization
  public boolean set(Object el, Set set) {
    return set.contains(el);
  }

  @Specialization
  public boolean dict(Object el, Dictionary dict) {
    return Unit.INSTANCE != dict.lookup(el);
  }

  protected Promise promise(Object left, Object right) {
    Promise all = Promise.all(new Object[]{left, right}, this);
    return all.map(args -> {
      Object[] argValues = (Object[]) args;

      if (argValues[1] instanceof Seq) {
        return seq(argValues[0], (Seq) argValues[1]);
      } else if (argValues[1] instanceof Set) {
        return set(argValues[0], (Set) argValues[1]);
      } else if (argValues[1] instanceof Dictionary) {
        return dict(argValues[0], (Dictionary) argValues[1]);
      } else {
        return YattaException.typeError(this, argValues);
      }
    }, this);
  }
}
