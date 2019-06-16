package yatta.runtime;

import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.interop.TruffleObject;
import com.oracle.truffle.api.library.ExportLibrary;

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
}
