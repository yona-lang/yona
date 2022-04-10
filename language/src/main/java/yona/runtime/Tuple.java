package yona.runtime;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.instrumentation.AllocationReporter;
import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.interop.TruffleObject;
import com.oracle.truffle.api.library.ExportLibrary;
import com.oracle.truffle.api.library.ExportMessage;
import com.oracle.truffle.api.nodes.Node;
import yona.runtime.async.Promise;

import java.util.Arrays;

@ExportLibrary(InteropLibrary.class)
public class Tuple implements TruffleObject {
  protected Object[] items;

  protected Tuple(Object... items) {
    this.items = items;
  }

  // context may be null when called from unit tests
  public static Tuple allocate(Node node, Object... items) {
    if (node != null) {
      Context context = Context.get(node);
      context.getAllocationReporter().onEnter(null, 0, AllocationReporter.SIZE_UNKNOWN);
      Tuple tuple = new Tuple(items);
      context.getAllocationReporter().onReturnValue(tuple, 0, AllocationReporter.SIZE_UNKNOWN);
      return tuple;
    } else {
      return new Tuple(items);
    }
  }

  @Override
  @CompilerDirectives.TruffleBoundary
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

  public final int length() {
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

  @ExportMessage
  public boolean isString() {
    return true;
  }

  @ExportMessage
  @CompilerDirectives.TruffleBoundary
  public String asString() {
    return toString();
  }

  public int size() {
    return items.length;
  }

  public Object get(int i) {
    return items[i];
  }

  public Object[] toArray() {
    return items;
  }

  @CompilerDirectives.TruffleBoundary
  public Object unwrapPromises(final Node node) {
    boolean hasPromise = false;
    for (Object item : items) {
      if (item instanceof Promise) {
        hasPromise = true;
        break;
      }
    }

    if (!hasPromise) {
      return items;
    } else {
      return Promise.all(items, node);
    }
  }
}
