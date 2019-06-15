package yatta.runtime;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.interop.InteropLibrary;
import com.oracle.truffle.api.interop.InvalidArrayIndexException;
import com.oracle.truffle.api.interop.TruffleObject;
import com.oracle.truffle.api.library.ExportLibrary;
import com.oracle.truffle.api.library.ExportMessage;

import java.util.HashMap;
import java.util.List;
import java.util.Map;

@ExportLibrary(InteropLibrary.class)
@SuppressWarnings("static-method")
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

  @ExportMessage
  boolean hasMembers() {
    return true;
  }

  @ExportMessage
  @CompilerDirectives.TruffleBoundary
  Object readMember(String member) {
    return functions.get(member);
  }

  @ExportMessage
  @CompilerDirectives.TruffleBoundary
  boolean isMemberReadable(String member) {
    return functions.containsKey(member);
  }

  @ExportMessage
  @CompilerDirectives.TruffleBoundary
  Object getMembers(@SuppressWarnings("unused") boolean includeInternal) {
    return new FunctionNamesObject(functions.keySet().toArray());
  }

  public static boolean isInstance(TruffleObject obj) {
    return obj instanceof Module;
  }

  @ExportLibrary(InteropLibrary.class)
  static final class FunctionNamesObject implements TruffleObject {

    private final Object[] names;

    FunctionNamesObject(Object[] names) {
      this.names = names;
    }

    @ExportMessage
    boolean hasArrayElements() {
      return true;
    }

    @ExportMessage
    boolean isArrayElementReadable(long index) {
      return index >= 0 && index < names.length;
    }

    @ExportMessage
    long getArraySize() {
      return names.length;
    }

    @ExportMessage
    Object readArrayElement(long index) throws InvalidArrayIndexException {
      if (!isArrayElementReadable(index)) {
        CompilerDirectives.transferToInterpreter();
        throw InvalidArrayIndexException.create(index);
      }
      return names[(int) index];
    }
  }
}
