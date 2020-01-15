package yatta.ast.builtin;

import com.oracle.truffle.api.dsl.CachedContext;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;
import yatta.YattaLanguage;
import yatta.runtime.Context;
import yatta.runtime.Unit;
import yatta.runtime.async.Promise;

@NodeInfo(shortName = "sleep")
public abstract class SleepBuiltin extends BuiltinNode {
  @Specialization
  public Promise sleep(long millis, @CachedContext(YattaLanguage.class) Context context) {
    Promise promise = new Promise();
    // TODO
    try {
      Thread.sleep(millis);
    } catch (InterruptedException e) {
      e.printStackTrace();
    }
    promise.fulfil(Unit.INSTANCE, null);
    return promise;
  }
}
