package yatta.runtime.stdlib.util;

import com.oracle.truffle.api.nodes.Node;
import yatta.runtime.exceptions.BadArgException;

import java.util.function.Predicate;

public final class YattaAssert {
  public static <T> void badArgAssert(T val, String description, Node node, Predicate<T> predicate) {
    if (!predicate.test(val)) {
      throw new BadArgException("Bad aargument error: " + description, node);
    }
  }
}
