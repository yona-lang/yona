package yatta.runtime;

import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.interop.TruffleObject;
import com.oracle.truffle.api.library.ExportLibrary;
import com.oracle.truffle.api.library.ExportMessage;

import java.util.Arrays;
import java.util.List;

@ExportLibrary(InteropLibrary.class)
public class StringList implements TruffleObject {
  private final String[] items;

  public StringList(String... items) {
    this.items = items;
  }

  public List<String> asJavaList() {
    return Arrays.asList(items);
  }

  @Override
  public String toString() {
    String toStr = Arrays.toString(items);
    return "[" + toStr.substring(1, toStr.length() - 1) + ']';
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

  static boolean isInstance(TruffleObject list) {
    return list instanceof StringList;
  }
}
