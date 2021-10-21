package yona.ast.call;

import com.oracle.truffle.api.instrumentation.StandardTags;
import com.oracle.truffle.api.instrumentation.Tag;
import com.oracle.truffle.api.interop.ArityException;
import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.interop.UnsupportedMessageException;
import com.oracle.truffle.api.interop.UnsupportedTypeException;
import com.oracle.truffle.api.nodes.Node;
import yona.ast.ExpressionNode;
import yona.runtime.Function;
import yona.runtime.async.Promise;
import yona.runtime.exceptions.UndefinedNameException;

public abstract class InvokeNode extends ExpressionNode {
  public static Object dispatchFunction(Function function, InteropLibrary library, Node node, Object... argumentValues) {
    Function dispatchFunction = function;
    while (true) {
      try {
        return library.execute(dispatchFunction, argumentValues);
      } catch (TailCallException e) {
        dispatchFunction = e.function;
        argumentValues = e.arguments;
      } catch (ArityException | UnsupportedTypeException | UnsupportedMessageException e) {
        /* Execute was not successful. */
        throw UndefinedNameException.undefinedFunction(node, dispatchFunction);
      }
    }
  }

  public static Object dispatchFunctionUnwrapArgs(Function function, boolean unwrapPromises, InteropLibrary library, ExpressionNode node, Object... argumentValues) {
    if (unwrapPromises) {
      Promise argsPromise = Promise.all(argumentValues, node);
      return argsPromise.map(argValues -> dispatchFunction(function, library, node, (Object[]) argValues), node);
    } else {
      if (node.isTail()) {
        throw new TailCallException(function, argumentValues);
      }

      return dispatchFunction(function, library, node, (Object[]) argumentValues);
    }
  }

  @Override
  public boolean hasTag(Class<? extends Tag> tag) {
    if (tag == StandardTags.CallTag.class) {
      return true;
    }
    return super.hasTag(tag);
  }
}
