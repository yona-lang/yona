package yona.runtime.threading;

import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.nodes.Node;
import yona.YonaException;
import yona.ast.call.InvokeNode;
import yona.runtime.Function;
import yona.runtime.async.Promise;

import java.util.function.Supplier;

public interface ExecutableFunction {
  void execute(Promise promise);

  final class JavaExecutableFunction implements ExecutableFunction {
    private final Supplier<?> function;
    private final Node node;

    public JavaExecutableFunction(Supplier<?> function, Node node) {
      this.function = function;
      this.node = node;
    }

    public void execute(Promise promise) throws YonaException {
      try {
        promise.fulfil(function.get(), node);
      } catch (Throwable e) {
        promise.fulfil(e, node);
      }
    }
  }

  final class YonaExecutableFunction implements ExecutableFunction {
    private final Function function;
    private final InteropLibrary dispatch;
    private final Node node;

    public YonaExecutableFunction(Function function, InteropLibrary dispatch, Node node) throws YonaException {
      this.function = function;
      this.dispatch = dispatch;
      this.node = node;
    }

    public void execute(Promise promise) {
      try {
        promise.fulfil(InvokeNode.dispatchFunction(function, dispatch, node), node);
      } catch (Throwable e) {
        promise.fulfil(e, node);
      }
    }
  }
}

