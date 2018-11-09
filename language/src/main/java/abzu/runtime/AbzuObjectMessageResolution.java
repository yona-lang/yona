package abzu.runtime;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.interop.*;
import com.oracle.truffle.api.nodes.Node;
import com.oracle.truffle.api.object.DynamicObject;
import abzu.ast.access.AbzuReadPropertyCacheNode;
import abzu.ast.access.AbzuReadPropertyCacheNodeGen;
import abzu.ast.call.AbzuDispatchNode;
import abzu.ast.call.AbzuDispatchNodeGen;
import abzu.ast.interop.AbzuForeignToAbzuTypeNode;
import abzu.ast.interop.AbzuForeignToAbzuTypeNodeGen;

/**
 * The class containing all message resolution implementations of an SL object.
 */
@MessageResolution(receiverType = AbzuObjectType.class)
public class AbzuObjectMessageResolution {

  /*
   * An SL object resolves the READ message and maps it to an object property read access.
   */
  @Resolve(message = "READ")
  public abstract static class AbzuForeignReadNode extends Node {

    @Child
    private AbzuReadPropertyCacheNode read = AbzuReadPropertyCacheNodeGen.create();
    @Child
    private AbzuForeignToAbzuTypeNode nameToSLType = AbzuForeignToAbzuTypeNodeGen.create();

    public Object access(DynamicObject receiver, Object name) {
      Object convertedName = nameToSLType.executeConvert(name);
      Object result;
      try {
        result = read.executeRead(receiver, convertedName);
      } catch (AbzuUndefinedNameException undefinedName) {
        throw UnknownIdentifierException.raise(String.valueOf(convertedName));
      }
      return result;
    }
  }

  /*
   * An Abzu object resolves the INVOKE message and maps it to an object property read access
   * followed by an function invocation. The object property must be an SL function object, which
   * is executed eventually.
   */
  @Resolve(message = "INVOKE")
  public abstract static class AbzuForeignInvokeNode extends Node {

    @Child
    private AbzuDispatchNode dispatch = AbzuDispatchNodeGen.create();

    public Object access(DynamicObject receiver, String name, Object[] arguments) {
      Object property = receiver.get(name);
      if (property instanceof AbzuFunction) {
        AbzuFunction function = (AbzuFunction) property;
        Object[] arr = new Object[arguments.length];
        // Before the arguments can be used by the SLFunction, they need to be converted to
        // SL
        // values.
        for (int i = 0; i < arguments.length; i++) {
          arr[i] = AbzuContext.fromForeignValue(arguments[i]);
        }
        Object result = dispatch.executeDispatch(function, arr);
        return result;
      } else {
        throw UnknownIdentifierException.raise(name);
      }
    }
  }

  @Resolve(message = "HAS_KEYS")
  public abstract static class AbzuForeignHasPropertiesNode extends Node {

    @SuppressWarnings("unused")
    public Object access(DynamicObject receiver) {
      return true;
    }
  }

  @Resolve(message = "KEY_INFO")
  public abstract static class AbzuForeignPropertyInfoNode extends Node {

    public int access(DynamicObject receiver, Object name) {
      Object property = receiver.get(name);
      if (property == null) {
        return KeyInfo.INSERTABLE;
      } else if (property instanceof AbzuFunction) {
        return KeyInfo.READABLE | KeyInfo.REMOVABLE | KeyInfo.MODIFIABLE | KeyInfo.INVOCABLE;
      } else {
        return KeyInfo.READABLE | KeyInfo.REMOVABLE | KeyInfo.MODIFIABLE;
      }
    }
  }

  @Resolve(message = "KEYS")
  public abstract static class AbzuForeignPropertiesNode extends Node {
    public Object access(DynamicObject receiver) {
      return obtainKeys(receiver);
    }

    @CompilerDirectives.TruffleBoundary
    private static Object obtainKeys(DynamicObject receiver) {
      Object[] keys = receiver.getShape().getKeyList().toArray();
      return new KeysArray(keys);
    }
  }

  @MessageResolution(receiverType = KeysArray.class)
  static final class KeysArray implements TruffleObject {

    private final Object[] keys;

    KeysArray(Object[] keys) {
      this.keys = keys;
    }

    @Resolve(message = "HAS_SIZE")
    abstract static class HasSize extends Node {

      public Object access(@SuppressWarnings("unused") KeysArray receiver) {
        return true;
      }
    }

    @Resolve(message = "GET_SIZE")
    abstract static class GetSize extends Node {

      public Object access(KeysArray receiver) {
        return receiver.keys.length;
      }
    }

    @Resolve(message = "READ")
    abstract static class Read extends Node {

      public Object access(KeysArray receiver, int index) {
        try {
          Object key = receiver.keys[index];
          assert key instanceof String;
          return key;
        } catch (IndexOutOfBoundsException e) {
          CompilerDirectives.transferToInterpreter();
          throw UnknownIdentifierException.raise(String.valueOf(index));
        }
      }
    }

    @Override
    public ForeignAccess getForeignAccess() {
      return KeysArrayForeign.ACCESS;
    }

    static boolean isInstance(TruffleObject array) {
      return array instanceof KeysArray;
    }

  }
}
