package abzu.runtime;

import com.oracle.truffle.api.interop.CanResolve;
import com.oracle.truffle.api.interop.MessageResolution;
import com.oracle.truffle.api.interop.Resolve;
import com.oracle.truffle.api.interop.TruffleObject;
import com.oracle.truffle.api.nodes.Node;
import abzu.ast.call.AbzuDispatchNode;
import abzu.ast.call.AbzuDispatchNodeGen;

import static abzu.runtime.AbzuContext.fromForeignValue;

/**
 * The class containing all message resolution implementations of {@link AbzuFunction}.
 */
@MessageResolution(receiverType = AbzuFunction.class)
public class AbzuFunctionMessageResolution {
  /*
   * An Abzu function resolves an EXECUTE message.
   */
  @Resolve(message = "EXECUTE")
  public abstract static class AbzuForeignFunctionExecuteNode extends Node {

    @Child
    private AbzuDispatchNode dispatch = AbzuDispatchNodeGen.create();

    public Object access(AbzuFunction receiver, Object[] arguments) {
      Object[] arr = new Object[arguments.length];
      // Before the arguments can be used by the AbzuFunction, they need to be converted to SL
      // values.
      for (int i = 0; i < arr.length; i++) {
        arr[i] = fromForeignValue(arguments[i]);
      }
      Object result = dispatch.executeDispatch(receiver, arr);
      return result;
    }
  }

  /*
   * An SL function should respond to an IS_EXECUTABLE message with true.
   */
  @Resolve(message = "IS_EXECUTABLE")
  public abstract static class SLForeignIsExecutableNode extends Node {
    public Object access(Object receiver) {
      return receiver instanceof AbzuFunction;
    }
  }

  @CanResolve
  public abstract static class CheckFunction extends Node {

    protected static boolean test(TruffleObject receiver) {
      return receiver instanceof AbzuFunction;
    }
  }
}
