package yatta.runtime;

import com.oracle.truffle.api.interop.CanResolve;
import com.oracle.truffle.api.interop.MessageResolution;
import com.oracle.truffle.api.interop.Resolve;
import com.oracle.truffle.api.interop.TruffleObject;
import com.oracle.truffle.api.nodes.Node;

/**
 * The class containing all message resolution implementations of {@link Unit}.
 */
@MessageResolution(receiverType = Unit.class)
public class IsNoneMessageResolution {
  /*
   * An Yatta function resolves the IS_NONE message.
   */
  @Resolve(message = "IS_NONE")
  public abstract static class YattaForeignIsNoneNode extends Node {

    public Object access(Object receiver) {
      return Unit.INSTANCE == receiver;
    }
  }

  @CanResolve
  public abstract static class CheckNone extends Node {

    protected static boolean test(TruffleObject receiver) {
      return receiver instanceof Unit;
    }
  }
}
