package abzu.runtime;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.interop.*;
import com.oracle.truffle.api.nodes.Node;

import java.util.HashMap;
import java.util.Iterator;
import java.util.Map;
import java.util.Set;

class FunctionsObject implements TruffleObject {

  final Map<String, Function> functions = new HashMap<>();

  FunctionsObject() {
  }

  @Override
  public ForeignAccess getForeignAccess() {
    return FunctionsObjectMessageResolutionForeign.ACCESS;
  }

  public static boolean isInstance(TruffleObject obj) {
    return obj instanceof FunctionsObject;
  }

  @MessageResolution(receiverType = FunctionsObject.class)
  static final class FunctionsObjectMessageResolution {

    @Resolve(message = "HAS_KEYS")
    abstract static class FunctionsObjectHasKeysNode extends Node {

      @SuppressWarnings("unused")
      public Object access(FunctionsObject fo) {
        return true;
      }
    }

    @Resolve(message = "KEYS")
    abstract static class FunctionsObjectKeysNode extends Node {

      @CompilerDirectives.TruffleBoundary
      public Object access(FunctionsObject fo) {
        return new FunctionsObjectMessageResolution.FunctionNamesObject(fo.functions.keySet());
      }
    }

    @Resolve(message = "KEY_INFO")
    abstract static class FunctionsObjectKeyInfoNode extends Node {

      @CompilerDirectives.TruffleBoundary
      public Object access(FunctionsObject fo, String name) {
        if (fo.functions.containsKey(name)) {
          return 3;
        } else {
          return 0;
        }
      }
    }

    @Resolve(message = "READ")
    abstract static class FunctionsObjectReadNode extends Node {

      @CompilerDirectives.TruffleBoundary
      public Object access(FunctionsObject fo, String name) {
        try {
          return fo.functions.get(name);
        } catch (IndexOutOfBoundsException ioob) {
          return null;
        }
      }
    }

    static final class FunctionNamesObject implements TruffleObject {

      private final Set<String> names;

      private FunctionNamesObject(Set<String> names) {
        this.names = names;
      }

      @Override
      public ForeignAccess getForeignAccess() {
        return FunctionNamesMessageResolutionForeign.ACCESS;
      }

      public static boolean isInstance(TruffleObject obj) {
        return obj instanceof FunctionsObjectMessageResolution.FunctionNamesObject;
      }

      @MessageResolution(receiverType = FunctionsObjectMessageResolution.FunctionNamesObject.class)
      static final class FunctionNamesMessageResolution {

        @Resolve(message = "HAS_SIZE")
        abstract static class FunctionNamesHasSizeNode extends Node {

          @SuppressWarnings("unused")
          public Object access(FunctionsObjectMessageResolution.FunctionNamesObject namesObject) {
            return true;
          }
        }

        @Resolve(message = "GET_SIZE")
        abstract static class FunctionNamesGetSizeNode extends Node {

          @CompilerDirectives.TruffleBoundary
          public Object access(FunctionsObjectMessageResolution.FunctionNamesObject namesObject) {
            return namesObject.names.size();
          }
        }

        @Resolve(message = "READ")
        abstract static class FunctionNamesReadNode extends Node {

          @CompilerDirectives.TruffleBoundary
          public Object access(FunctionsObjectMessageResolution.FunctionNamesObject namesObject, int index) {
            if (index >= namesObject.names.size()) {
              throw UnknownIdentifierException.raise(Integer.toString(index));
            }
            Iterator<String> iterator = namesObject.names.iterator();
            int i = index;
            while (i-- > 0) {
              iterator.next();
            }
            return iterator.next();
          }
        }

      }
    }
  }
}
