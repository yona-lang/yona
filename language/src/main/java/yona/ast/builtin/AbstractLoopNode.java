package yona.ast.builtin;

import com.oracle.truffle.api.interop.ArityException;
import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.interop.UnsupportedMessageException;
import com.oracle.truffle.api.interop.UnsupportedTypeException;
import yona.YonaException;
import yona.runtime.Function;
import yona.runtime.async.Promise;
import yona.runtime.exceptions.RuntimeException;
import yona.runtime.exceptions.UndefinedNameException;

public abstract class AbstractLoopNode extends BuiltinNode {
  protected void runCallback(Function function, InteropLibrary dispatch) {
    try {
      Object result = dispatch.execute(function);
      if (result instanceof Promise) {
        Promise promise = (Promise) result;
        if (!promise.isFulfilled()) {
          Promise.await(promise);
        }
      }
    } catch (ArityException | UnsupportedTypeException | UnsupportedMessageException e) {
      throw UndefinedNameException.undefinedFunction(this, function);
    } catch (YonaException yonaException) {
      throw yonaException;
    } catch (Throwable throwable) {
      throw new RuntimeException(throwable, this);
    }
  }
}
