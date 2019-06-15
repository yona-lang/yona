package abzu.runtime;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.interop.*;
import com.oracle.truffle.api.nodes.Node;

import java.util.Arrays;

@MessageResolution(receiverType = Tuple.class)
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

  @Override
  public ForeignAccess getForeignAccess() {
    return TupleForeign.ACCESS;
  }

  @Resolve(message = "GET_SIZE")
  abstract static class GetSize extends Node {
    Object access(Tuple obj) {
      return obj.items.length;
    }
  }

  @Resolve(message = "HAS_SIZE")
  abstract static class HasSize extends Node {
    public Object access(@SuppressWarnings("unused") Tuple receiver) {
      return true;
    }
  }

  @Resolve(message = "KEY_INFO")
  public abstract static class InfoNode extends Node {

    public int access(Tuple receiver, int index) {
      if (index < receiver.items.length) {
        return KeyInfo.READABLE;
      } else {
        return KeyInfo.NONE;
      }
    }
  }

  @Resolve(message = "READ")
  abstract static class Read extends Node {
    public Object access(Tuple receiver, int index) {
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
    return tuple instanceof Tuple;
  }

  public int size() {
    return items.length;
  }

  public Object get(int i) {
    return items[i];
  }
}
