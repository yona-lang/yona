package yona.runtime.stdlib.util;

import com.oracle.truffle.api.nodes.Node;
import yona.runtime.Symbol;
import yona.runtime.Tuple;
import yona.runtime.async.Promise;
import yona.runtime.exceptions.BadArgException;

import java.util.Arrays;

public final class TimeUnitUtil {
  private TimeUnitUtil() {
  }

  /**
   * @return millis * factor
   */
  private static Object factor(Object timeTuple, double factor, Node node) {
    if (timeTuple instanceof Object[] elements) {
      if (elements.length != 2) {
        throw new BadArgException("Invalid time unit tuple: " + Arrays.toString(elements), node);
      } else {
        if (!(elements[0] instanceof Symbol timeUnitSymbol) || !(elements[1] instanceof Long)) {
          throw new BadArgException("Invalid time unit tuple: " + Arrays.toString(elements), node);
        } else {
          String timeUnitSymbolString = timeUnitSymbol.asString();
          long timeValue = (long) elements[1];
          return switch (timeUnitSymbolString) {
            case "milli" -> (long) (timeValue * factor);
            case "millis" -> (long) (timeValue * factor);
            case "second" -> (long) (timeValue * factor * 1000);
            case "seconds" -> (long) (timeValue * factor * 1000);
            case "minute" -> (long) (timeValue * factor * 1000 * 60);
            case "minutes" -> (long) (timeValue * factor * 1000 * 60);
            case "hour" -> (long) (timeValue * factor * 1000 * 60 * 60);
            case "hours" -> (long) (timeValue * factor * 1000 * 60 * 60);
            case "day" -> (long) (timeValue * factor * 1000 * 60 * 60 * 24);
            case "days" -> (long) (timeValue * factor * 1000 * 60 * 60 * 24);
            case "week" -> (long) (timeValue * factor * 1000 * 60 * 60 * 24 * 7);
            case "weeks" -> (long) (timeValue * factor * 1000 * 60 * 60 * 24 * 7);
            default -> throw new BadArgException("Unknown time unit: " + timeUnitSymbolString, node);
          };
        }
      }
    } else { // Promise
      Promise elementsPromise = (Promise) timeTuple;
      return elementsPromise.map(elements -> factor(elements, factor, node), node);
    }
  }

  public static Object getSeconds(Tuple timeTuple, Node node) {
    return factor(timeTuple.unwrapPromises(node), 1.0 / 1000, node);
  }

  public static Object getMilliseconds(Tuple timeTuple, Node node) {
    return factor(timeTuple.unwrapPromises(node), 1, node);
  }

  public static Object timeTupleToSeconds(Object timeTuple, Node node) {
    if (timeTuple instanceof Object[] elements) {
      if (elements.length != 3) {
        throw new BadArgException("Invalid time tuple: " + Arrays.toString(elements), node);
      } else {
        if (!(elements[0] instanceof Long) || !(elements[1] instanceof Long) || !(elements[2] instanceof Long)) {
          throw new BadArgException("Invalid time tuple: " + Arrays.toString(elements), node);
        } else {
          long hour = (long) elements[0];
          long minute = (long) elements[1];
          long second = (long) elements[2];

          if (hour > 24 || minute > 60 || second > 60) {
            throw new BadArgException("Invalid time tuple: " + Arrays.toString(elements), node);
          }

          return hour * 3600 * minute * 60 + second;
        }
      }
    } else { // Promise
      Promise elementsPromise = (Promise) timeTuple;
      return elementsPromise.map(elements -> timeTupleToSeconds(elements, node), node);
    }
  }
}
