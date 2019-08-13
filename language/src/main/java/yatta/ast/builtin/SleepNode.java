package yatta.ast.builtin;

import com.oracle.truffle.api.dsl.CachedContext;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;
import yatta.YattaLanguage;
import yatta.runtime.Context;
import yatta.runtime.Unit;
import yatta.runtime.async.Promise;

@NodeInfo(shortName = "sleep")
public abstract class SleepNode extends BuiltinNode {
  @Specialization
  public Promise sleep(long millis, @CachedContext(YattaLanguage.class) Context context) {
    Promise promise = new Promise();
    context.getThreading().submit(() -> {
      try {
        Thread.sleep(millis);
      } catch (InterruptedException ignored) { }
      promise.fulfil(Unit.INSTANCE, this);
    });
    return promise;
  }
}
