package yona.runtime;

import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.interop.TruffleObject;
import com.oracle.truffle.api.library.ExportLibrary;
import com.oracle.truffle.api.nodes.Node;
import yona.YonaException;

import java.util.Objects;

@ExportLibrary(InteropLibrary.class)
public class NativeObject<T> implements TruffleObject {
  private final T value;

  public NativeObject(T value) {
    this.value = value;
  }

  static boolean isInstance(TruffleObject nativeObject) {
    return nativeObject instanceof NativeObject;
  }

  /**
   * Only for use in guard expressions
   */
  public T getValue() {
    return value;
  }

  public <C> C getValue(Class<C> dataType, Node node) {
    if (!(dataType.isAssignableFrom(value.getClass()))) {
      throw YonaException.typeError(node, value);
    } else {
      return dataType.cast(value);
    }
  }

  @Override
  public String toString() {
    return "NATIVE(" + value + ')';
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    NativeObject<?> that = (NativeObject<?>) o;
    return Objects.equals(value, that.value);
  }

  @Override
  public int hashCode() {
    return Objects.hash(value);
  }
}
