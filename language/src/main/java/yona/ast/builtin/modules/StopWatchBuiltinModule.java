package yona.ast.builtin.modules;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.interop.ArityException;
import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.interop.UnsupportedMessageException;
import com.oracle.truffle.api.interop.UnsupportedTypeException;
import com.oracle.truffle.api.library.CachedLibrary;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.YonaException;
import yona.ast.builtin.BuiltinNode;
import yona.ast.call.InvokeNode;
import yona.runtime.Context;
import yona.runtime.Function;
import yona.runtime.Tuple;
import yona.runtime.async.Promise;
import yona.runtime.exceptions.BadArgException;
import yona.runtime.exceptions.RuntimeException;
import yona.runtime.exceptions.UndefinedNameException;
import yona.runtime.stdlib.Builtins;
import yona.runtime.stdlib.ExportedFunction;

@BuiltinModuleInfo(moduleName = "Stopwatch")
public final class StopWatchBuiltinModule implements BuiltinModule {
  @NodeInfo(shortName = "nanos")
  abstract static class NanosBuiltin extends BuiltinNode {
    @Specialization
    public Object micros(Function function, @CachedLibrary(limit = "3") InteropLibrary dispatch) {
      if (function.getCardinality() > 0) {
        CompilerDirectives.transferToInterpreterAndInvalidate();
        throw new BadArgException("Function " + function + " expects " + function.getCardinality() + "arguments", this);
      }
      final long start = System.nanoTime();
      final Node thisNode = this;
      try {
        Object result = InvokeNode.dispatchFunction(function, dispatch, this);
        if (result instanceof Promise promise) {
          if (!promise.isFulfilled()) {
            return promise.map(res -> Tuple.allocate(thisNode, System.nanoTime() - start, res), this);
          } else {
            return Tuple.allocate(thisNode, System.nanoTime() - start, promise.unwrapOrThrow());
          }
        } else {
          return Tuple.allocate(thisNode, System.nanoTime() - start, result);
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
    return new Builtins(
        new ExportedFunction(StopWatchBuiltinModuleFactory.NanosBuiltinFactory.getInstance())
    );
  }
}
