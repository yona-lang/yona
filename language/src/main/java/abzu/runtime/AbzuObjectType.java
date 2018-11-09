package abzu.runtime;

import com.oracle.truffle.api.interop.ForeignAccess;
import com.oracle.truffle.api.interop.TruffleObject;
import com.oracle.truffle.api.object.DynamicObject;
import com.oracle.truffle.api.object.ObjectType;

public final class AbzuObjectType extends ObjectType {

  public static final ObjectType INSTANCE = new AbzuObjectType();

  private AbzuObjectType() {
  }

  public static boolean isInstance(TruffleObject obj) {
    return AbzuContext.isAbzuObject(obj);
  }

  @Override
  public ForeignAccess getForeignAccessFactory(DynamicObject obj) {
    return AbzuObjectMessageResolutionForeign.ACCESS;
  }
}
