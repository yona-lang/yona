package abzu.ast.builtin;

import abzu.runtime.Context;
import abzu.runtime.async.Promise;
import abzu.runtime.Unit;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;

@NodeInfo(shortName = "sleep")
public abstract class SleepNode extends BuiltinNode {
  @Specialization
  public Object sleep(long millis) {
    Promise promise = new Promise();
    Context.getCurrent().getExecutor().submit(() -> {
      try {
        Thread.sleep(millis);
      } catch (InterruptedException e) {
        promise.fulfil(Unit.INSTANCE);
      }
      promise.fulfil(Unit.INSTANCE);
    });
    return promise;
  }
}
