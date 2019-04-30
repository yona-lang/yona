package abzu.ast.builtin;

import abzu.runtime.Context;
import abzu.runtime.Unit;
import abzu.runtime.async.AbzuFuture;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;

import java.util.concurrent.CompletableFuture;

@NodeInfo(shortName = "sleep")
public abstract class SleepNode extends BuiltinNode {
  @Specialization
  public AbzuFuture sleep(long millis) {
//    Promise promise = new Promise("sleep_" + millis);
    AbzuFuture future = new AbzuFuture();
    Context.getCurrent().getExecutor().submit(() -> {
      try {
        Thread.sleep(millis);
      } catch (InterruptedException ignored) { }
//      promise.fulfil(Unit.INSTANCE);
      future.completableFuture.complete(Unit.INSTANCE);
    });
//    return promise;
    return future;
  }
}
