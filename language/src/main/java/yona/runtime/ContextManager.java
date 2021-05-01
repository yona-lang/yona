package yona.runtime;

import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import yona.TypesGen;
import yona.YonaException;
import yona.runtime.async.Promise;
import yona.runtime.exceptions.BadArgException;

public class ContextManager<T> extends Tuple {
  private final Class<T> dataType;

  public ContextManager(String contextIdentifier, Function wrapperFunction, T data) {
    super(Seq.fromCharSequence(contextIdentifier), wrapperFunction, data);
    this.dataType = (Class<T>) data.getClass();
  }

  public ContextManager(Seq contextIdentifier, Function wrapperFunction, T data) {
    super(contextIdentifier, wrapperFunction, data);
    this.dataType = (Class<T>) data.getClass();
  }

  public Seq contextIdentifier() {
    return (Seq) items[0];
  }

  public Function wrapperFunction() {
    return (Function) items[1];
  }

  public T getData(Node node) {
    if (!(dataType.isAssignableFrom(items[2].getClass()))) {
      throw YonaException.typeError(node, items[2]);
    } else {
      return dataType.cast(items[2]);
    }
  }

  @SuppressWarnings("unchecked")
  public static <T> ContextManager<T> fromItems(Object[] items, Node node) {
    if (items.length != 3) {
      throw invalidContextException(items, null, node);
    } else {
      try {
        return new ContextManager<T>(TypesGen.expectSeq(items[0]), TypesGen.expectFunction(items[1]), (T) items[2]);
      } catch (UnexpectedResultException e) {
        throw invalidContextException(items, e, node);
      }
    }
  }

  public static Object ensureValid(Tuple tuple, Node node) {
    if (tuple.length() != 3) {
      throw invalidContextException(tuple, null, node);
    }

    Object evaluatedTuple = tuple.unwrapPromises(node);
    if (evaluatedTuple instanceof Promise) {
      Promise evaluatedTuplePromise = (Promise) evaluatedTuple;

      return evaluatedTuplePromise.map((items) -> fromItems((Object[]) items, node), node);
    } else {
      return fromItems(tuple.items, node);
    }
  }

  public static RuntimeException invalidContextException(Object was, Throwable cause, Node node) {
    return new BadArgException("Invalid context manager tuple. Must be a tuple in form of (identifier, wrapFunction, contextValue). Was: " + was, cause, node);
  }
}
