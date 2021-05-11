package yona.ast.builtin.modules;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.dsl.CachedContext;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.interop.ArityException;
import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.interop.UnsupportedMessageException;
import com.oracle.truffle.api.interop.UnsupportedTypeException;
import com.oracle.truffle.api.library.CachedLibrary;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.YonaException;
import yona.YonaLanguage;
import yona.ast.builtin.BuiltinNode;
import yona.runtime.Context;
import yona.runtime.Function;
import yona.runtime.Seq;
import yona.runtime.Tuple;
import yona.runtime.async.Promise;
import yona.runtime.exceptions.BadArgException;
import yona.runtime.exceptions.RuntimeException;
import yona.runtime.exceptions.UndefinedNameException;
import yona.runtime.stdlib.Builtins;
import yona.runtime.stdlib.ExportedFunction;
import yona.runtime.threading.ExecutableFunction;

@BuiltinModuleInfo(moduleName = "Stopwatch")
public final class StopWatchBuiltinModule implements BuiltinModule {
  @NodeInfo(shortName = "nanos")
  abstract static class NanosBuiltin extends BuiltinNode {
    @Specialization
    public Object micros(Function function, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
      if (function.getCardinality() > 0) {
        CompilerDirectives.transferToInterpreterAndInvalidate();
        throw new BadArgException("async function accepts only functions with zero arguments. Function " + function + " expects " + function.getCardinality() + "arguments", this);
      }
      final long start = System.nanoTime();
      try {
        Object result = dispatch.execute(function);
        if (result instanceof Promise promise) {
          if (!promise.isFulfilled()) {
            return promise.map(res -> new Tuple(System.nanoTime() - start, res) ,this);
          } else {
            return new Tuple(System.nanoTime() - start, promise.unwrapOrThrow());
          }
        } else {
          return new Tuple(System.nanoTime() - start, result);
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

  public Builtins builtins() {
    Builtins builtins = new Builtins();
    builtins.register(new ExportedFunction(StopWatchBuiltinModuleFactory.NanosBuiltinFactory.getInstance()));
    return builtins;
  }
}
