package yatta.ast.builtin;

import com.oracle.truffle.api.dsl.CachedContext;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.library.CachedLibrary;
import com.oracle.truffle.api.nodes.NodeInfo;
import yatta.YattaLanguage;
import yatta.runtime.Context;
import yatta.runtime.async.Promise;

import java.io.IOException;

@NodeInfo(shortName = "read")
public abstract class ReadBuiltin extends BuiltinNode {
  @Specialization
  public Promise read(@CachedContext(YattaLanguage.class) Context context, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
    Promise promise = new Promise(dispatch);
    context.ioExecutor.submit(() -> {
      try {
        promise.fulfil(context.getInput().read(), this);
      } catch (IOException e) {
        promise.fulfil(new yatta.runtime.exceptions.IOException(e, this), this);
      }
    });
    return promise;
  }
}
