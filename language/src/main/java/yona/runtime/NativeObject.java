package yona.runtime;

import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.interop.TruffleObject;
import com.oracle.truffle.api.library.ExportLibrary;

import java.util.Objects;

@ExportLibrary(InteropLibrary.class)
public class NativeObject implements TruffleObject {
  private Object value;

  public NativeObject(Object value) {
    this.value = value;
  }

  static boolean isInstance(TruffleObject nativeObject) {
    return nativeObject instanceof NativeObject;
  }

  public Object getValue() {
    return value;
  }

  @Override
  public String toString() {
    return "NativeObject{" +
        "value=" + value +
        '}';
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    NativeObject that = (NativeObject) o;
    return Objects.equals(value, that.value);
  }

  @Override
  public int hashCode() {
    return Objects.hash(value);
  }
}
