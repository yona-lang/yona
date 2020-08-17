package yona.runtime.stdlib.util;

import com.oracle.truffle.api.nodes.Node;
import yona.runtime.exceptions.BadArgException;

import java.util.function.Predicate;

public final class YonaAssert {
  public static <T> void badArgAssert(T val, String description, Node node, Predicate<T> predicate) {
    if (!predicate.test(val)) {
      throw new BadArgException("Bad argument error: " + description, node);
    }
  }
}
