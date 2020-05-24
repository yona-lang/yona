package yatta.runtime;

import com.oracle.truffle.api.nodes.ExplodeLoop;

import java.lang.reflect.Array;
import java.util.function.Function;

public final class ArrayUtils {
  public static <T> T[] addElementToArray(T arr[], Class<T> cls, T x) {
    final int len = arr.length;
    @SuppressWarnings("unchecked")
    T newarr[] = (T[]) Array.newInstance(cls, len + 1);

    System.arraycopy(arr, 0, newarr, 0, len);
    newarr[len] = x;

    return newarr;
  }

  public static String[] addElementToArray(String arr[], String x) {
    final int len = arr.length;
    String newarr[] = new String[len + 1];

    System.arraycopy(arr, 0, newarr, 0, len);
    newarr[len] = x;

    return newarr;
  }

  public static <T> T[] catenate(T[] a, T[] b) {
    int aLen = a.length;
    int bLen = b.length;

    @SuppressWarnings("unchecked")
    T[] c = (T[]) Array.newInstance(a.getClass().getComponentType(), aLen + bLen);
    System.arraycopy(a, 0, c, 0, aLen);
    System.arraycopy(b, 0, c, aLen, bLen);

    return c;
  }

  @ExplodeLoop
  public static <T> String[] catenateMany(T[] args, Function<T, String[]> getter) {
    String[][] identifiers = new String[args.length][];
    int totalLen = 0;
    for (int i = 0; i < args.length; i++) {
      String[] currentIdentifiers = getter.apply(args[i]);
      identifiers[i] = currentIdentifiers;
      totalLen += currentIdentifiers.length;
    }

    String[] ret = new String[totalLen];
    int pos = 0;
    for (int i = 0; i < args.length; i++) {
      String[] currentIdentifiers = identifiers[i];
      if (currentIdentifiers.length > 0) {
        System.arraycopy(currentIdentifiers, 0, ret, pos, currentIdentifiers.length);
        pos += currentIdentifiers.length;
      }
    }
    return ret;
  }
}
