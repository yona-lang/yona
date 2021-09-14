package yona.ast.builtin;

import com.oracle.truffle.api.interop.InteropLibrary;
import yona.YonaException;
import yona.ast.call.InvokeNode;
import yona.runtime.Function;
import yona.runtime.async.Promise;
import yona.runtime.exceptions.RuntimeException;

public abstract class AbstractLoopNode extends BuiltinNode {
  protected void runCallback(Function function, InteropLibrary dispatch) {
    try {
      Object result = InvokeNode.dispatchFunction(function, dispatch, this);
      if (result instanceof Promise promise) {
        if (!promise.isFulfilled()) {
          Promise.await(promise);
        }
      }
    } catch (YonaException yonaException) {
      throw yonaException;
    } catch (Throwable throwable) {
      throw new RuntimeException(throwable, this);
    }
  }
}
