package abzu.runtime;

import abzu.ast.call.DispatchNode;
import abzu.ast.call.DispatchNodeGen;
import com.oracle.truffle.api.interop.CanResolve;
import com.oracle.truffle.api.interop.MessageResolution;
import com.oracle.truffle.api.interop.Resolve;
import com.oracle.truffle.api.interop.TruffleObject;
import com.oracle.truffle.api.nodes.Node;

import static abzu.runtime.Context.fromForeignValue;

/**
 * The class containing all message resolution implementations of {@link Function}.
 */
@MessageResolution(receiverType = Function.class)
public class FunctionMessageResolution {
  /*
   * An AbzuLanguage function resolves an EXECUTE message.
   */
  @Resolve(message = "EXECUTE")
  public abstract static class AbzuForeignFunctionExecuteNode extends Node {

    @Child
    private DispatchNode dispatch = DispatchNodeGen.create();

    public Object access(Function receiver, Object[] arguments) {
      Object[] arr = new Object[arguments.length];
      // Before the arguments can be used by the Function, they need to be converted to SL
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
      return receiver instanceof Function;
    }
  }

  @CanResolve
  public abstract static class CheckFunction extends Node {

    protected static boolean test(TruffleObject receiver) {
      return receiver instanceof Function;
    }
  }
}
