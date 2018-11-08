package abzu.runtime;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.interop.*;
import com.oracle.truffle.api.nodes.Node;

@MessageResolution(receiverType = AbzuTuple.class)
public class AbzuTuple implements TruffleObject {
  private final Object[] items;

  public AbzuTuple(Object... items) {
    this.items = items;
  }

  @Override
  public ForeignAccess getForeignAccess() {
    return AbzuTupleForeign.ACCESS;
  }

  @Resolve(message = "GET_SIZE")
  abstract static class GetSize extends Node {
    Object access(AbzuTuple obj) {
      return obj.items.length;
    }
  }

  @Resolve(message = "HAS_SIZE")
  abstract static class HasSize extends Node {
    public Object access(@SuppressWarnings("unused") AbzuTuple receiver) {
      return true;
    }
  }

  @Resolve(message = "KEY_INFO")
  public abstract static class InfoNode extends Node {

    public int access(AbzuTuple receiver, int index) {
      if (index < receiver.items.length) {
        return KeyInfo.READABLE;
      } else {
        return KeyInfo.NONE;
      }
    }
  }

  @Resolve(message = "READ")
  abstract static class Read extends Node {
    public Object access(AbzuTuple receiver, int index) {
      try {
        Object key = receiver.items[index];
        assert key instanceof Number;
        return key;
      } catch (IndexOutOfBoundsException e) {
        CompilerDirectives.transferToInterpreter();
        throw UnknownIdentifierException.raise(String.valueOf(index));
      }
    }
  }

  static boolean isInstance(TruffleObject tuple) {
    return tuple instanceof AbzuTuple;
  }
}
