package yatta.runtime;

import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.interop.TruffleObject;
import com.oracle.truffle.api.library.ExportLibrary;
import com.oracle.truffle.api.library.ExportMessage;

import java.util.Arrays;

@ExportLibrary(InteropLibrary.class)
public class Tuple implements TruffleObject {
  private final Object[] items;

  public Tuple(Object... items) {
    this.items = items;
  }

  @Override
  public String toString() {
    String toStr = Arrays.toString(items);
    return "(" + toStr.substring(1, toStr.length() - 1) + ')';
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    Tuple tuple = (Tuple) o;
    return Arrays.equals(items, tuple.items);
  }

  @Override
  public int hashCode() {
    return Arrays.hashCode(items);
  }

  @ExportMessage
  public final long getArraySize() {
    return items.length;
  }

  @ExportMessage
  public final Object readArrayElement(long index) {
    return items[(int) index];
  }

  @ExportMessage
  public final boolean isArrayElementReadable(long index) {
    return index < items.length;
  }

  @ExportMessage
  public final boolean hasArrayElements() {
    return true;
  }

  static boolean isInstance(TruffleObject tuple) {
    return tuple instanceof Tuple;
  }

  public int size() {
    return items.length;
  }

  public Object get(int i) {
    return items[i];
  }
}
