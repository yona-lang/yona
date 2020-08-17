package yona.ast.binary;

import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.YonaException;
import yona.runtime.*;
import yona.runtime.async.Promise;

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
  public boolean dict(Object el, Dict dict) {
    return dict.contains(el);
  }

  protected Promise promise(Object left, Object right) {
    Promise all = Promise.all(new Object[]{left, right}, this);
    return all.map(args -> {
      Object[] argValues = (Object[]) args;

      if (argValues[1] instanceof Seq) {
        return seq(argValues[0], (Seq) argValues[1]);
      } else if (argValues[1] instanceof Set) {
        return set(argValues[0], (Set) argValues[1]);
      } else if (argValues[1] instanceof Dict) {
        return dict(argValues[0], (Dict) argValues[1]);
      } else {
        return YonaException.typeError(this, argValues);
      }
    }, this);
  }
}
