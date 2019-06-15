package yatta.runtime;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.interop.*;
import com.oracle.truffle.api.nodes.Node;

import java.util.Arrays;
import java.util.List;

@MessageResolution(receiverType = StringList.class)
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

  @Override
  public ForeignAccess getForeignAccess() {
    return StringListForeign.ACCESS;
  }

  @Resolve(message = "GET_SIZE")
  abstract static class GetSize extends Node {
    Object access(StringList obj) {
      return obj.items.length;
    }
  }

  @Resolve(message = "HAS_SIZE")
  abstract static class HasSize extends Node {
    public Object access(@SuppressWarnings("unused") StringList receiver) {
      return true;
    }
  }

  @Resolve(message = "KEY_INFO")
  public abstract static class InfoNode extends Node {

    public int access(StringList receiver, int index) {
      if (index < receiver.items.length) {
        return KeyInfo.READABLE;
      } else {
        return KeyInfo.NONE;
      }
    }
  }

  @Resolve(message = "READ")
  abstract static class Read extends Node {
    public Object access(StringList receiver, int index) {
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

  static boolean isInstance(TruffleObject list) {
    return list instanceof StringList;
  }
}
