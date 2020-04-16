package yatta.runtime.stdlib.util;

import com.oracle.truffle.api.nodes.Node;
import yatta.runtime.Symbol;
import yatta.runtime.Tuple;
import yatta.runtime.async.Promise;
import yatta.runtime.exceptions.BadArgException;

public final class TimeUnitUtil {
  private TimeUnitUtil() {}

  /**
   * @return milis * factor
   */
  private static Object factor(Object timeTuple, double factor, Node node) {
    if (timeTuple instanceof Object[]) {
      Object[] elements = (Object[]) timeTuple;
      if (elements.length != 2) {
        throw new BadArgException("Invalid time unit tuple: " + elements, node);
      } else {
        if (!(elements[0] instanceof Symbol) || !(elements[1] instanceof Long)) {
          throw new BadArgException("Invalid time unit tuple: " + elements, node);
        } else {
          Symbol timeUnitSymbol = (Symbol) elements[0];
          String timeUnitSymbolString = timeUnitSymbol.asString();
          long timeValue = (long) elements[1];
          switch (timeUnitSymbolString) {
            case "seconds":
              return timeValue * factor * 1000;
            case "minutes":
              return timeValue * factor * 1000 * 60;
            case "hours":
              return timeValue * factor * 1000 * 60 * 60;
            case "days":
              return timeValue * factor * 1000 * 60 * 60 * 24;
            case "weeks":
              return timeValue * factor * 1000 * 60 * 60 * 24 * 7;
            default:
              throw new BadArgException("Unknown time unit: " + timeUnitSymbolString, node);
          }
        }
      }
    } else { // Promise
      Promise elementsPromise = (Promise) timeTuple;
      return elementsPromise.map(elements -> factor(elements, factor, node), node);
    }
  }

  public static Object getSeconds(Tuple timeTuple, Node node) {
    return factor(timeTuple.unwrapPromises(node), 1 / 1000, node);
  }
}
