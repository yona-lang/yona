package abzu.runtime;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.interop.*;
import com.oracle.truffle.api.nodes.Node;

import java.util.*;

public final class Module implements TruffleObject {
  final String fqn;
  final List<String> exports;
  final Map<String, Function> functions = new HashMap<>();

  public Module(String fqn, List<String> exports, List<Function> functionsList) {
    this.fqn = fqn;
    this.exports = exports;

    for (Function fun : functionsList) {
      this.functions.put(fun.getName(), fun);
    }
  }

  @Override
  public String toString() {
    return "Module{" +
           "fqn=" + fqn +
           ", exports=" + exports +
           ", functions=" + functions +
           '}';
  }

  public String getFqn() {
    return fqn;
  }

  public List<String> getExports() {
    return exports;
  }

  public Map<String, Function> getFunctions() {
    return functions;
  }

  @Override
  public ForeignAccess getForeignAccess() {
    return ModuleMessageResolutionForeign.ACCESS;
  }

  public static boolean isInstance(TruffleObject obj) {
    return obj instanceof Module;
  }

  @MessageResolution(receiverType = Module.class)
  static final class ModuleMessageResolution {

    @Resolve(message = "HAS_KEYS")
    abstract static class ModuleHasKeysNode extends Node {

      @SuppressWarnings("unused")
      public Object access(Module fo) {
        return true;
      }
    }

    @Resolve(message = "KEYS")
    abstract static class ModuleKeysNode extends Node {

      @CompilerDirectives.TruffleBoundary
      public Object access(Module fo) {
        return new ModuleMessageResolution.FunctionNamesObject(fo.functions.keySet());
      }
    }

    @Resolve(message = "KEY_INFO")
    abstract static class ModuleKeyInfoNode extends Node {

      @CompilerDirectives.TruffleBoundary
      public Object access(Module fo, String name) {
        if (fo.functions.containsKey(name)) {
          return 3;
        } else {
          return 0;
        }
      }
    }

    @Resolve(message = "READ")
    abstract static class ModuleReadNode extends Node {

      @CompilerDirectives.TruffleBoundary
      public Object access(Module fo, String name) {
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
        return obj instanceof ModuleMessageResolution.FunctionNamesObject;
      }

      @MessageResolution(receiverType = ModuleMessageResolution.FunctionNamesObject.class)
      static final class FunctionNamesMessageResolution {

        @Resolve(message = "HAS_SIZE")
        abstract static class FunctionNamesHasSizeNode extends Node {

          @SuppressWarnings("unused")
          public Object access(ModuleMessageResolution.FunctionNamesObject namesObject) {
            return true;
          }
        }

        @Resolve(message = "GET_SIZE")
        abstract static class FunctionNamesGetSizeNode extends Node {

          @CompilerDirectives.TruffleBoundary
          public Object access(ModuleMessageResolution.FunctionNamesObject namesObject) {
            return namesObject.names.size();
          }
        }

        @Resolve(message = "READ")
        abstract static class FunctionNamesReadNode extends Node {

          @CompilerDirectives.TruffleBoundary
          public Object access(ModuleMessageResolution.FunctionNamesObject namesObject, int index) {
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
