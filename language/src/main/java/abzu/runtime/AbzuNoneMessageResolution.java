package abzu.runtime;

import com.oracle.truffle.api.interop.CanResolve;
import com.oracle.truffle.api.interop.MessageResolution;
import com.oracle.truffle.api.interop.Resolve;
import com.oracle.truffle.api.interop.TruffleObject;
import com.oracle.truffle.api.nodes.Node;

/**
 * The class containing all message resolution implementations of {@link AbzuUnit}.
 */
@MessageResolution(receiverType = AbzuUnit.class)
public class AbzuNoneMessageResolution {
  /*
   * An SL function resolves the IS_NONE message.
   */
  @Resolve(message = "IS_NONE")
  public abstract static class AbzuForeignIsNoneNode extends Node {

    public Object access(Object receiver) {
      return AbzuUnit.INSTANCE == receiver;
    }
  }

  @CanResolve
  public abstract static class CheckNone extends Node {

    protected static boolean test(TruffleObject receiver) {
      return receiver instanceof AbzuUnit;
    }
  }
}
