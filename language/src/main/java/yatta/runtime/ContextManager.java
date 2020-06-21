package yatta.runtime;

import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import yatta.TypesGen;
import yatta.runtime.async.Promise;
import yatta.runtime.exceptions.BadArgException;

public class ContextManager<T> extends Tuple {
  public ContextManager(String contextIdentifier, Function enterFunction, Function leaveFunction, T data) {
    super(Seq.fromCharSequence(contextIdentifier), enterFunction, leaveFunction, data);
  }

  public ContextManager(Seq contextIdentifier, Function enterFunction, Function leaveFunction, T data) {
    super(contextIdentifier, enterFunction, leaveFunction, data);
  }

  public Seq contextIdentifier() {
    return (Seq) items[0];
  }

  public Function enterFunction() {
    return (Function) items[1];
  }

  public Function leaveFunction() {
    return (Function) items[2];
  }

  public T data() {
    return (T) items[3];
  }

  public static <T> ContextManager<T> fromItems(Object[] items, Node node) {
    if (items.length != 4) {
      throw invalidContextException(items, null, node);
    } else {
      try {
        return new ContextManager<T>(TypesGen.expectSeq(items[0]), TypesGen.expectFunction(items[1]), TypesGen.expectFunction(items[2]), (T) items[3]);
      } catch (UnexpectedResultException e) {
        throw invalidContextException(items, e, node);
      }
    }
  }

  public static Object ensureValid(Tuple tuple, Node node) {
    if (tuple.length() != 4) {
      throw invalidContextException(tuple, null, node);
    }

    Object evaluatedTuple = tuple.unwrapPromises(node);
    if (evaluatedTuple instanceof Promise) {
      Promise evaluatedTuplePromise = (Promise) evaluatedTuple;

      return evaluatedTuplePromise.map((items) -> fromItems((Object[]) items, node), node);
    } else {
      return fromItems((Object[]) tuple.items, node);
    }
  }

  public static RuntimeException invalidContextException(Object was, Throwable cause, Node node) {
    return new BadArgException("Invalid context manager tuple. Must be a tuple in form of (identifier, enterFunction, leaveFunction, contextValue). Was: " + was, cause, node);
  }
}
