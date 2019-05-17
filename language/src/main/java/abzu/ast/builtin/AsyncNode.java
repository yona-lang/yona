package abzu.ast.builtin;

import abzu.AbzuLanguage;
import abzu.runtime.Context;
import abzu.runtime.Function;
import abzu.runtime.UndefinedNameException;
import abzu.runtime.async.Promise;
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
  public Promise async(Function function, @CachedContext(AbzuLanguage.class) Context context, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
    Promise promise = new Promise();
    context.getExecutor().submit(() -> {
      try {
        promise.fulfil(dispatch.execute(function, new Object[] {}), this);
      } catch (ArityException | UnsupportedTypeException | UnsupportedMessageException e) {
        /* Execute was not successful. */
        promise.fulfil(UndefinedNameException.undefinedFunction(this, function), this);
      }
    });

    return promise;
  }
}
