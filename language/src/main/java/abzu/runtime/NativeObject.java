package abzu.runtime;

import com.oracle.truffle.api.interop.ForeignAccess;
import com.oracle.truffle.api.interop.MessageResolution;
import com.oracle.truffle.api.interop.TruffleObject;

@MessageResolution(receiverType = NativeObject.class)
public class NativeObject implements TruffleObject {
  private Object value;

  public NativeObject(Object value) {
    this.value = value;
  }

  @Override
  public ForeignAccess getForeignAccess() {
    return NativeObjectForeign.ACCESS;
  }

  static boolean isInstance(TruffleObject nativeObject) {
    return nativeObject instanceof NativeObject;
  }

  public Object getValue() {
    return value;
  }
}
