package yatta.ast.builtin;

import yatta.YattaLanguage;
import yatta.runtime.Context;
import yatta.runtime.Function;
import yatta.runtime.UndefinedNameException;
import yatta.runtime.async.Promise;
import com.oracle.truffle.api.dsl.CachedContext;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.interop.ArityException;
import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.interop.UnsupportedMessageException;
import com.oracle.truffle.api.interop.UnsupportedTypeException;
import com.oracle.truffle.api.library.CachedLibrary;
import com.oracle.truffle.api.nodes.NodeInfo;

@NodeInfo(shortName = "async")
public abstract class AsyncNode extends BuiltinNode {
  @Specialization
  public Promise async(Function function, @CachedContext(YattaLanguage.class) Context context, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
    Promise promise = new Promise();
    context.getExecutor().submit(() -> {
      try {
        promise.fulfil(dispatch.execute(function), this);
      } catch (ArityException | UnsupportedTypeException | UnsupportedMessageException e) {
        /* Execute was not successful. */
        promise.fulfil(UndefinedNameException.undefinedFunction(this, function), this);
      } catch (Throwable e) {
        promise.fulfil(e, this);
      }
    });

    return promise;
  }
}
