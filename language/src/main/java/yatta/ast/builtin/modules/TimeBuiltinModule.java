package yatta.ast.builtin.modules;

import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;
import yatta.YattaException;
import yatta.ast.builtin.BuiltinNode;
import yatta.runtime.async.Promise;
import yatta.runtime.exceptions.BadArgException;
import yatta.runtime.exceptions.TimeoutException;
import yatta.runtime.stdlib.Builtins;
import yatta.runtime.stdlib.ExportedFunction;

@BuiltinModuleInfo(moduleName = "Time")
public final class TimeBuiltinModule implements BuiltinModule {
  @NodeInfo(shortName = "now")
  abstract static class NowBuiltin extends BuiltinNode {
    @Specialization
    public long now() {
      return System.currentTimeMillis();
    }
  }

  @NodeInfo(shortName = "timeout")
  abstract static class TimeoutBuiltin extends BuiltinNode {
    @Specialization
    public Object timeout(Object value, long millis) {
      if (millis < 0) {
        throw new BadArgException("millis value should be >= 0", this);
      }
      if (!(value instanceof Promise)) {
        return value;
      }
      final Promise promise = (Promise) value;
      if (promise.isFulfilled()) {
        final Object result = promise.unwrap();
        if (result instanceof Error) {
          throw (Error) result;
        } else if (result instanceof RuntimeException) {
          throw (RuntimeException) result;
        } else if (result instanceof Exception) {
          throw new YattaException((Exception) result, TimeoutBuiltin.this);
        } else {
          return result;
        }
      }
      final Promise result = new Promise();
      new Thread(() -> {
        try {
          if (Promise.timeout(promise, millis)) {
            result.fulfil(promise.unwrap(), TimeoutBuiltin.this);
          } else {
            result.fulfil(new TimeoutException(TimeoutBuiltin.this), TimeoutBuiltin.this);
          }

        } catch (Throwable throwable) {
          result.fulfil(throwable, TimeoutBuiltin.this);
        }
      }).start();
      return result;
    }
  }

  public Builtins builtins() {
    Builtins builtins = new Builtins();
    builtins.register(new ExportedFunction(TimeBuiltinModuleFactory.NowBuiltinFactory.getInstance()));
    builtins.register(new ExportedFunction(TimeBuiltinModuleFactory.TimeoutBuiltinFactory.getInstance()));
    return builtins;
  }
}
