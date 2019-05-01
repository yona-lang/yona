package abzu.ast.builtin;

import abzu.runtime.Context;
import abzu.runtime.Unit;
import abzu.runtime.async.Promise;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;

@NodeInfo(shortName = "sleep")
public abstract class SleepNode extends BuiltinNode {
  @Specialization
  public Promise sleep(long millis) {
    Promise promise = new Promise();
    Context.getCurrent().getExecutor().submit(() -> {
      try {
        Thread.sleep(millis);
      } catch (InterruptedException ignored) { }
      promise.fulfil(Unit.INSTANCE);
    });
    return promise;
  }
}
