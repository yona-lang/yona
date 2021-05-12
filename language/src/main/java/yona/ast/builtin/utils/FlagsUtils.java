package yona.ast.builtin.utils;

import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.nodes.UnexpectedResultException;
import yona.TypesGen;
import yona.YonaException;
import yona.runtime.Set;
import yona.runtime.async.Promise;

import java.util.Arrays;
import java.util.function.Function;

public class FlagsUtils {
  public static <T, R> Object withFlags(Object promiseOrFlags, Function<T[], R> callback, Node node) {
    if (promiseOrFlags instanceof Promise promise) {
      return promise.map(flags -> callback.apply((T[]) flags), node);
    } else {
      return callback.apply((T[]) promiseOrFlags);
    }
  }

  public static <T> Object extractFlags(Set items, Function<String, T> mappingFunction, Node node) {
    Object unwrapResult = items.unwrapPromises(node);
    if (unwrapResult instanceof Promise promise) {
      return promise.map(els -> extractFlagsFromUnwrappedArray((Object[]) els, mappingFunction, node), node);
    } else {
      return extractFlagsFromUnwrappedArray(items.toArray(), mappingFunction, node);
    }
  }

  @SuppressWarnings("unchecked")
  private static <T> T[] extractFlagsFromUnwrappedArray(Object[] items, Function<String, T> mappingFunction, Node node) {
    String[] checkedStrings = new String[items.length];

    for (int i = 0; i < items.length; i++) {
      try {
        checkedStrings[i] = TypesGen.expectSymbol(items[i]).asString();
      } catch (UnexpectedResultException e) {
        throw YonaException.typeError(node, items[i]);
      }
    }

    return (T[]) Arrays.stream(checkedStrings).map(mappingFunction).toArray();
  }
}
